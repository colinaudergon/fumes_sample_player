#include "SdBlockDevice.h"
/**
 * @file SdBlockDevice.cpp
 * @brief IBlockDevice implementation backed by a sd card communication.
 */

BlockDeviceResult filesystem::SdBlockDevice::Init()
{
    return BlockDeviceResult();
}

uint8_t filesystem::SdBlockDevice::Status()
{
    return 0;
}

/*
TODO: Implement following methods
*/

BlockDeviceResult filesystem::SdBlockDevice::Read(uint8_t *buffer, uint32_t sector, uint32_t count)
{
    return BlockDeviceResult();
}

BlockDeviceResult filesystem::SdBlockDevice::Write(const uint8_t *buffer, uint32_t sector, uint32_t count)
{
    return BlockDeviceResult();
}

BlockDeviceResult filesystem::SdBlockDevice::Ioctl(filesystem::BlockDeviceIoctl cmd, void *data)
{
    return BlockDeviceResult();
}
