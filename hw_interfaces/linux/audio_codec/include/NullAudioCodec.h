/**
 * @file NullAudioCodec.h
 * @brief IAudioCodec implementation for native/Linux builds, backed by miniaudio for real
 * playback through the host's default audio output device.
 */

#pragma once

#include "IAudioCodec.h"
#include "miniaudio.h"

namespace hw_interface
{

    class NullAudioCodec : public IAudioCodec
    {
    public:
        NullAudioCodec() = default;
        ~NullAudioCodec() override;

        int Init() override;
        int RegisterFillCallback(buffer_fill_cb cb) override;

        // Not part of IAudioCodec: this backend needs an explicit start/stop of the underlying
        // miniaudio device (no real hardware to auto-start against).
        int Start();
        int Stop();
    private:
        static constexpr ma_uint32 kChannels = 2;
        static constexpr ma_uint32 kSampleRate = 44100;
        static constexpr ma_uint32 kMaxFramesPerCallback = 4096;

        static void DataCallback(ma_device *device, void *output, const void *input, ma_uint32 frame_count);
        void FillOutput(float *output, ma_uint32 frame_count);

        buffer_fill_cb fill_cb_ = nullptr;

        ma_context context_{};
        bool context_initialized_ = false;
        ma_device device_{};
        ma_device_config device_config_{};
        bool device_initialized_ = false;
        
        // Ping-pong scratch buffers handed to fill_cb_ as (buffer_0, buffer_1), alternating which
        // one is "current" (to be consumed this callback) vs "next" (read-ahead) on each call --
        // mirrors the double-buffer contract used by the embedded (RP2040) backend.
        float buffer_a_left_[kMaxFramesPerCallback];
        float buffer_a_right_[kMaxFramesPerCallback];
        float buffer_b_left_[kMaxFramesPerCallback];
        float buffer_b_right_[kMaxFramesPerCallback];
        audio_buffer_t buffer_a_{buffer_a_left_, buffer_a_right_, 0};
        audio_buffer_t buffer_b_{buffer_b_left_, buffer_b_right_, 0};
        bool use_buffer_a_as_current_ = true;
    };

} // namespace hw_interface
