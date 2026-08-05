# Porting FatFs to Raspberry Pi Pico with a Layered, Hardware-Independent Architecture

This document describes how to port [FatFs](http://elm-chan.org/fsw/ff/00index_e.html) to the
Raspberry Pi Pico (RP2040/RP2350) using a clean layered architecture where:

- The **hardware layer** (SPI/SDIO/QSPI flash drivers) is fully decoupled from FatFs.
- FatFs's own `diskio.c` glue talks to hardware only through an abstract **block device interface**.
- The application never calls `f_open`/`f_read`/`f_write` directly — it goes through an
  application-defined **filesystem interface** that wraps FatFs, so the app layer is not tied to
  FatFs's API either.

This gives you three independently swappable layers: hardware driver, disk I/O binding, and
filesystem API facade.

---

## 1. Layered Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                         Application Layer                       │
│         (WAV reader / audio engine / your business logic)       │
└───────────────────────────────┬───────────────────────────────--┘
                                 │ calls
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                    IFileSystem  (interface)                     │
│   fs_open() / fs_read() / fs_write() / fs_close() / fs_seek()    │
│   fs_stat()  / fs_mkdir() / fs_mount() / fs_unmount() ...        │
└───────────────────────────────┬───────────────────────────────--┘
                                 │ implemented by
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                    FatFsFileSystem (adapter)                    │
│     Translates IFileSystem calls into FatFs API calls:          │
│     f_open, f_read, f_write, f_close, f_lseek, f_mount ...       │
└───────────────────────────────┬───────────────────────────────--┘
                                 │ FatFs core calls
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                     FatFs Core  (ff.c / ff.h)                   │
│         Hardware-agnostic, unmodified upstream FatFs code        │
└───────────────────────────────┬───────────────────────────────--┘
                                 │ disk_read/disk_write/disk_ioctl
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│              diskio.c  (FatFs <-> IBlockDevice glue)            │
│    Implements disk_initialize/status/read/write/ioctl by         │
│    forwarding to the active IBlockDevice instance                │
└───────────────────────────────┬───────────────────────────────--┘
                                 │ calls
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                   IBlockDevice  (interface)                     │
│  bd_init() / bd_status() / bd_read_blocks() / bd_write_blocks()  │
│  bd_ioctl() / bd_block_size() / bd_block_count()                 │
└───────────────────────────────┬───────────────────────────────--┘
                                 │ implemented by
                    ┌────────────┼─────────────┐
                    ▼            ▼             ▼
          ┌─────────────┐ ┌─────────────┐ ┌─────────────┐
          │ SdSpiBlockDev│ │SdSdioBlockDev│ │QspiFlashDev │
          │ (SPI + SD)   │ │ (SDIO + SD)  │ │ (onboard    │
          │ hardware_spi │ │ RP2040 SDIO  │ │  flash XIP) │
          └─────────────┘ └─────────────┘ └─────────────┘
                 Pico SDK hardware drivers (bottom layer)
```

**Key idea:** Only the bottom two boxes (`IBlockDevice` implementations) know about real
hardware. Only the `FatFsFileSystem` adapter knows about FatFs. The application only knows
`IFileSystem`. Swapping SD-over-SPI for SDIO, or FatFs for LittleFS, touches exactly one layer.

---

## 2. Layer-by-Layer Breakdown

### 2.1 `IBlockDevice` — Hardware Abstraction Interface

Purpose: represent *any* block-addressable storage device (SD card, flash chip, RAM disk) with a
uniform C interface (a struct of function pointers, since this is C, not C++).

```c
// block_device.h
typedef struct {
    int  (*init)(void *ctx);
    int  (*status)(void *ctx);                 // returns disk status bits
    int  (*read)(void *ctx, uint8_t *buf, uint32_t sector, uint32_t count);
    int  (*write)(void *ctx, const uint8_t *buf, uint32_t sector, uint32_t count);
    int  (*ioctl)(void *ctx, uint8_t cmd, void *data);
    uint32_t block_size;
    uint32_t block_count;
    void *ctx;                                  // driver-private state (SPI instance, pins, etc.)
} block_device_t;
```

Each concrete driver (`sd_spi_block_device.c`, `sd_sdio_block_device.c`, `qspi_flash_block_device.c`)
populates one of these structs using Pico SDK calls (`hardware/spi.h`, `hardware/gpio.h`,
`hardware/flash.h`). None of this code knows FatFs exists.

### 2.2 `diskio.c` — FatFs Binding Layer

This is the **only** file that bridges FatFs to `IBlockDevice`. It implements the fixed signatures
FatFs expects (`disk_status`, `disk_initialize`, `disk_read`, `disk_write`, `disk_ioctl`,
`get_fattime`) by forwarding to whichever `block_device_t*` is registered for a given drive number.

```c
// diskio.c
static block_device_t *drives[FF_VOLUMES] = {0};

void diskio_register(BYTE pdrv, block_device_t *bd) { drives[pdrv] = bd; }

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    block_device_t *bd = drives[pdrv];
    if (!bd) return RES_NOTRDY;
    return bd->read(bd->ctx, buff, sector, count) == 0 ? RES_OK : RES_ERROR;
}
// ... disk_write, disk_status, disk_initialize, disk_ioctl similarly
```

This file, plus unmodified `ff.c`/`ff.h`/`ffconf.h` from elm-chan.org, form the "FatFs Core" box.
No changes to FatFs source itself are ever required.

### 2.3 `IFileSystem` — Application-Facing Filesystem Interface

Purpose: hide FatFs (or any future filesystem) behind an interface your application code depends
on, so the app is portable to LittleFS, a RAM filesystem for tests, etc.

```c
// ifilesystem.h
typedef struct fs_file fs_file_t;   // opaque handle

