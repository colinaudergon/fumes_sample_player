/**
 * @file PosixBlockDevice.h
 * @brief IBlockDevice implementation backed by a regular file, for native/Linux builds.
 *
 * Lets the FatFs stack run against a disk-image file (e.g. "disk.img") on a host Linux
 * system, exactly the way it runs against a real SD/USB device on the RP2040 target: this
 * class only implements filesystem::IBlockDevice, so nothing above it (diskio.cpp, FatFs,
 * FatFsFileSystemAdapter, IFileSystem) needs to know it isn't real hardware.
 */

#pragma once

#include <cstdio>
#include <string>

#include "IBlockDevice.h"

namespace hw_interface
{

    class PosixBlockDevice : public filesystem::IBlockDevice
    {
    public:
        // sector_size/sector_count describe the geometry FatFs will see; image_path is
        // created (and zero-filled to sector_size * sector_count bytes) on first Init() if it
        // does not already exist.
        PosixBlockDevice(std::string image_path, uint32_t sector_size = 512, uint32_t sector_count = 65536);
        ~PosixBlockDevice() override;

        filesystem::BlockDeviceResult Init() override;
        uint8_t Status() override;

        filesystem::BlockDeviceResult Read(uint8_t *buffer, uint32_t sector, uint32_t count) override;
        filesystem::BlockDeviceResult Write(const uint8_t *buffer, uint32_t sector, uint32_t count) override;
        filesystem::BlockDeviceResult Ioctl(filesystem::BlockDeviceIoctl cmd, void *data) override;

    private:
        std::string image_path_;
        uint32_t sector_size_;
        uint32_t sector_count_;
        std::FILE *file_ = nullptr;
    };

} // namespace hw_interface
