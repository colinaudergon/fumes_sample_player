/**
 * @file SdBlockDevice.cpp
 * @brief IBlockDevice implementation backed by the vendored
 * carlk3/no-OS-FatFS-SD-SPI-RPi-Pico SD-over-SPI driver (see hw_config.c for the pin/SPI
 * configuration this wraps, and CMakeLists.txt for how the vendored driver subset is built as
 * SdSpiDriver).
 *
 * This is a thin adapter only: all SD/SPI protocol handling lives in the vendored sd_card_t
 * (sd_card.h/.c, sd_spi.c, spi.c, crc.c). This file just translates between that driver's C API
 * and filesystem::IBlockDevice, the same role SdBlockDevice always had.
 */
#include "SdBlockDevice.h"

#include "hw_config.h"
#include "sd_card.h"

namespace
{
    // This project only configures/uses drive 0 (see hw_config.c: sd_get_num() == 1); if more
    // drives are added later, SdBlockDevice would need to know which pdrv/sd_card_t index it
    // owns instead of hardcoding 0 here.
    constexpr size_t kSdCardIndex = 0;

    sd_card_t *GetSdCard()
    {
        return sd_get_by_num(kSdCardIndex);
    }
} // namespace

filesystem::BlockDeviceResult filesystem::SdBlockDevice::Init()
{
    if (!sd_init_driver())
    {
        return BlockDeviceResult::kError;
    }

    sd_card_t *sd_card = GetSdCard();
    if (sd_card == nullptr)
    {
        return BlockDeviceResult::kNotReady;
    }

    return (sd_card->init(sd_card) == 0) ? BlockDeviceResult::kOk : BlockDeviceResult::kNotReady;
}

uint8_t filesystem::SdBlockDevice::Status()
{
    sd_card_t *sd_card = GetSdCard();
    if (sd_card == nullptr)
    {
        return BlockDeviceStatus::kNoInit;
    }

    uint8_t status = static_cast<uint8_t>(sd_card->m_Status);

    if (sd_card->use_card_detect && !sd_card_detect(sd_card))
    {
        status |= (BlockDeviceStatus::kNoDisk | BlockDeviceStatus::kNoInit);
    }

    return status;
}

filesystem::BlockDeviceResult filesystem::SdBlockDevice::Read(uint8_t *buffer, uint32_t sector, uint32_t count)
{
    sd_card_t *sd_card = GetSdCard();
    if (sd_card == nullptr)
    {
        return BlockDeviceResult::kNotReady;
    }

    return (sd_card->read_blocks(sd_card, buffer, sector, count) == 0) ? BlockDeviceResult::kOk
                                                                        : BlockDeviceResult::kError;
}

filesystem::BlockDeviceResult filesystem::SdBlockDevice::Write(const uint8_t *buffer, uint32_t sector, uint32_t count)
{
    sd_card_t *sd_card = GetSdCard();
    if (sd_card == nullptr)
    {
        return BlockDeviceResult::kNotReady;
    }

    return (sd_card->write_blocks(sd_card, buffer, sector, count) == 0) ? BlockDeviceResult::kOk
                                                                         : BlockDeviceResult::kError;
}

filesystem::BlockDeviceResult filesystem::SdBlockDevice::Ioctl(filesystem::BlockDeviceIoctl cmd, void *data)
{
    sd_card_t *sd_card = GetSdCard();
    if (sd_card == nullptr)
    {
        return BlockDeviceResult::kNotReady;
    }

    // Matches sd_block_size (512 bytes), the only block size the vendored driver supports, and
    // FF_MIN_SS/FF_MAX_SS (both 512) already configured in lib/FatFsCore/ff15/source/ffconf.h.
    constexpr uint16_t kSectorSizeBytes = 512;

    switch (cmd)
    {
    case BlockDeviceIoctl::kCtrlSync:
        // SPI transfers issued by the vendored driver are synchronous/blocking already, so
        // there is nothing to flush here.
        return BlockDeviceResult::kOk;

    case BlockDeviceIoctl::kGetSectorCount:
        if (data == nullptr)
        {
            return BlockDeviceResult::kInvalidParameter;
        }
        *static_cast<uint32_t *>(data) = static_cast<uint32_t>(sd_sectors(sd_card));
        return BlockDeviceResult::kOk;

    case BlockDeviceIoctl::kGetSectorSize:
        if (data == nullptr)
        {
            return BlockDeviceResult::kInvalidParameter;
        }
        *static_cast<uint16_t *>(data) = kSectorSizeBytes;
        return BlockDeviceResult::kOk;

    case BlockDeviceIoctl::kGetBlockSize:
        if (data == nullptr)
        {
            return BlockDeviceResult::kInvalidParameter;
        }
        // The vendored driver has no concept of an SD erase-block size distinct from the
        // sector size, so report 1 sector per "block" (FatFs treats this as advisory, used by
        // f_mkfs() for allocation-unit alignment).
        *static_cast<uint32_t *>(data) = 1;
        return BlockDeviceResult::kOk;

    case BlockDeviceIoctl::kCtrlTrim:
        // Not implemented: the vendored driver does not issue SD erase (CMD32/33/38) commands.
        return BlockDeviceResult::kError;

    default:
        return BlockDeviceResult::kInvalidParameter;
    }
}
