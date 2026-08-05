/**
 * @file SdBlockDevice.h
 * @brief IBlockDevice implementation backed by a sd card communication.
 */
#pragma once

#include "IBlockDevice.h"
namespace filesystem
{
    class SdBlockDevice : public IBlockDevice
    {
    public:
        SdBlockDevice() = default;
        ~SdBlockDevice() override = default;

        /**
         * @brief Initializes the SD card and puts it in a state ready to accept Read()/Write().
         * @return BlockDeviceResult::kOk on success, or an error code describing why the card
         * could not be initialized (e.g. kError, kNotReady).
         */
        BlockDeviceResult Init() override;

        /**
         * @brief Reports the current status of the SD card.
         * @return A bitmask of BlockDeviceStatus values (see IBlockDevice.h), e.g. kNoInit if
         * Init() has not (yet) succeeded, kNoDisk if no card is present, kWriteProtected if the
         * card's write-protect switch is engaged.
         */
        uint8_t Status() override;

        /**
         * @brief Reads one or more consecutive sectors from the SD card.
         * @param buffer Destination buffer, must be at least `count * sector size` bytes.
         * @param sector Zero-based index of the first sector to read.
         * @param count Number of consecutive sectors to read.
         * @return BlockDeviceResult::kOk on success, or an error code on failure.
         */
        BlockDeviceResult Read(uint8_t *buffer, uint32_t sector, uint32_t count) override;

        /**
         * @brief Writes one or more consecutive sectors to the SD card.
         * @param buffer Source buffer, must contain at least `count * sector size` bytes.
         * @param sector Zero-based index of the first sector to write.
         * @param count Number of consecutive sectors to write.
         * @return BlockDeviceResult::kOk on success, or an error code on failure (e.g.
         * kWriteProtected if the card is write-protected).
         */
        BlockDeviceResult Write(const uint8_t *buffer, uint32_t sector, uint32_t count) override;

        /**
         * @brief Performs a miscellaneous control operation on the SD card (see
         * BlockDeviceIoctl in IBlockDevice.h).
         * @param cmd The control command to execute (e.g. kCtrlSync, kGetSectorCount,
         * kGetSectorSize, kGetBlockSize, kCtrlTrim).
         * @param data Command-specific argument/output buffer; its expected type depends on
         * `cmd` (e.g. a uint32_t* for kGetSectorCount, a uint16_t* for kGetSectorSize).
         * @return BlockDeviceResult::kOk on success, or an error code on failure.
         */
        BlockDeviceResult Ioctl(filesystem::BlockDeviceIoctl cmd, void *data) override;
    };
} // namespace filesystem
