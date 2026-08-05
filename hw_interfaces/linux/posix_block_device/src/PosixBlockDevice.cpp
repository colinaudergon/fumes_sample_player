/**
 * @file PosixBlockDevice.cpp
 * @brief IBlockDevice implementation backed by a regular file, for native/Linux builds.
 */

#include "PosixBlockDevice.h"

#include <vector>

namespace hw_interface
{
    using filesystem::BlockDeviceIoctl;
    using filesystem::BlockDeviceResult;
    namespace BlockDeviceStatus = filesystem::BlockDeviceStatus;

    PosixBlockDevice::PosixBlockDevice(std::string image_path, uint32_t sector_size, uint32_t sector_count)
        : image_path_(std::move(image_path)), sector_size_(sector_size), sector_count_(sector_count)
    {
    }

    PosixBlockDevice::~PosixBlockDevice()
    {
        if (file_ != nullptr)
        {
            std::fclose(file_);
        }
    }

    BlockDeviceResult PosixBlockDevice::Init()
    {
        if (file_ != nullptr)
        {
            return BlockDeviceResult::kOk;
        }

        // Open for update if the image already exists, otherwise create and zero-fill it so
        // FatFs sees a fixed-size "disk" of sector_count_ * sector_size_ bytes.
        file_ = std::fopen(image_path_.c_str(), "r+b");
        if (file_ == nullptr)
        {
            file_ = std::fopen(image_path_.c_str(), "w+b");
            if (file_ == nullptr)
            {
                return BlockDeviceResult::kError;
            }

            const std::vector<uint8_t> zeros(sector_size_, 0);
            for (uint32_t sector = 0; sector < sector_count_; ++sector)
            {
                if (std::fwrite(zeros.data(), 1, zeros.size(), file_) != zeros.size())
                {
                    std::fclose(file_);
                    file_ = nullptr;
                    return BlockDeviceResult::kError;
                }
            }
            std::fflush(file_);
        }

        return BlockDeviceResult::kOk;
    }

    uint8_t PosixBlockDevice::Status()
    {
        return (file_ != nullptr) ? BlockDeviceStatus::kOk : BlockDeviceStatus::kNoInit;
    }

    BlockDeviceResult PosixBlockDevice::Read(uint8_t *buffer, uint32_t sector, uint32_t count)
    {
        if (file_ == nullptr)
        {
            return BlockDeviceResult::kNotReady;
        }

        if (std::fseek(file_, static_cast<long>(sector) * sector_size_, SEEK_SET) != 0)
        {
            return BlockDeviceResult::kError;
        }

        const size_t bytes = static_cast<size_t>(count) * sector_size_;
        return (std::fread(buffer, 1, bytes, file_) == bytes) ? BlockDeviceResult::kOk : BlockDeviceResult::kError;
    }

    BlockDeviceResult PosixBlockDevice::Write(const uint8_t *buffer, uint32_t sector, uint32_t count)
    {
        if (file_ == nullptr)
        {
            return BlockDeviceResult::kNotReady;
        }

        if (std::fseek(file_, static_cast<long>(sector) * sector_size_, SEEK_SET) != 0)
        {
            return BlockDeviceResult::kError;
        }

        const size_t bytes = static_cast<size_t>(count) * sector_size_;
        if (std::fwrite(buffer, 1, bytes, file_) != bytes)
        {
            return BlockDeviceResult::kError;
        }
        std::fflush(file_);
        return BlockDeviceResult::kOk;
    }

    BlockDeviceResult PosixBlockDevice::Ioctl(BlockDeviceIoctl cmd, void *data)
    {
        if (file_ == nullptr)
        {
            return BlockDeviceResult::kNotReady;
        }

        switch (cmd)
        {
        case BlockDeviceIoctl::kCtrlSync:
            std::fflush(file_);
            return BlockDeviceResult::kOk;
        case BlockDeviceIoctl::kGetSectorCount:
            *static_cast<uint32_t *>(data) = sector_count_;
            return BlockDeviceResult::kOk;
        case BlockDeviceIoctl::kGetSectorSize:
            *static_cast<uint16_t *>(data) = static_cast<uint16_t>(sector_size_);
            return BlockDeviceResult::kOk;
        case BlockDeviceIoctl::kGetBlockSize:
            *static_cast<uint32_t *>(data) = 1;
            return BlockDeviceResult::kOk;
        case BlockDeviceIoctl::kCtrlTrim:
            return BlockDeviceResult::kOk;
        default:
            return BlockDeviceResult::kInvalidParameter;
        }
    }

} // namespace hw_interface
