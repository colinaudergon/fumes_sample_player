/**
 * @file cross_core_queues.cpp
 * @brief Implementation of InputEventQueue/TelemetryQueue -- see cross_core_queues.h.
 */

#include "cross_core_queues.h"

#include <algorithm>
#include <cstring>

// ---- InputEventQueue -----------------------------------------------------------------------

void InputEventQueue::Init(uint capacity)
{
    queue_init(&queue_, sizeof(hw_interface::InputEvent), capacity);
}

bool InputEventQueue::TryPush(const hw_interface::InputEvent &event)
{
    return queue_try_add(&queue_, &event);
}

bool InputEventQueue::TryPop(hw_interface::InputEvent &out)
{
    return queue_try_remove(&queue_, &out);
}

// ---- TelemetryQueue ------------------------------------------------------------------------

void TelemetryQueue::Init(uint capacity)
{
    queue_init(&queue_, sizeof(TelemetryMessage), capacity);
}

void TelemetryQueue::PushPlayerState(bool is_playing, bool is_reverse, bool is_looping, bool is_freezed,
                                      uint32_t duration_ms, float playback_speed)
{
    TelemetryMessage message{};
    message.type = TelemetryMessageType::kPlayerState;
    message.player_state.is_playing = is_playing;
    message.player_state.is_reverse = is_reverse;
    message.player_state.is_looping = is_looping;
    message.player_state.is_freezed = is_freezed;
    message.player_state.duration_ms = duration_ms;
    message.player_state.playback_speed = playback_speed;
    queue_try_add(&queue_, &message);
}

void TelemetryQueue::PushFileManagerState(size_t current_bank, size_t number_of_banks, size_t current_file,
                                           size_t number_of_files_in_bank, const char *file_name)
{
    TelemetryMessage message{};
    message.type = TelemetryMessageType::kFileManagerState;
    message.file_manager_state.current_bank = current_bank;
    message.file_manager_state.number_of_banks = number_of_banks;
    message.file_manager_state.current_file = current_file;
    message.file_manager_state.number_of_files_in_bank = number_of_files_in_bank;
    std::strncpy(message.file_manager_state.file_name, file_name,
                 sizeof(message.file_manager_state.file_name) - 1);
    queue_try_add(&queue_, &message);
}

void TelemetryQueue::PushAudioBufferSnapshot(const hw_interface::audio_buffer_t &buffer)
{
    TelemetryMessage message{};
    message.type = TelemetryMessageType::kAudioBufferSnapshot;
    const size_t frames_to_copy = std::min(buffer.buffer_len, kTelemetryBufferFrames);
    std::memcpy(message.buffer_snapshot.left, buffer.buffer_left, frames_to_copy * sizeof(float));
    std::memcpy(message.buffer_snapshot.right, buffer.buffer_right, frames_to_copy * sizeof(float));
    message.buffer_snapshot.n_frames = frames_to_copy;
    queue_try_add(&queue_, &message);
}

bool TelemetryQueue::TryPop(TelemetryMessage &out)
{
    return queue_try_remove(&queue_, &out);
}
