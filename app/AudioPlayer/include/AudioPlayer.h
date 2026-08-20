/**
 * @file AudioPlayer.h
 * @brief
 */

#pragma once

#include "../../../hw_interfaces/include/IAudioCodec.h"
#include "../../include/IFileSystem.h"
#include "wav_file_handler.h"
#include "GlitchEngine.h"

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

        /// @brief Sets the start/stop marker as a position relative to the file's total length.
        /// @param relative_position 0.0 = start of file, 1.0 = end of file. Clamped to [0, 1].
        void SetStartMarker(float relative_position);
        void SetStopMarker(float relative_position);
        size_t GetStartMarker();
        size_t GetStopMarker();

        /// @brief Same as GetStartMarker()/GetStopMarker(), converted to milliseconds using the
        /// loaded file's sample rate. Returns 0 if no file has been loaded yet.
        uint32_t GetStartMarkerMs();
        uint32_t GetStopMarkerMs();

        int Start();
        int Stop();

        int SetLooping(bool looping);

        int SetPlaybackSpeed(float speed);
        /// @brief Returns the current playback speed multiplier (see SetPlaybackSpeed()),
        /// already clamped to [kMinPlayBackSpeed, kMaxPlaybackSpeed].
        float GetPlaybackSpeed();

        void Freeze(bool enable);

        void SetGlitching(bool enable);
        void SetGlitch(float amount);

        // ---- Individual GlitchEngine parameter controls: direct pass-throughs to
        // glitch_engine_, distinct from SetGlitch()'s single composite "amount" knob above.
        void EnableNoiseOutput(bool enable);
        void EnablePitchMod(bool enable);
        void SetBitcrushEnable(bool enable);
        void SetPitchModProbability(float value);
        void SetStutterProbability(float value);
        void SetSampleRateReduction(int value);
        void SetReductionFactor(int value);
        int GetSampleRate();
        size_t GetNumChannels();
        int GetBitsPerSample();
        int GetDataSize();

        bool IsNewAudioDataRequired();

        /// @brief Returns the total playback duration of the currently loaded file, in
        /// milliseconds, derived from its data size/sample rate/channels/bit depth. Returns 0
        /// if no file has been loaded yet (or its header hasn't been parsed).
        uint32_t GetDurationMs();

        /// @brief Returns the current playhead position as a frame index into the file's data
        /// chunk, normalized so it means "frame currently being played" regardless of direction
        /// (current_frame_index_ itself is direction-asymmetric: see its declaration below).
        size_t GetPlayheadFrame();

        /// @brief Same as GetPlayheadFrame(), converted to milliseconds using the loaded file's
        /// sample rate. Returns 0 if no file has been loaded yet.
        uint32_t GetPlayheadMs();

        /// @brief Returns the path passed to the most recent successful LoadFile() call, or an
        /// empty string if no file has been loaded yet.
        const char *GetAudioFile();

        /// @brief Fills `data` with `n_frames` downsampled amplitude values (one per display
        /// column) covering the whole loaded file's data chunk, for waveform display. Each value
        /// is the peak absolute amplitude, within that column's frame range, of a locally
        /// computed mono mixdown (average of L/R) of the source, scaled to the full uint16_t
        /// range. Only callable while playback is stopped (is_playing_ == false), since it reuses
        /// the time_adjust_source_l_/r_ scratch buffers that Read() uses while streaming. The
        /// file cursor is restored to the start of the data chunk (LoadFile()'s postcondition)
        /// before returning, regardless of the current playback position.
        /// @param data Destination array; must have room for exactly `n_frames` values.
        /// @param n_frames Number of columns to fill (typically the display width in pixels).
        /// @return 0 on success, -1 if no file is loaded, playback is active, or the arguments
        /// are invalid.
        int GetAudioDataToDisplay(uint16_t *data, size_t n_frames);

        bool IsPlaying();

        /// @brief Returns whether reverse playback is currently enabled (see SetReverse()).
        bool IsReverse();

        /// @brief Returns whether looping is currently enabled (see SetLooping()).
        bool IsLooping();

        /// @brief Returns whether playback is currently frozen (see Freeze()).
        bool IsFrozen();

    private:
        // ---- Private helper methods ----

        void FillWithZeros(wav::audio_frame_t &output, size_t n_frames);

        /// @brief Resamples `input` (whose valid length is `input.n_frames`) into `output`,
        /// stretching/compressing it to exactly `n_frames` output frames via linear
        /// interpolation (see ApplyInterpolation()). Used to implement variable playback speed.
        /// @param input Source buffer; input.n_frames is the number of valid source frames.
        /// @param output Destination buffer, filled with exactly `n_frames` frames.
        /// @param n_frames The desired number of OUTPUT frames (already computed by the caller).
        void AdjustTime(wav::audio_frame_t &input, wav::audio_frame_t &output, size_t n_frames);

        void TrackCurrentFrameIndex(size_t total_frames_read);
        void TrackFileReadIndex(size_t bytes_read);

        void SeekStartChunk();
        void SeekStartMarker();

        /// @brief Total number of frames in the currently loaded file's data chunk, or 0 if
        /// nothing is loaded / frame_bytes_ hasn't been computed yet.
        size_t GetTotalFrames();

        /// @brief True when the marker ordering means the playable range wraps around the file
        /// boundary: forward plays [start_marker_, end of file) then [0, stop_marker_) when
        /// stop_marker_ < start_marker_; reverse plays [start_marker_, 0] then
        /// [end of file, stop_marker_) when stop_marker_ > start_marker_.
        bool IsWrapEnabled();

        /// @brief If playback has just reached the far boundary (physical end of file when
        /// forward, frame 0 when reverse) with wraparound enabled (see IsWrapEnabled()) and
        /// hasn't already wrapped once this playthrough, seeks to the opposite boundary and
        /// returns true. Must be called after TrackCurrentFrameIndex() has updated
        /// current_frame_index_ for the frames just read.
        bool WrapMarker();

        size_t FetchData(size_t buffer_offset = 0);

        bool HandleEndOfFile(wav::audio_frame_t &output, size_t n_frames, size_t total_frames_read);

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

        /// @brief Sets start_marker_/stop_marker_ from a position relative to the file's total
        /// length (0.0 = start of file, 1.0 = end of file), converting to a frame index using
        /// GetTotalFrames(). `relative_position` is clamped to [0, 1].
        void SetMarker(float relative_position, bool is_start_marker);

        bool IsOutpuBufferValid(wav::audio_frame_t &output);
        // ---- Playback state flags ----

        bool is_reverse_{false};
        bool is_playing_{false};
        bool is_looping_{false};
        bool is_freezed_{false};

        // True once playback has physically wrapped around the file boundary during the current
        // playthrough (see IsWrapEnabled()/WrapMarker()). Reset by SeekStartMarker().
        bool has_wrapped_{false};

        // ---- Start/stop markers ----

        static constexpr size_t kDefaultStartPointer = 0;
        static constexpr uint32_t kMillisMultiplier = 1000;

        size_t start_marker_{kDefaultStartPointer};
        size_t stop_marker_{0};

        // ---- Playback speed ----

        static constexpr float kMinPlayBackSpeed = 0.01f;
        // Upper clamp on playback_speed_: bounds how many raw source frames a single Read() call
        // can ever need (see kMaxSourceFrames below), so the pre-resample scratch buffers stay a
        // fixed, safe size regardless of what SetPlaybackSpeed() is called with.
        static constexpr float kMaxPlaybackSpeed = 4.0f;
        float playback_speed_{kMinPlayBackSpeed};
        

        // ---- Glitch amount/ state ----
        bool glitch_enable_{false};
        float glitch_amount_{0.0f};
        
        GlitchEngine glitch_engine_;


        // ---- File/format state ----

        size_t n_channels_{0};
        IFileSystem &file_system_;
        wav::WavFileHandler wav_file_handler_;
        FsFile *file_{nullptr};

        // Mirrors FsFileInfo::name's fixed-size buffer convention (see IFileSystem.h) to avoid
        // any dynamic allocation for something this small/short-lived.
        static constexpr size_t kMaxFilePathLength = 256;
        char loaded_file_path_[kMaxFilePathLength] = {};

        static constexpr size_t kWavHeaderSize = 44;

        // Next frame to read when forward; one past the last frame played when reverse (see
        // SeekStartMarker()). Not a UI-friendly "playhead" position on its own — use
        // GetPlayheadFrame()/GetPlayheadMs() for that.
        size_t current_frame_index_{kWavHeaderSize};

        // ---- Read()/FetchData() scratch buffers and bookkeeping ----

        // Bounded scratch buffer used to stream+convert raw file bytes into Read()'s output in
        // chunks, avoiding both unbounded stack/heap use and any extra copy: WavFileHandler::
        // ReadData() converts straight from this buffer into the caller's output arrays.
        static constexpr size_t kMaxReadFrames = 1024;
        static constexpr size_t kMaxBytesPerFrame = 8; // 2 channels * 4 bytes (32-bit) max
        uint8_t read_scratch_buffer_[kMaxReadFrames * kMaxBytesPerFrame];

        size_t bytes_per_sample_{0};
        size_t frame_bytes_{0};
        size_t n_frames_out_{0};
        size_t frames_remaining_{0};

        // Read()'s precondition: callers must not request more than this many output frames per
        // call. Defaults to 4096 (matching hw_interface::NullAudioCodec::kMaxFramesPerCallback,
        // the largest buffer the native/Linux build's IAudioCodec backend hands to
        // AudioPlayer::Read() at once), but is overridable at compile time via
        // APP_AUDIO_MAX_FRAMES_PER_CALLBACK -- e.g. the RP2040 build overrides this to 256
        // (hw_interface::PicoAudioCodec's actual buffer size) since the desktop-sized default
        // would otherwise reserve ~128KB for time_adjust_source_l_/r_ alone, overflowing the
        // RP2040's much smaller SRAM.
#ifndef APP_AUDIO_MAX_FRAMES_PER_CALLBACK
#define APP_AUDIO_MAX_FRAMES_PER_CALLBACK 4096
#endif
        static constexpr size_t kMaxOutputFrames = APP_AUDIO_MAX_FRAMES_PER_CALLBACK;
        size_t file_read_index_{0};

        // Capacity of the pre-resample scratch buffers Read() fills at the file's native sample
        // rate before AdjustTime() stretches/compresses them into exactly kMaxOutputFrames output
        // frames: worst case is kMaxOutputFrames output frames at kMaxPlaybackSpeed.
        static constexpr size_t kMaxSourceFrames = kMaxOutputFrames * static_cast<size_t>(kMaxPlaybackSpeed);
        float time_adjust_source_l_[kMaxSourceFrames] = {};
        float time_adjust_source_r_[kMaxSourceFrames] = {};
    };

} // namespace app::audio