typedef struct {
    int  (*mount)(void *ctx, const char *path);
    int  (*unmount)(void *ctx, const char *path);
    int  (*open)(void *ctx, fs_file_t **out, const char *path, int flags);
    int  (*close)(void *ctx, fs_file_t *file);
    int  (*read)(void *ctx, fs_file_t *file, void *buf, size_t len, size_t *out_read);
    int  (*write)(void *ctx, fs_file_t *file, const void *buf, size_t len, size_t *out_written);
    int  (*seek)(void *ctx, fs_file_t *file, long offset, int whence);
    int  (*stat)(void *ctx, const char *path, /* fs_stat_t* */ void *out_stat);
    void *ctx;
} ifilesystem_t;
```

### 2.4 `FatFsFileSystem` — Adapter Implementing `IFileSystem`

```c
// fatfs_filesystem.c
static int ff_mount(void *ctx, const char *path) {
    FATFS *fs = ctx;
    FRESULT r = f_mount(fs, path, 1);
    return (r == FR_OK) ? 0 : -1;
}

static int ff_open(void *ctx, fs_file_t **out, const char *path, int flags) {
    FIL *fil = malloc(sizeof(FIL));
    FRESULT r = f_open(fil, path, translate_flags(flags));
    if (r != FR_OK) { free(fil); return -1; }
    *out = (fs_file_t *)fil;
    return 0;
}
// ... ff_read wraps f_read, ff_write wraps f_write, ff_seek wraps f_lseek, ff_close wraps f_close

const ifilesystem_t fatfs_filesystem_vtable = {
    .mount = ff_mount, .open = ff_open, .read = ff_read,
    .write = ff_write, .seek = ff_seek, .close = ff_close, /* ... */
};
```

The application calls `fs->open(fs->ctx, &file, "song.wav", FS_READ)` — it never sees `FIL`,
`FRESULT`, or `f_open` directly.

---

## 3. Wiring It Together at Startup

```c
// main.c
block_device_t sd_bd;
sd_spi_block_device_init(&sd_bd, spi0, PIN_MISO, PIN_MOSI, PIN_SCK, PIN_CS);
diskio_register(0, &sd_bd);              // FatFs drive 0 <- SD-over-SPI

