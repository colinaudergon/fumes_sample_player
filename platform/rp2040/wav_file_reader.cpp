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

#include "FileManager.h"
#include "IBlockDevice.h"
#include "IFileSystem.h"
#include "SdBlockDevice.h"

int main()
{
    stdio_init_all();

    // Register and initialize the SD-over-SPI storage backend (app/FileSystem/hw_layer/
    // SdInterface/, wrapping the vendored carlk3/no-OS-FatFS-SD-SPI-RPi-Pico driver) as FatFs
    // drive 0, so diskio.cpp's disk_* functions have a working IBlockDevice to forward to.
    // NOTE: hw_config.c currently configures a PLACEHOLDER pinout, not real hardware wiring.
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
        if (file_manager.Init(disk_path, static_cast<uint8_t>(app::filesystem::SupportedFileExtensions::KWav)) < 0)
        {
            printf("Failed to initialize file manager\n");
        }
        else
        {
            printf("Number of banks: %zu\n", file_manager.GetNumberOfBanks());
        }
    }

    while (true) {
        sleep_ms(1000);
    }
}

