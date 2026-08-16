/**
 * @file wav_file_reader.cpp
 * @brief RP2040/Pico entry point.
 *
 * Mirrors platform/linux/main.cpp's storage-mounting setup, but wired against the RP2040
 * hardware implementations (SdBlockDevice) instead of the native/Linux ones
 * (hw_interfaces/linux/PosixBlockDevice), using the same hardware-agnostic app-layer classes
 * (IFileSystem, FileManager) either platform build shares.
 *
 * Split across both RP2040 cores via pico_multicore: core0 (this file's main(), the boot core)
 * owns everything SD-card/audio-related (FileManager, AudioPlayer, PicoAudioCodec/
 * ServiceRefill()) so it's never blocked by UI work; core1 (Core1Main(), launched from main()
 * via multicore_launch_core1()) owns everything UI-related (PicoDisplay,
 * PicoRotaryEncoderInputHandler, UserInterface). Neither FatFs/SD nor the u8g2 display driver is
 * safe to touch from both cores at once, so the two sides never call into each other's owned
 * objects directly -- they only exchange messages through the InputEventQueue/TelemetryQueue
 * wrappers (see utils/cross_core_queues.h) around one-directional pico/util/queue.h queue_t
 * instances (each internally spinlock-protected, safe to use from either core without any
 * additional locking on our part):
 *   - input_event_queue_: core1 -> core0, one hw_interface::InputEvent per detected UI command.
 *   - telemetry_queue_: core0 -> core1, one TelemetryMessage per notable AudioPlayer/FileManager
 *     state change or audio buffer snapshot, so core1 can update the display without core0 ever
 *     touching PicoDisplay/u8g2, and without core1 ever touching FileManager/AudioPlayer/FatFs.
 */

#include <cstdio>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

#include "FileManager.h"
#include "IBlockDevice.h"
#include "IFileSystem.h"
#include "SdBlockDevice.h"
#include "AudioPlayer.h"
#include "pwm_audio_codec.h"
#include "EffectController.h"
#include "pico_display.h"
#include "pico_rotary_encoder_input_handler.h"
#include "UserInterface.h"
#include "utils/cross_core_queues.h"

// Rotary encoder A/B quadrature channels (see hw_interfaces/rp2040/rotary_encoder).
static constexpr uint kEncoderPinA = 9;
static constexpr uint kEncoderPinB = 0;

// Core0-owned (SD card / audio path) -- never touched from core1.
app::audio::AudioPlayer audio_player;
hw_interface::PicoAudioCodec audio_codec;
filesystem::SdBlockDevice sd_block_device;
app::filesystem::FileManager file_manager;
app::audio::EffectController effect_controller;

// Core1-owned (UI path) -- never touched from core0.
hw_interface::PicoDisplay display;
hw_interface::PicoRotaryEncoderInputHandler rotary_encoder(kEncoderPinA, kEncoderPinB);
app::ui::UserInterface ui(rotary_encoder, display);

// Cross-core message queues (see file-level comment above and utils/cross_core_queues.h).
// Capacities are sized generously relative to how fast either producer can realistically
// enqueue messages (one InputEvent per detected encoder step; telemetry messages per state
// change/buffer callback) -- both queues only ever use non-blocking Try*() methods, so a
// momentarily full queue just means the offending message is dropped rather than either core
// stalling waiting on the other.
static constexpr uint kInputEventQueueCapacity = 8;
InputEventQueue input_event_queue_;

// telemetry_queue_'s element size is dominated by AudioBufferSnapshot (~2KB: 256 frames * 2
// channels * 4 bytes), pushed on every buffer_callback() invocation (~172/s at 44.1kHz), so its
// capacity is kept modest to bound RAM growth (~3 * ~2KB =~ 6-7KB) -- a slow core1 consumer just
// means the audio buffer snapshot momentarily drops frames rather than backing up.
static constexpr uint kTelemetryQueueCapacity = 3;
TelemetryQueue telemetry_queue_;

void Core1Main();
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

    // Forwards a copy of what was just decoded to core1 on every callback. buffer_callback()
    // runs from ServiceRefill() in main()'s loop (not DMA IRQ context -- see the comment there),
    // but the push must still stay non-blocking so a momentarily full queue can never stall
    // audio refilling (see TelemetryQueue::PushAudioBufferSnapshot()).
    telemetry_queue_.PushAudioBufferSnapshot(current);
}

