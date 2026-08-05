/**
 * @file diskio.cpp
 * @brief FatFs <-> IBlockDevice glue layer.
 *
 * This is the ONLY file that bridges FatFs to hardware. It implements the disk_* functions
 * that ff.c calls (declared in lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src/ff15/source/diskio.h)
 * by forwarding each call to whichever IBlockDevice is registered for the requested physical
 * drive number (see IBlockDevice::RegisterBlockDevice). FatFs core and IBlockDevice
 * implementations never reference each other directly, so either side can be swapped
 * independently.
 *
 * This replaces the upstream diskio.c skeleton shipped in the ff15 submodule; that file is
 * left untouched and must be excluded from the build in favor of this one.
 */

#include "../include/IBlockDevice.h"
#include "diskio.h"

using filesystem::BlockDeviceIoctl;
using filesystem::BlockDeviceResult;
using filesystem::GetBlockDevice;
using filesystem::IBlockDevice;

namespace
{
    DRESULT ToDresult(BlockDeviceResult result)
    {
        switch (result)
        {
        case BlockDeviceResult::kOk:
            return RES_OK;
        case BlockDeviceResult::kWriteProtected:
            return RES_WRPRT;
        case BlockDeviceResult::kNotReady:
            return RES_NOTRDY;
        case BlockDeviceResult::kInvalidParameter:
            return RES_PARERR;
        case BlockDeviceResult::kError:
        default:
            return RES_ERROR;
        }
    }
} // namespace

// FatFs's ff.c calls these with C linkage; they must match diskio.h's prototypes exactly.
extern "C"
{

    DSTATUS disk_status(BYTE pdrv)
    {
        IBlockDevice *device = GetBlockDevice(pdrv);
        if (device == nullptr)
        {
            return STA_NOINIT;
        }
        return static_cast<DSTATUS>(device->Status());
    }

    DSTATUS disk_initialize(BYTE pdrv)
    {
        IBlockDevice *device = GetBlockDevice(pdrv);
        if (device == nullptr)
        {
            return STA_NOINIT;
        }
        return (device->Init() == BlockDeviceResult::kOk) ? 0 : STA_NOINIT;
    }

    DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
    {
        IBlockDevice *device = GetBlockDevice(pdrv);
        if (device == nullptr)
        {
            return RES_NOTRDY;
        }
        return ToDresult(device->Read(buff, static_cast<uint32_t>(sector), count));
    }

#if FF_FS_READONLY == 0
    DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
    {
        IBlockDevice *device = GetBlockDevice(pdrv);
        if (device == nullptr)
        {
            return RES_NOTRDY;
        }
        return ToDresult(device->Write(buff, static_cast<uint32_t>(sector), count));
    }
#endif

    DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
    {
        IBlockDevice *device = GetBlockDevice(pdrv);
        if (device == nullptr)
        {
            return RES_NOTRDY;
        }

        BlockDeviceIoctl ioctl;
        switch (cmd)
        {
        case CTRL_SYNC:
            ioctl = BlockDeviceIoctl::kCtrlSync;
            break;
        case GET_SECTOR_COUNT:
            ioctl = BlockDeviceIoctl::kGetSectorCount;
            break;
        case GET_SECTOR_SIZE:
            ioctl = BlockDeviceIoctl::kGetSectorSize;
            break;
        case GET_BLOCK_SIZE:
            ioctl = BlockDeviceIoctl::kGetBlockSize;
            break;
        case CTRL_TRIM:
            ioctl = BlockDeviceIoctl::kCtrlTrim;
            break;
        default:
            return RES_PARERR;
        }

        return ToDresult(device->Ioctl(ioctl, buff));
    }

    // FatFs core (ff.c) requires this for file/dir timestamps when FF_FS_NORTC == 0.
    // Wire this up to a real RTC (e.g. Pico's aon_timer) when one is available; until then
    // it returns a fixed, valid FAT timestamp so f_open/f_mkdir etc. don't fail to link or run.
    DWORD get_fattime(void)
    {
        // Bit layout expected by FatFs: bits31:25=Year-1980, 24:21=Month, 20:16=Day,
        // 15:11=Hour, 10:5=Minute, 4:0=Second/2.
        return ((DWORD)(2024 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
    }

} // extern "C"
