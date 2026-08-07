/**
 * @file AudioPlayer.h
 * @brief
 */

#pragma once

#include "../../../hw_interfaces/include/IAudioCodec.h"
#include "../../include/IFileSystem.h"
#include "wav_file_handler.h"

namespace app::audio
{

    struct AudioPlayerConfiguration
    {
        float playback_speed;
        size_t n_channels;
    };

    class AudioPlayer
    {
    public:
        // No IFileSystem is injected: the instance is obtained internally from GetFileSystem()
        // (see IFileSystem.h). A dedicated factory will replace this direct call later.
        AudioPlayer() : file_system_(GetFileSystem()) {}
        ~AudioPlayer();

        int Init(AudioPlayerConfiguration &configuration);
        int LoadFile(const char *path);
        /// @brief Fills `output` with exactly `n_frames` frames (resampled to account for
        /// SetPlaybackSpeed(), via AdjustTime()/ApplyInterpolation()), or silence if nothing is
        /// currently playing/loaded.
        /// @param n_frames Requested output frame count; must not exceed kMaxOutputFrames.
        int Read(wav::audio_frame_t &output, size_t n_frames);
        void SetReverse(bool enable);
        int Start();
        int Stop();
        int SetLooping(bool looping);
        int SetPlaybackSpeed(float speed);
        /// @brief Returns the current playback speed multiplier (see SetPlaybackSpeed()),
        /// already clamped to [kMinPlayBackSpeed, kMaxPlaybackSpeed].
        float GetPlaybackSpeed();
        void Freeze(bool enable);
        int GetSampleRate();
        size_t GetNumChannels();
        int GetBitsPerSample();
        int GetDataSize();

        /// @brief Returns the total playback duration of the currently loaded file, in
        /// milliseconds, derived from its data size/sample rate/channels/bit depth. Returns 0
        /// if no file has been loaded yet (or its header hasn't been parsed).
        uint32_t GetDurationMs();

        /// @brief Returns the path passed to the most recent successful LoadFile() call, or an
        /// empty string if no file has been loaded yet.
        const char *GetAudioFile();

        bool IsPlaying();

    private:
        void FillWithZeros(wav::audio_frame_t &output, size_t n_frames);
        /// @brief Resamples `input` (whose valid length is `input.n_frames`) into `output`,
        /// stretching/compressing it to exactly `n_frames` output frames via linear
        /// interpolation (see ApplyInterpolation()). Used to implement variable playback speed.
        /// @param input Source buffer; input.n_frames is the number of valid source frames.
        /// @param output Destination buffer, filled with exactly `n_frames` frames.
        /// @param n_frames The desired number of OUTPUT frames (already computed by the caller).
        void AdjustTime(wav::audio_frame_t &input, wav::audio_frame_t &output, size_t n_frames);
        /// @brief Linearly interpolates the sample at fractional index `single_value` within
        /// `input`, using `n_frames` as the bounds of that buffer. Takes a raw channel pointer
        /// (rather than a full audio_frame_t) so it can be reused for either channel.
        /// @param input Single-channel source buffer to interpolate from.
        /// @param single_value Fractional sample index (e.g. current read position at a
        /// playback speed other than 1.0).
        /// @param n_frames Number of valid frames in `input`.
        /// @return The interpolated sample value, or 0.0f if `input` is null or `n_frames` is 0.
        float ApplyInterpolation(float *input, float single_value, size_t n_frames);
        void ReverseFrames(wav::audio_frame_t &input, size_t n_frames);
        bool is_reverse_{false};
        bool is_playing_{false};
        bool is_looping_{false};
        bool is_freezed_{false};
        static constexpr float kMinPlayBackSpeed = 0.01f;
        // Upper clamp on playback_speed_: bounds how many raw source frames a single Read() call
        // can ever need (see kMaxSourceFrames below), so the pre-resample scratch buffers stay a
        // fixed, safe size regardless of what SetPlaybackSpeed() is called with.
        static constexpr float kMaxPlaybackSpeed = 4.0f;
        float playback_speed_{kMinPlayBackSpeed};
        size_t n_channels{0};
        IFileSystem &file_system_;
        wav::WavFileHandler wav_file_handler_;
        FsFile *file_{nullptr};

        // Mirrors FsFileInfo::name's fixed-size buffer convention (see IFileSystem.h) to avoid
        // any dynamic allocation for something this small/short-lived.
        static constexpr size_t kMaxFilePathLength = 256;
        char loaded_file_path_[kMaxFilePathLength] = {};

        static constexpr size_t kWavHeaderSize = 44;
        size_t current_frame_index_{0};
        // Bounded scratch buffer used to stream+convert raw file bytes into Read()'s output in
        // chunks, avoiding both unbounded stack/heap use and any extra copy: WavFileHandler::
        // ReadData() converts straight from this buffer into the caller's output arrays.
        static constexpr size_t kMaxReadFrames = 1024;
        static constexpr size_t kMaxBytesPerFrame = 8; // 2 channels * 4 bytes (32-bit) max
        uint8_t read_scratch_buffer_[kMaxReadFrames * kMaxBytesPerFrame];

        // Read()'s precondition: callers must not request more than this many output frames per
        // call (matches hw_interface::NullAudioCodec::kMaxFramesPerCallback, the largest buffer
        // any current IAudioCodec backend hands to AudioPlayer::Read() at once).
        static constexpr size_t kMaxOutputFrames = 4096;

        // Capacity of the pre-resample scratch buffers Read() fills at the file's native sample
        // rate before AdjustTime() stretches/compresses them into exactly kMaxOutputFrames output
        // frames: worst case is kMaxOutputFrames output frames at kMaxPlaybackSpeed.
        static constexpr size_t kMaxSourceFrames = kMaxOutputFrames * static_cast<size_t>(kMaxPlaybackSpeed);
        float time_adjust_source_l_[kMaxSourceFrames] = {};
        float time_adjust_source_r_[kMaxSourceFrames] = {};
    };

} // namespace app::audio