static FATFS fatfs_instance;
ifilesystem_t fs = fatfs_filesystem_vtable;
fs.ctx = &fatfs_instance;
fs.mount(fs.ctx, "0:");

fs_file_t *wav;
fs.open(fs.ctx, &wav, "0:/audio/track.wav", FS_READ);
```

To switch storage hardware later, replace `sd_spi_block_device_init` with
`qspi_flash_block_device_init` — nothing above `IBlockDevice` changes. To switch filesystems,
replace `fatfs_filesystem_vtable` with a `littlefs_filesystem_vtable` — nothing in `main.c`'s
calls to `fs.open/read/write` changes.

---

## 4. Recommended Project Layout

```
/lib
  /fatfs                 <- unmodified upstream ff.c, ff.h, ffconf.h, diskio.h
  /block_device
    block_device.h        <- IBlockDevice interface
    sd_spi_block_device.c  <- Pico SPI + SD card driver
    sd_sdio_block_device.c <- (optional) Pico PIO-based SDIO driver
    qspi_flash_block_device.c <- (optional) onboard flash driver
  /diskio
    diskio.c              <- FatFs <-> IBlockDevice glue (registers drives)
  /filesystem
    ifilesystem.h          <- IFileSystem interface
    fatfs_filesystem.c     <- FatFs adapter implementing IFileSystem
/src
  main.c                  <- application code, depends only on ifilesystem.h
```

---

## 5. Implementation Steps Checklist

1. Vendor unmodified FatFs source (`ff.c`, `ff.h`, `ffconf.h`, `diskio.h`) from elm-chan.org into
   `/lib/fatfs`. Configure `ffconf.h` (LFN, volume count `FF_VOLUMES`, sector size).
2. Define `IBlockDevice` in `block_device.h`.
3. Implement one concrete block device (start with SD-over-SPI using Pico SDK `hardware_spi`):
   handle the SD SPI init sequence (CMD0/CMD8/ACMD41/CMD58), block read (CMD17/CMD18), block write
   (CMD24/CMD25).
4. Implement `diskio.c` as a thin registry/forwarder from FatFs drive numbers to `IBlockDevice*`.
5. Implement `get_fattime()` (tie to Pico RTC/`aon_timer`, or return a constant if no RTC needed).
6. Define `IFileSystem` in `ifilesystem.h`.
7. Implement `fatfs_filesystem.c` adapting `IFileSystem` calls to FatFs API calls.
8. Wire everything in `main.c`: create block device → `diskio_register` → `f_mount` via the
   adapter → application uses `IFileSystem` only.
9. Add CMake targets: compile `ff.c`, `diskio.c`, chosen block device driver, and the adapter into
   a static library; link Pico SDK's `hardware_spi`, `hardware_gpio` (and `hardware_flash` if
   using onboard flash).
10. Validate: write a small file with a PC SD card reader, mount on Pico, `read`/verify contents
    through the `IFileSystem` API only (no direct `f_*` calls in test code).

---

## 6. Benefits of This Architecture

- **Hardware independence**: swap SD-over-SPI ↔ SDIO ↔ QSPI flash by writing a new
  `IBlockDevice` implementation; `diskio.c`, FatFs core, and the app are untouched.
- **Filesystem independence**: swap FatFs ↔ LittleFS ↔ a mock/RAM filesystem for unit tests by
  writing a new `IFileSystem` adapter; the application and hardware layers are untouched.
- **Testability**: the application can be unit-tested on a PC against a mock `IFileSystem`
  without any Pico hardware or FatFs at all.
- **Unmodified upstream FatFs**: since all glue lives in `diskio.c` and the adapter, you can drop
  in newer FatFs releases without merge conflicts.
