#pragma once
#include <cstddef>
#include <cstdint>
#include "../../include/AudioTypes.h"
#include "../lfsr/lfsr.h"

namespace app::audio
{

    class EnvFollower
    {
    public:
        // scaling should be between 0 and 1
        EnvFollower(float attack_scaling, float decay_scaling) : attack_scaling_(attack_scaling), decay_scaling_(decay_scaling) {};

        float Process(float sample)
        {
            if (sample < 0)
            {
                sample = sample * (-1);
            }

            const auto abs_x = sample;

            if (abs_x > output_val_)
            {
                output_val_ = attack_scaling_ * output_val_ + (1 - attack_scaling_) * abs_x;
            }
            else
            {
                output_val_ = decay_scaling_ * output_val_ + (1 - decay_scaling_) * abs_x;
            }

            return output_val_;
        }

    private:
        float output_val_{0};
        float attack_scaling_;
        float decay_scaling_;
    };

    class BeatTracker
    {
    public:
        enum class BeatState : uint8_t
        {
            kArmed,
            kTriggered
        };

        void Process(float sample_l, float sample_r, size_t frame)
        {

            // Process can be called continously on a small repeating small buffer,
            // two options:
            // 1 the audio buffer dispatcher keeps track of the indexes it already beat detected
            // 2 The BeatTracker does not recompute a beat start for a frame already in beat_frame_tracker_
            // if beat_frame_tracker_ already contains frame, we skip it

            mono_env_ = (env_follower_l_.Process(sample_l) + env_follower_r_.Process(sample_r)) * 0.5f;

            bool rising = mono_env_ > prev_mono_env_;
            prev_mono_env_ = mono_env_;

            switch (state_)
            {
            case BeatState::kArmed:
                if (rising && mono_env_ >= low_to_high_thres)
                {
                    frame_index_ = (frame_index_ + 1) % kTrackingBufferSize;
                    beat_frame_tracker_[frame_index_] = frame;
                    state_ = BeatState::kTriggered;
                }

                break;
            case BeatState::kTriggered:
                if (mono_env_ <= high_to_low_thres)
                {
                    state_ = BeatState::kArmed;
                }
                break;
            default:
                break;
            }
        }

        void Reset()
        {

            frame_index_ = 0;
            state_ = BeatState::kArmed;

            for (size_t i = 0; i < kTrackingBufferSize; i++)
            {
                beat_frame_tracker_[i] = 0;
            }
        }

        void SetLowToHighThreshold(float value)
        {
            low_to_high_thres = value;
        }
        void SetHighToLowThreshold(float value)
        {
            high_to_low_thres = value;
        }
        size_t GetLastFrameIndex()
        {
            return frame_index_;
        }

        // Looks up the file frame position recorded at a given ring-buffer slot (see Process()).
        // ring_index is wrapped into [0, kTrackingBufferSize) so any value is safe to pass in.
        size_t GetBeatFrame(size_t ring_index) const
        {
            return beat_frame_tracker_[ring_index % kTrackingBufferSize];
        }

    private:
        static constexpr size_t kTrackingBufferSize{256};

        float mono_env_{0.0f};
        float prev_mono_env_{0.0f};

        size_t frame_index_{0};
        size_t beat_frame_tracker_[kTrackingBufferSize];
        // Envelope follower
        EnvFollower env_follower_l_{0.75, 0.99};
        EnvFollower env_follower_r_{0.75, 0.99};

        float low_to_high_thres = {0.3};
        float high_to_low_thres = {0.15};

        BeatState state_{BeatState::kArmed};
    };

    class GlitchEngine
    {
    public:
        void OnNewFile(size_t file_sample_rate, size_t file_duration);
        void ProcessFrame(audio_frame_t &input, audio_frame_t &output, size_t n_frames, size_t frame_index);
        bool IsGlitchFetchRequired();
        bool IsPitchModRequired();
        float GetNextPlaybackSpeed();
        float GetPreviousPlaybackSpeed();
        size_t GlitchFetchFramePosition();
        void SetBitcrushEnable(bool enable);
        void SetSampleRateReduction(int value);
        void SetReductionFactor(int value);
        void SetStutterProbability(float value);
        void SetPlaybackSpeed(float value);
        void EnableNoiseOutput(bool enable);
        void EnablePitchMod(bool enable);
        void SetPitchModProbability(float value);
        void SavePreviousPlaybackSpeed(float prev_speed);

    private:
        // ---- Meta parameter
        float amount_;

        // ---- local file parameters
        size_t file_duration_;
        size_t file_sample_rate_;

        // ---- Bitcrush parameters
        bool bitcrush_enabled_{false};
        float Quantize(float sample) const;
        static constexpr int kMinSampleRateReduction{1};
        static constexpr int kMinReductionFactor{1};
        int reduction_factor_{0};
        int sample_rate_reduction_{kMinSampleRateReduction};

        float held_sample_l_ = 0.0f;
        float held_sample_r_ = 0.0f;
        size_t sample_counter_ = 0;

        // --- Beat tracking and repeat
        BeatTracker tracker_;
        float stutter_probability_{0.0f};
        // Set by IsGlitchFetchRequired() (called once per chunk from AudioPlayer::SeekStartChunk()
        // before ProcessFrame() runs) so ProcessFrame() can tell whether this chunk's audio came
        // from a fresh, linear read or from a glitch/beat-repeat jump to already-analyzed material.
        // See ProcessFrame(): beat tracking is skipped for glitch chunks, otherwise replaying a
        // detected beat would immediately re-detect and re-register it, flooding
        // BeatTracker::beat_frame_tracker_ with duplicates of the same few positions instead of a
        // diverse set of detected beats.
        bool is_glitch_chunk_{false};

        float playback_speed_{1.0f};
        float centered_noise_{0.0f};
        bool enable_noise_output_{false};
        // Counts samples since the last noise recomputation so ProcessFrame() can hold
        // (repeat) the current bipolar noise value for N samples when playback_speed_ < 1,
        // instead of recomputing a brand new random value on every sample.
        size_t noise_repeat_counter_{0};

        // ---- lfsr

        Lfsr rnd_{0xACE1ACE1};

        // ---- pitch mod traking

        bool pitch_mod_enabled_{false};
        float pitch_mod_probability_{0.8f};
        float previous_playback_speed_{1.0f};

        // Delay
        static constexpr size_t kBufferLen = 4096 * 2;
        float buffer_left_[kBufferLen];
        float buffer_right_[kBufferLen];

        float wet_mix_{0.9};
        float feedback_{0.99};
        size_t left_write_index_{0};
        size_t right_write_index_{0};
        float ProcessDelay(float input, bool left_channel);
        // ---- constants
        // Samples are expected in the [-1.0, 1.0] range, mirroring int16 PCM data. Scale up to
        // int16 range to reproduce the original integer quantization, then scale back down.
        static constexpr float kInt16Max = 32767.0f;

        // 2^32, normalizes Lfsr::Process()'s uint32_t output to a uniform float in [0, 1) for
        // probability rolls (see IsGlitchFetchRequired()).
        static constexpr float kLfsrMax = 4294967296.0f;
        static constexpr float kHalfUint32Range = 2147483648.0f; // 2^31
    };
} // namespace app::audio