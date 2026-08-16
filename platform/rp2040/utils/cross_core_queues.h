/**
 * @file cross_core_queues.h
 * @brief Cross-core (core0 <-> core1) message queue types/classes used by wav_file_reader.cpp to
 * bridge the RP2040 audio (core0) and UI (core1) halves of the app -- see that file's top-level
 * comment for the full multicore design rationale. Pulled out into its own translation unit
 * purely to keep wav_file_reader.cpp's main()/Core1Main() focused on orchestration instead of
 * message-queue plumbing.
 *
 * Two independent, one-directional queues are provided:
 *   - InputEventQueue (core1 -> core0): one hw_interface::InputEvent per detected UI command.
 *   - TelemetryQueue (core0 -> core1): one TelemetryMessage per notable AudioPlayer/FileManager
 *     state change or audio buffer snapshot, so core1 can update the display without core0 ever
 *     touching PicoDisplay/u8g2, and without core1 ever touching FileManager/AudioPlayer/FatFs.
 *     A single tagged-union message type is used (rather than one queue per kind) so core1 only
 *     needs to drain and switch on one queue -- see TelemetryMessage/TelemetryMessageType below.
 *
 * Both classes wrap a pico/util/queue.h queue_t (internally spinlock-protected, safe to use from
 * either core without any additional locking) and only ever use its non-blocking
 * queue_try_add()/queue_try_remove() variants, so a momentarily full queue just means the
 * offending message is dropped rather than either core stalling waiting on the other.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "pico/util/queue.h"

#include "IAudioCodec.h"
#include "IInputHandler.h"

// ---- core1 -> core0: UI commands ---------------------------------------------------------

// Thin wrapper around a queue_t of hw_interface::InputEvent: core1 pushes each UI-detected
// command here (see UserInterface::PopCommand()), core0's main loop drains it and acts on
// navigation events (see wav_file_reader.cpp).
class InputEventQueue
{
public:
    void Init(uint capacity);
    bool TryPush(const hw_interface::InputEvent &event);
    bool TryPop(hw_interface::InputEvent &out);

private:
    queue_t queue_{};
};

// ---- core0 -> core1: telemetry (player/file-manager state + audio buffer snapshot) --------

enum class TelemetryMessageType : uint8_t
{
    kPlayerState,
    kFileManagerState,
    kAudioBufferSnapshot,
};

// Snapshot of app::audio::AudioPlayer's playback mode/state (see AudioPlayer::IsPlaying()/
// IsReverse()/IsLooping()/IsFrozen()/GetPlaybackSpeed()/GetDurationMs()). No position/progress
// field is included -- there's no progress bar in this product, so duration alone (plus mode
// flags) is all core1 needs to reflect what's currently loaded/playing.
struct PlayerStateInfo
{
    bool is_playing;
    bool is_reverse;
    bool is_looping;
    bool is_freezed;
    uint32_t duration_ms;
    float playback_speed;
};

inline constexpr size_t kTelemetryFileNameLength = 64;

// Snapshot of app::filesystem::FileManager's current selection (see FileManager::
// GetCurrentBankIndex()/GetCurrentFileIndex()/GetNumberOfBanks()/
// GetNumberOfFileInCurrentBank()/GetFileName()). file_name is just the file's name (e.g.
// "kick.wav") -- NOT the bank-path-prefixed full path GetSelectedFilePath() returns.
struct FileManagerStateInfo
{
    size_t current_bank;
    size_t number_of_banks;
    size_t current_file;
    size_t number_of_files_in_bank;
    char file_name[kTelemetryFileNameLength];
};

// Matches hw_interface::PicoAudioCodec::kBufferSize (private to that class -- see
// hw_interfaces/rp2040/pwm_audio_codec/include/pwm_audio_codec.h), the number of mono samples
// per half of its double buffer, i.e. exactly what buffer_callback() (in wav_file_reader.cpp)
// receives per call.
inline constexpr size_t kTelemetryBufferFrames = 256;

// Raw copy of one PicoAudioCodec buffer's worth of decoded left/right samples -- a plain copy
// rather than pointers, since the source buffers are owned/reused by core0 and would be invalid
// (or worse, mid-mutation) by the time core1 got around to reading them if only pointers were
// passed across.
struct AudioBufferSnapshot
{
    float left[kTelemetryBufferFrames];
    float right[kTelemetryBufferFrames];
    size_t n_frames;
};

// Tagged union: exactly one queue is shared by all three telemetry message kinds (see
// TelemetryQueue below) rather than three separate queue_t instances, so core1 only has to
// drain and switch on a single queue. Element size is fixed at sizeof(TelemetryMessage)
// regardless of which member is active -- dominated by AudioBufferSnapshot, the largest member.
struct TelemetryMessage
{
    TelemetryMessageType type;
    union {
        PlayerStateInfo player_state;
        FileManagerStateInfo file_manager_state;
        AudioBufferSnapshot buffer_snapshot;
    };
};

// Wraps the core0 -> core1 telemetry queue_t plus the Push*() helpers that build each message
// kind, so callers only need to hand over already-known primitive values (from AudioPlayer/
// FileManager getters) instead of touching TelemetryMessage's union directly. Element size is
// dominated by AudioBufferSnapshot (~2KB: 256 frames * 2 channels * 4 bytes), pushed on every
// buffer_callback() invocation (~172/s at 44.1kHz) -- Init()'s capacity should be kept modest by
// the caller to bound RAM growth; a slow core1 consumer just means the audio buffer snapshot
// momentarily drops frames rather than backing up.
class TelemetryQueue
{
public:
    void Init(uint capacity);

    // Non-blocking -- if the queue is momentarily full, it's fine to drop a state update rather
    // than stall the audio path waiting on core1.
    void PushPlayerState(bool is_playing, bool is_reverse, bool is_looping, bool is_freezed,
                          uint32_t duration_ms, float playback_speed);
    void PushFileManagerState(size_t current_bank, size_t number_of_banks, size_t current_file,
                               size_t number_of_files_in_bank, const char *file_name);
    // Copies buffer.buffer_left/buffer_right (up to kTelemetryBufferFrames frames) and forwards
    // them to core1. Non-blocking, same drop-if-full rationale as the other Push*() methods.
    void PushAudioBufferSnapshot(const hw_interface::audio_buffer_t &buffer);

    bool TryPop(TelemetryMessage &out);

private:
    queue_t queue_{};
};
