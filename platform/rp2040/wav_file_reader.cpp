/**
 * @file wav_file_reader.cpp
 * @brief RP2040/Pico entry point.
 *
 * Mirrors platform/linux/main.cpp's storage-mounting setup, but wired against the RP2040
 * hardware implementations (SdBlockDevice) instead of the native/Linux ones
 * (hw_interfaces/linux/PosixBlockDevice), using the same hardware-agnostic app-layer classes
 * (IFileSystem, FileManager) either platform build shares.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

static constexpr uint kBlinkLedGpio = 0;
static constexpr uint32_t kBlinkPeriodMs = 500;

#include "FileManager.h"
#include "IBlockDevice.h"
#include "IFileSystem.h"
#include "SdBlockDevice.h"

int main()
{
    stdio_init_all();

    gpio_init(kBlinkLedGpio);
    gpio_set_dir(kBlinkLedGpio, GPIO_OUT);
    sleep_ms(kBlinkPeriodMs);
    // Register and initialize the SD-over-SPI storage backend (app/FileSystem/hw_layer/
    // SdInterface/, wrapping the vendored carlk3/no-OS-FatFS-SD-SPI-RPi-Pico driver) as FatFs
    // drive 0, so diskio.cpp's disk_* functions have a working IBlockDevice to forward to.
    static filesystem::SdBlockDevice sd_block_device;
    filesystem::RegisterBlockDevice(0, &sd_block_device);
    sd_block_device.Init();

    // Own the app-wide singleton IFileSystem instance (see app::GetFileSystem() in
    // IFileSystem.h), same as platform/linux/main.cpp.
    app::IFileSystem &file_system = app::GetFileSystem();
    file_system.Init();

    const char *disk_path = "0:";
    app::FsResult mount_result = file_system.Mount(disk_path);
    if (mount_result != app::FsResult::kOk)
    {
        printf("Mount result: %d\n", static_cast<int>(mount_result));
    }
    else
    {
        // FileManager::Init() counts the top-level "bank_<N>" folders on the disk (see
        // FileManager::CountBanksOnDisk()/ValidateBankName()); these are the "folders" on the
        // SD card that this firmware treats as sample banks.
        app::filesystem::FileManager file_manager;
        int file_manager_init_result = file_manager.Init(disk_path, static_cast<uint8_t>(app::filesystem::SupportedFileExtensions::KWav));
        
        if (file_manager_init_result < 0)
        {
            printf("Failed to initialize file manager: %d\n",file_manager_init_result);
        }
        else
        {
            printf("Number of banks: %zu\n", file_manager.GetNumberOfBanks());
        }
    }

    while (true) {
        gpio_put(kBlinkLedGpio, true);
        sleep_ms(kBlinkPeriodMs);
        gpio_put(kBlinkLedGpio, false);
        sleep_ms(kBlinkPeriodMs);
    }
}

