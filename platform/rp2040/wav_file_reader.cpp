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
#include "hardware/clocks.h"

static constexpr uint kBlinkLedGpio = 0;
static constexpr uint32_t kBlinkPeriodMs = 500;

#include "FileManager.h"
#include "IBlockDevice.h"
#include "IFileSystem.h"
#include "SdBlockDevice.h"
#include "AudioPlayer.h"
#include "pwm_audio_codec.h"
#include "EffectController.h"

app::audio::AudioPlayer audio_player;
hw_interface::PicoAudioCodec audio_codec;
filesystem::SdBlockDevice sd_block_device;
app::filesystem::FileManager file_manager;
app::audio::EffectController effect_controller;

void InitUI();
int InitFileSystem();
int InitAudioSystem();
void buffer_callback(hw_interface::audio_buffer_t *buffer_0, hw_interface::audio_buffer_t *buffer_1)
{
    // buffer_0 is always the buffer to be consumed for this callback ("current"); buffer_1 is
    // read-ahead scratch space managed internally by the codec and isn't touched here.
    (void)buffer_1;
    hw_interface::audio_buffer_t &current = *buffer_0;
    app::audio::wav::audio_frame_t frame{current.buffer_left, current.buffer_right, current.buffer_len};
    audio_player.Read(frame, current.buffer_len);
    effect_controller.Process(frame, frame, current.buffer_len);
}

int main()
{

    stdio_init_all();
    set_sys_clock_khz(176000, true);
    InitUI();

    int res = InitFileSystem();
    if (res < 0)
    {
        printf("Failed to init the file system:%d\n", res);
    }

    file_manager.SelectNextFile();
    const char *file_path = file_manager.GetSelectedFilePath();

    if (file_path[0] != '\0')
    {
        int load_result = audio_player.LoadFile(file_path);
        if (load_result == 0)
        {
            // Started before InitAudioSystem() (which calls audio_codec.Start() internally):
            // PicoAudioCodec::Start() synchronously primes both buffer halves via the
            // registered fill_cb_ (buffer_callback -> audio_player.Read()) before returning.
            // If is_playing_ were still false at that point, Read() would fall into its
            // "nothing loaded/playing" branch and fill both priming buffers with silence.
            audio_player.Start();

            res = InitAudioSystem();
            if (res < 0)
            {
                printf("Failed to init the audio system:%d\n", res);
            }
            printf("File loaded: %s\n", file_path);
        }
        else
        {
            // LoadFile() failed internally (bad header, unsupported/non-canonical chunk
            // layout, short read, etc.) -- audio_player.file_ stays null, so Read() would
            // silently fall into its FillWithZeros() branch forever (permanent silence) if we
            // called Start() anyway. Surface the real failure instead of proceeding.
            printf("Failed to load file %s: %d\n", file_path, load_result);
        }
    }
    else
    {
        printf("Failed to load file\n");
    }

    uint32_t last_blink_ms = to_ms_since_boot(get_absolute_time());
    bool led_on = false;

    while (true)
    {
        // Serviced here (main-loop context), NOT from the DMA IRQ: buffer_callback ultimately
        // calls into AudioPlayer::Read() -> the SD card file system, which can block for a
        // while (mutex-guarded SPI DMA transfer) -- unsafe to run directly inside a hardware
        // interrupt handler. PicoAudioCodec's IRQ only swaps buffers and flags that a refill is
        // needed; polling this every iteration (instead of blocking in sleep_ms() below) is what
        // actually keeps audio fed.
        audio_codec.ServiceRefill();

        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - last_blink_ms >= kBlinkPeriodMs)
        {
            led_on = !led_on;
            gpio_put(kBlinkLedGpio, led_on);
            last_blink_ms = now_ms;

            // Direct proof (or disproof) of main-loop buffer starvation: each increment means
            // ServiceRefill() didn't run in time and the DMA replayed a stale buffer -- an
            // audible click/stutter. See PicoAudioCodec::GetMissedRefillCount().
            static uint32_t last_missed = 0;
            uint32_t missed = audio_codec.GetMissedRefillCount();
            if (missed != last_missed)
            {
                printf("[wav_file_reader] missed refill count: %lu\n", static_cast<unsigned long>(missed));
                last_missed = missed;
            }
        }
    }
}

void InitUI()
{
    gpio_init(kBlinkLedGpio);
    gpio_set_dir(kBlinkLedGpio, GPIO_OUT);
    sleep_ms(kBlinkPeriodMs);
}

int InitFileSystem()
{
    // Register and initialize the SD-over-SPI storage backend (app/FileSystem/hw_layer/
    // SdInterface/, wrapping the vendored carlk3/no-OS-FatFS-SD-SPI-RPi-Pico driver) as FatFs
    // drive 0, so diskio.cpp's disk_* functions have a working IBlockDevice to forward to.

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
        return static_cast<int>(mount_result);
    }
    else
    {
        // FileManager::Init() counts the top-level "bank_<N>" folders on the disk (see
        // FileManager::CountBanksOnDisk()/ValidateBankName()); these are the "folders" on the
        // SD card that this firmware treats as sample banks.

        int file_manager_init_result = file_manager.Init(disk_path, static_cast<uint8_t>(app::filesystem::SupportedFileExtensions::KWav));

        if (file_manager_init_result < 0)
        {
            printf("Failed to initialize file manager: %d\n", file_manager_init_result);
            return file_manager_init_result;
        }
        else
        {

            size_t number_of_banks = file_manager.GetNumberOfBanks();
            printf("Number of banks: %ld\n", number_of_banks);
            if (file_manager.SelectBank(0) < 0)
            {
                printf("Failed to select bank\n");
                return -1;
            }
        }
    }
    return 0;
}

int InitAudioSystem()
{
    app::audio::AudioPlayerConfiguration configuration = {
        .playback_speed = 1.0f,
        .n_channels = 2};

    audio_player.Init(configuration);

    // RP2040 stereo PWM-via-DMA playback device (see hw_interfaces/rp2040/pwm_audio_codec):
    // drives two independent 8-bit PWM output pins (left/right), unlike the native/Linux build's
    // NullAudioCodec (miniaudio-backed) used by platform/linux/main.cpp.

    int codec_init_result = audio_codec.Init();
    if (codec_init_result != 0)
    {
        printf("Audio codec Init result: %d\n", codec_init_result);
        return -1;
    }

    audio_codec.RegisterFillCallback(buffer_callback);

    // Unlike NullAudioCodec::Start() (native/Linux), PicoAudioCodec::Start() has no return value:
    // it primes both buffer halves via the registered fill_cb_ and kicks off both stereo
    // channels' DMA chains directly against the hardware.
    audio_codec.Start();

    return 0;
}
