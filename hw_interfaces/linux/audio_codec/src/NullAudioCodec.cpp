/**
 * @file NullAudioCodec.cpp
 * @brief IAudioCodec implementation for native/Linux builds, backed by miniaudio for real
 * playback through the host's default audio output device.
 */

#define MINIAUDIO_IMPLEMENTATION
#include "NullAudioCodec.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace hw_interface
{

    NullAudioCodec::~NullAudioCodec()
    {
        if (device_initialized_)
        {
            ma_device_uninit(&device_);
        }
        if (context_initialized_)
        {
            ma_context_uninit(&context_);
        }
    }

    namespace
    {
        // WSLg exposes host audio playback as a PulseAudio sink named "RDP Sink" (forwarded to
        // the Windows host over the RDP virtual channel). Enumerated playback devices are
        // searched for a name containing this substring so that we explicitly target it instead
        // of relying on whatever PulseAudio happens to report as its default sink.
        constexpr const char *kPreferredDeviceNameSubstr = "RDP Sink";

        // Returns the ma_device_id of the first enumerated playback device whose name contains
        // kPreferredDeviceNameSubstr, or nullptr if none match (caller should then fall back to
        // the backend's default device). The returned pointer is only valid while pContext
        // remains initialized, since it points directly into the context's device info array.
        const ma_device_id *FindPreferredPlaybackDeviceId(ma_context *pContext)
        {
            ma_device_info *playback_infos = nullptr;
            ma_uint32 playback_count = 0;
            if (ma_context_get_devices(pContext, &playback_infos, &playback_count, nullptr, nullptr) != MA_SUCCESS)
            {
                std::printf("NullAudioCodec: failed to enumerate playback devices.\n");
                return nullptr;
            }

            const ma_device_info *preferred = nullptr;
            for (ma_uint32 i = 0; i < playback_count; ++i)
            {
                const ma_device_info &info = playback_infos[i];
                std::printf("NullAudioCodec: found playback device: %s%s\n", info.name,
                            info.isDefault ? " (default)" : "");
                if (preferred == nullptr && std::strstr(info.name, kPreferredDeviceNameSubstr) != nullptr)
                {
                    preferred = &info;
                }
            }

            return (preferred != nullptr) ? &preferred->id : nullptr;
        }
    } // namespace

    int NullAudioCodec::Init()
    {
        if (ma_context_init(nullptr, 0, nullptr, &context_) != MA_SUCCESS)
        {
            std::printf("NullAudioCodec: failed to initialize miniaudio context.\n");
            return -1;
        }
        context_initialized_ = true;

        device_config_ = ma_device_config_init(ma_device_type_playback);
        // Explicitly target the WSLg RDP sink so audio is forwarded to the Windows host instead
        // of whichever device the backend would otherwise pick as "default"; falls back to the
        // default device if that sink isn't found (e.g. when running outside WSLg).
        device_config_.playback.pDeviceID = FindPreferredPlaybackDeviceId(&context_);
        device_config_.playback.format = ma_format_f32;
        device_config_.playback.channels = kChannels;
        device_config_.sampleRate = kSampleRate;
        device_config_.periodSizeInFrames = kMaxFramesPerCallback;
        device_config_.dataCallback = DataCallback;
        device_config_.pUserData = this;

        if (ma_device_init(&context_, &device_config_, &device_) != MA_SUCCESS)
        {
            std::printf("NullAudioCodec: failed to initialize miniaudio playback device.\n");
            ma_context_uninit(&context_);
            context_initialized_ = false;
            return -1;
        }

        // device_.playback.name is populated by ma_device_init() with the name of the backend
        // device that was actually selected.
        std::printf("NullAudioCodec: selected playback device: %s\n", device_.playback.name);

        device_initialized_ = true;
        return 0;
    }

    int NullAudioCodec::RegisterFillCallback(buffer_fill_cb cb)
    {
        fill_cb_ = cb;
        return 0;
    }

    int NullAudioCodec::Start()
    {
        if (!device_initialized_)
        {
            return -1;
        }

        return (ma_device_start(&device_) == MA_SUCCESS) ? 0 : -1;
    }

    int NullAudioCodec::Stop()
    {
        if (!device_initialized_)
        {
            return -1;
        }

        return (ma_device_stop(&device_) == MA_SUCCESS) ? 0 : -1;
    }


    void NullAudioCodec::DataCallback(ma_device *device, void *output, const void * /*input*/, ma_uint32 frame_count)
    {
        auto *self = static_cast<NullAudioCodec *>(device->pUserData);
        if (self == nullptr)
        {
            return;
        }

        self->FillOutput(static_cast<float *>(output), frame_count);
    }

    void NullAudioCodec::FillOutput(float *output, ma_uint32 frame_count)
    {
        const ma_uint32 frames_to_process = std::min(frame_count, kMaxFramesPerCallback);

        if (fill_cb_ == nullptr)
        {
            std::fill(output, output + static_cast<size_t>(frames_to_process) * kChannels, 0.0f);
            return;
        }

        // Alternate which scratch buffer is "current" (to be consumed this callback) vs "next"
        // (read-ahead), same double-buffer contract as the embedded (RP2040) backend.
        audio_buffer_t &current = use_buffer_a_as_current_ ? buffer_a_ : buffer_b_;
        audio_buffer_t &next = use_buffer_a_as_current_ ? buffer_b_ : buffer_a_;
        use_buffer_a_as_current_ = !use_buffer_a_as_current_;
        current.buffer_len = frames_to_process;
        next.buffer_len = frames_to_process;

        fill_cb_(&current, &next);

        for (ma_uint32 i = 0; i < frames_to_process; i++)
        {
            output[i * kChannels + 0] = current.buffer_left[i];
            output[i * kChannels + 1] = current.buffer_right[i];
        }
    }

} // namespace hw_interface

