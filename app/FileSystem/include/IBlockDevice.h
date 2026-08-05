/**
 * @file IBlockDevice.h
 * @brief Hardware-agnostic block device interface used by the FatFs diskio glue layer.
 *
 * Concrete drivers (SD-over-SPI, SDIO, QSPI flash, RAM disk, ...) implement this interface.
 * diskio.cpp is the only file that talks to IBlockDevice on FatFs's behalf, so swapping the
 * storage hardware never requires changes to FatFs core or to diskio.cpp itself.
 */

#pragma once

#include <cstdint>

namespace filesystem
{
    /* Bitmask values mirroring FatFs's DSTATUS (see diskio.h), kept independent of FatFs headers. */
    namespace BlockDeviceStatus
    {
        constexpr uint8_t kOk = 0x00;
        constexpr uint8_t kNoInit = 0x01;
        constexpr uint8_t kNoDisk = 0x02;
        constexpr uint8_t kWriteProtected = 0x04;
    } // namespace BlockDeviceStatus

    enum class BlockDeviceResult
    {
        kOk,
        kError,
        kWriteProtected,
        kNotReady,
        kInvalidParameter
    };

    /* Mirrors the subset of FatFs's disk_ioctl command codes (see diskio.h) that IBlockDevice
     * implementations need to support, without depending on FatFs headers. */
    enum class BlockDeviceIoctl
    {
        kCtrlSync,
        kGetSectorCount,
        kGetSectorSize,
        kGetBlockSize,
        kCtrlTrim
    };

    class IBlockDevice
    {
    public:
        virtual ~IBlockDevice() = default;

        virtual BlockDeviceResult Init() = 0;
        virtual uint8_t Status() = 0; // bitmask of BlockDeviceStatus values

        virtual BlockDeviceResult Read(uint8_t *buffer, uint32_t sector, uint32_t count) = 0;
        virtual BlockDeviceResult Write(const uint8_t *buffer, uint32_t sector, uint32_t count) = 0;
        virtual BlockDeviceResult Ioctl(BlockDeviceIoctl cmd, void *data) = 0;
    };

    constexpr uint8_t kMaxBlockDevices = 4; // matches FF_VOLUMES in !ffconf.h

    /**
     * @brief Bind an IBlockDevice instance to a FatFs physical drive number (0..kMaxBlockDevices-1).
     *
     * Called once at startup for each mounted drive, e.g.:
     *   filesystem::RegisterBlockDevice(0, &sdSpiBlockDevice);
     *
     * diskio.cpp's disk_* functions forward FatFs's requests for drive `pdrv` to whichever
     * device was registered here.
     */
    void RegisterBlockDevice(uint8_t pdrv, IBlockDevice *device);

    /// Returns the IBlockDevice registered for `pdrv`, or nullptr if none is registered.
    IBlockDevice *GetBlockDevice(uint8_t pdrv);

} // namespace filesystem
