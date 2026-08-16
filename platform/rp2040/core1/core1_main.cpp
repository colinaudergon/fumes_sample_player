/**
 * @file core1_main.cpp
 * @brief Everything that runs on RP2040 core1 -- see platform/rp2040/wav_file_reader.cpp's
 * file-level comment for the full core0/core1 split rationale.
 *
 * Core1 (launched via multicore_launch_core1(Core1Main) from wav_file_reader.cpp's main(), the
 * boot core/core0) owns everything UI-related: PicoDisplay, PicoRotaryEncoderInputHandler, and
 * UserInterface. Neither the u8g2 display driver nor the rotary encoder's GPIO IRQ registration
 * is safe to touch from both cores at once (GPIO IRQ callback registration in particular is
 * per-core), so all three objects are defined here rather than in wav_file_reader.cpp, and
 * core0 never references them directly -- it only exchanges messages with Core1Main() through
 * the InputEventQueue/TelemetryQueue globals declared in utils/cross_core_queues.h (defined and
 * Init()-ed in wav_file_reader.cpp before core1 is launched).
 */

#include <cstdio>

#include "pico_display.h"
#include "pico_rotary_encoder_input_handler.h"
#include "UserInterface.h"
#include "utils/cross_core_queues.h"

#include "core1_main.h"

// Rotary encoder A/B quadrature channels (see hw_interfaces/rp2040/rotary_encoder).
static constexpr uint kEncoderPinA = 9;
static constexpr uint kEncoderPinB = 0;

// Core1-owned (UI path) -- never touched from core0.
namespace
{
    hw_interface::PicoDisplay display;
    hw_interface::PicoRotaryEncoderInputHandler rotary_encoder(kEncoderPinA, kEncoderPinB);
    app::ui::UserInterface ui(rotary_encoder, display);
} // namespace

void Core1Main()
{
    // Bring up the SSD1306 SPI OLED (see hw_interfaces/rp2040/pico_display) and show a
    // placeholder so the display/wiring can be validated on hardware before anything else
    // (file system, audio) is wired up to it.
    if (display.Init() != 0)
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
        // it here: FileManager/AudioPlayer are core0-owned (see wav_file_reader.cpp).
        hw_interface::InputEvent command;
        while (ui.PopCommand(command))
        {
            input_event_queue_.TryPush(command);
        }

        // Drains telemetry pushed by core0 (see TelemetryQueue's Push*() methods). Note
        // telemetry_queue_'s capacity is small (see wav_file_reader.cpp's
        // kTelemetryQueueCapacity) and shared by all three message kinds -- if a burst of audio
        // buffer snapshots (pushed on every buffer_callback(), ~172/s) fills the queue right as
        // a player/file-manager state change is pushed, that state message can be dropped like
        // any other TryPush() overflow. Draining every iteration here (much faster than 172Hz)
        // keeps that window small in practice.
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
                if (latest_file_manager_state.fault)
                {
                    display.ShowText("fault");
                    return;
                }
                display.DisplayFileInfo(latest_file_manager_state.file_name, latest_player_state.duration_ms);
                break;
            case TelemetryMessageType::kAudioBufferSnapshot:
                latest_buffer_snapshot = telemetry.buffer_snapshot;
                display.DisplayAudioBufferContent(latest_buffer_snapshot.left, latest_buffer_snapshot.right, latest_buffer_snapshot.n_frames);
                break;
            }
        }
    }
}
