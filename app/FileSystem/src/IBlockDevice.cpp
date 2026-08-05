/**
 * @file IBlockDevice.cpp
 * @brief Registry binding IBlockDevice instances to FatFs physical drive numbers.
 */

#include "../include/IBlockDevice.h"

namespace filesystem
{
    namespace
    {
        IBlockDevice *g_devices[kMaxBlockDevices] = {nullptr, nullptr, nullptr, nullptr};
    }

    void RegisterBlockDevice(uint8_t pdrv, IBlockDevice *device)
    {
        if (pdrv < kMaxBlockDevices)
        {
            g_devices[pdrv] = device;
        }
    }

    IBlockDevice *GetBlockDevice(uint8_t pdrv)
    {
        return (pdrv < kMaxBlockDevices) ? g_devices[pdrv] : nullptr;
    }

} // namespace filesystem