int main()
{
    stdio_init_all();
    set_sys_clock_khz(176000, true);

    // Both queues must exist before core1 starts (Core1Main() pushes to/reads from them
    // immediately), and Init()/queue_t are not core-specific, so initializing them here on
    // core0 before multicore_launch_core1() is safe.
    input_event_queue_.Init(kInputEventQueueCapacity);
    telemetry_queue_.Init(kTelemetryQueueCapacity);

    // Core1 owns PicoDisplay/PicoRotaryEncoderInputHandler/UserInterface from here on -- see the
    // file-level comment for why core0 never touches them directly.
    multicore_launch_core1(Core1Main);

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

            telemetry_queue_.PushFileManagerState(file_manager.GetCurrentBankIndex(), file_manager.GetNumberOfBanks(),
                                                   file_manager.GetCurrentFileIndex(),
                                                   file_manager.GetNumberOfFileInCurrentBank(), file_manager.GetFileName());
            telemetry_queue_.PushPlayerState(audio_player.IsPlaying(), audio_player.IsReverse(), audio_player.IsLooping(),
                                              audio_player.IsFrozen(), audio_player.GetDurationMs(),
                                              audio_player.GetPlaybackSpeed());
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

    uint32_t last_check_ms = to_ms_since_boot(get_absolute_time());
    static constexpr uint32_t kMissedRefillCheckPeriodMs = 500;

    while (true)
    {
        // Serviced here (main-loop context), NOT from the DMA IRQ: buffer_callback ultimately
        // calls into AudioPlayer::Read() -> the SD card file system, which can block for a
        // while (mutex-guarded SPI DMA transfer) -- unsafe to run directly inside a hardware
        // interrupt handler. PicoAudioCodec's IRQ only swaps buffers and flags that a refill is
        // needed; polling this every iteration (instead of blocking in sleep_ms() below) is what
        // actually keeps audio fed.
        audio_codec.ServiceRefill();

        // Drains commands forwarded by core1 (see Core1Main()) instead of polling the rotary
        // encoder/UserInterface directly -- those are core1-owned. InputEventQueue::TryPop() is
        // non-blocking, so this never stalls ServiceRefill() waiting on core1.
        hw_interface::InputEvent command;
        while (input_event_queue_.TryPop(command))
        {
            if (command.type == hw_interface::InputEventType::kNavigationEvent)
            {
                // Guard against the DMA IRQ racking up "missed refill" underruns while the
                // (blocking, SD-card-bound) file switch below runs: SelectNextFile()/LoadFile()
                // can take long enough that ServiceRefill() doesn't get called between two DMA
                // buffer swaps, and each such miss (see PicoAudioCodec::GetMissedRefillCount())
                // replays a stale buffer -- audible as a click/stutter. Stopping the codec here
                // halts DMA output entirely for the switch's duration; audio_codec.Start() below
                // re-primes both buffer halves straight from the newly loaded file before
                // resuming, so playback picks up cleanly instead of stuttering through the old
                // file's tail.
                audio_codec.Stop();
                audio_player.Stop();

                int ret = 0;
                if (command.navigationDirection == hw_interface::NavigationDirection::kUp)
                {
                    ret = file_manager.SelectNextFile();
                }
                else if (command.navigationDirection == hw_interface::NavigationDirection::kDown)
                {
                    ret = file_manager.SelectPreviousFile();
                }

                const char *selected_path = file_manager.GetSelectedFilePath();
                if (ret == 0 && selected_path[0] != '\0' && audio_player.LoadFile(selected_path) == 0)
                {
                    telemetry_queue_.PushFileManagerState(file_manager.GetCurrentBankIndex(), file_manager.GetNumberOfBanks(),
                                                           file_manager.GetCurrentFileIndex(),
                                                           file_manager.GetNumberOfFileInCurrentBank(), file_manager.GetFileName());
                    telemetry_queue_.PushPlayerState(audio_player.IsPlaying(), audio_player.IsReverse(), audio_player.IsLooping(),
                                                      audio_player.IsFrozen(), audio_player.GetDurationMs(),
                                                      audio_player.GetPlaybackSpeed());
                    audio_player.Start();
                }

                // Resume DMA output either way: if the load above failed, AudioPlayer::Read()'s
                // is_playing_/file_ guard makes fill_cb_ emit silence instead of leaving the
                // codec (and thus all audio output) permanently stopped.
                audio_codec.Start();
            }
        }

        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - last_check_ms >= kMissedRefillCheckPeriodMs)
        {
            last_check_ms = now_ms;

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

// Runs entirely on core1 (see multicore_launch_core1(Core1Main) in main()). 
// Owns PicoDisplay/PicoRotaryEncoderInputHandler/UserInterface.
void Core1Main()
{
    // Bring up the SSD1306 SPI OLED (see hw_interfaces/rp2040/pico_display) and show a
    // placeholder so the display/wiring can be validated on hardware before anything else
    // (file system, audio) is wired up to it.
    if (display.Init() == 0)
    {
        display.ShowText("Display OK");
    }
    else
    {
        printf("Failed to init display\n");
    }

    // Bring up the rotary encoder (see hw_interfaces/rp2040/rotary_encoder). GPIO IRQ callback
    // registration (gpio_set_irq_enabled_with_callback()) is per-core, so this must happen here
    // on core1 -- not in main() on core0 -- for the interrupt to actually fire on this core.
    if (rotary_encoder.Init() != 0)
    {
        printf("Failed to init rotary encoder\n");
    }

    // Latest telemetry snapshots pushed by core0 (see telemetry_queue_ drain below).
    // DisplayFileInfo() needs both the file name (from FileManagerStateInfo) and duration (from
    // PlayerStateInfo), which now arrive as two separate messages (see TelemetryMessageType) --
    // each is cached here so the display can be refreshed with the latest of both whenever
    // either one updates. latest_buffer_snapshot is cached for future visualization (e.g. a
    // waveform/VU meter); no rendering is added for it in this task, only the transport.
    FileManagerStateInfo latest_file_manager_state{};
    PlayerStateInfo latest_player_state{};
    AudioBufferSnapshot latest_buffer_snapshot{};

    while (true)
    {
        // Polls the rotary encoder via IInputHandler, queuing any detected step as a kNavigationEvent
        ui.ProcessUi();

        // Forwards each command UserInterface queued internally to core0 instead of acting on
        // it here: FileManager/AudioPlayer are core0-owned (see file-level comment).
        hw_interface::InputEvent command;
        while (ui.PopCommand(command))
        {
            input_event_queue_.TryPush(command);
        }

        // Drains telemetry pushed by core0 (see TelemetryQueue's Push*() methods). Note
        // telemetry_queue_'s capacity is small (see kTelemetryQueueCapacity) and shared by all
        // three message kinds -- if a burst of audio buffer snapshots (pushed on every
        // buffer_callback(), ~172/s) fills the queue right as a player/file-manager state
        // change is pushed, that state message can be dropped like any other TryPush()
        // overflow. Draining every iteration here (much faster than 172Hz) keeps that window
        // small in practice.
        TelemetryMessage telemetry;
        while (telemetry_queue_.TryPop(telemetry))
        {
            switch (telemetry.type)
            {
            case TelemetryMessageType::kPlayerState:
                latest_player_state = telemetry.player_state;
                display.DisplayFileInfo(latest_file_manager_state.file_name, latest_player_state.duration_ms);
                break;
            case TelemetryMessageType::kFileManagerState:
                latest_file_manager_state = telemetry.file_manager_state;
                display.DisplayFileInfo(latest_file_manager_state.file_name, latest_player_state.duration_ms);
                break;
            case TelemetryMessageType::kAudioBufferSnapshot:
                latest_buffer_snapshot = telemetry.buffer_snapshot;
                break;
            }
        }
    }
}

int InitFileSystem()
{
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

    int codec_init_result = audio_codec.Init();
    if (codec_init_result != 0)
    {
        printf("Audio codec Init result: %d\n", codec_init_result);
        return -1;
    }

    audio_codec.RegisterFillCallback(buffer_callback);
    audio_codec.Start();

    return 0;
}
