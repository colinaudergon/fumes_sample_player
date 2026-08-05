# wav_file_reader

A WAV file player built around a hardware-agnostic app layer (`AudioPlayer`, `FileSystem`,
`FatFsCore`) that talks to hardware only through interfaces (`IAudioCodec`, `IBlockDevice`,
`IFileSystem`). Concrete implementations of those interfaces are swapped per platform, which
lets the same application code build for either:

- the **Raspberry Pi Pico / RP2040** (real hardware), or
- a **native host build** (Linux, incl. WSL) for fast iteration, testing, and debugging
  without any board attached.

See [`FATFS_PICO_PORTING.md`](FATFS_PICO_PORTING.md) for details on the layered architecture.

## Repository layout

```
app/                    Hardware-agnostic application layer
  AudioPlayer/           Audio playback logic (depends only on IAudioCodec + IFileSystem)
  FileSystem/            FatFs <-> IBlockDevice glue + IFileSystem adapter
  include/               IFileSystem interface
hw_interfaces/          Interfaces + concrete hardware implementations
  include/               IAudioCodec, IStorageController interfaces
  pico_adc/              RP2040 ADC implementation
  i2s_audio_codec/       RP2040 I2S audio codec implementation
  linux/                 Native/Linux stub implementations (PosixBlockDevice, NullAudioCodec)
lib/                    Vendored/third-party libraries (FatFsCore, pico_audio_i2s_32b)
platform/
  rp2040/                Pico/RP2040 build entry point (CMakeLists.txt, pico_sdk_import.cmake,
                         wav_file_reader.cpp)
  linux/                 Native/Linux build entry point (main.cpp)
CMakeLists.txt          Native/Linux build entry point (root of the repo)
```

## Building

There are **two separate build entry points**, one per platform. They cannot be configured in
the same CMake run (the Pico SDK toolchain must be selected before the first `project()` call,
which conflicts with a native host build), so pick the one you need and configure it into its
own build directory.

| Platform                     | Configure from        | Typical build dir |
|-------------------------------|------------------------|--------------------|
| Native host (Linux / WSL)     | repository root (`CMakeLists.txt`) | `build/` |
| Raspberry Pi Pico / RP2040    | `platform/rp2040`      | `build-rp2040/`    |

### Option A — Native build (Linux / WSL)

Builds `wav_file_reader_native`, linking the shared app layer against the `hw_interfaces/linux`
implementations (a file-backed `IBlockDevice` and a no-op `IAudioCodec`). Useful for building
and running on a regular Linux machine or under WSL, without any Pico hardware.

Requirements: CMake >= 3.13, a C/C++ compiler (gcc/g++ or clang), and `make` or `ninja`.

```bash
# From the repository root
cmake -S . -B build
cmake --build build

# Run it (creates/uses disk.img in the current directory as the backing FAT volume)
./build/wav_file_reader_native
```

The native build also produces `make_disk_image`, a CLI utility that packs a folder's contents
into a FAT disk-image file, so you can populate `disk.img` with real WAV files (or anything
else) from the host filesystem before running the emulator:

```bash
# Pack ./my_wav_files into disk.img (32 MiB by default; pass a sector count as a 3rd arg for a
# larger image, e.g. `./build/make_disk_image my_wav_files disk.img 262144` for 128 MiB)
./build/make_disk_image my_wav_files disk.img

./build/wav_file_reader_native
```

`make_disk_image` goes through the same `PosixBlockDevice`/`FatFsFileSystemAdapter` stack as
the app itself, so the resulting image is guaranteed to mount correctly. Note: FatFs is built
with long-file-name support disabled (`FF_USE_LFN == 0`, see
`lib/FatFsCore/ff15/source/ffconf.h`), so only 8.3 short filenames are supported — rename any
files/folders with longer names before packing them.

### Option B — Raspberry Pi Pico / RP2040 build

Builds the `wav_file_reader` firmware image (`.uf2`, `.elf`, etc.) using the Pico SDK. This is
the original board target; configure it by pointing CMake at `platform/rp2040` instead of the
repository root.

Requirements: [Pico SDK](https://github.com/raspberrypi/pico-sdk) (set `PICO_SDK_PATH` or let it
fetch from git), the `arm-none-eabi` toolchain, CMake >= 3.13, and `make` or `ninja`.

```bash
# From the repository root
cmake -S platform/rp2040 -B build-rp2040
cmake --build build-rp2040

# Flash build-rp2040/wav_file_reader.uf2 to the Pico (e.g. via BOOTSEL mass-storage mode)
```

`PICO_BOARD` defaults to `pico`; override it with `-DPICO_BOARD=<board>` if targeting a
different board (e.g. `pico2`, `pico_w`).

### Choosing which one to use

- Use the **native build** while developing/testing app-layer logic (`AudioPlayer`,
  `FileSystem`, FatFs glue) — it's fast to iterate on and doesn't require hardware.
- Use the **RP2040 build** to produce the actual firmware that runs on the Pico.

Because both targets build the same `app/` and `lib/FatFsCore` sources unmodified — only the
`hw_interfaces/*` implementation and the platform entry point differ — behavior verified on the
native build should carry over directly to the RP2040 build.
