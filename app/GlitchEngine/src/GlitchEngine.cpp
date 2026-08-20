#include "GlitchEngine.h"
#include <cstdio>
void app::audio::GlitchEngine::OnNewFile(size_t file_sample_rate, size_t file_duration)
{
    tracker_.Reset();

    file_duration_ = file_duration;
    file_sample_rate_ = file_sample_rate;
}

void app::audio::GlitchEngine::ProcessFrame(audio_frame_t &input, audio_frame_t &output, size_t n_frames, size_t frame_index)
{
    int downsample = sample_rate_reduction_;
    for (size_t i = 0; i < n_frames; i++)
    {
        // Beat tracking: skipped for glitch/beat-repeat chunks -- that audio is a replay of
        // already-analyzed material, so feeding it back in would just re-detect and re-register
        // the same handful of positions over and over (see is_glitch_chunk_'s comment).
        if (!is_glitch_chunk_)
        {
            tracker_.Process(input.audio_l[i], input.audio_r[i], frame_index + i);
        }

        if (is_glitch_chunk_ && enable_noise_output_)
        {
            // playback_speed_ < 1 slows down the noise: hold each computed value for N
            // samples instead of drawing a fresh one every sample, where N grows as
            // playback_speed_ shrinks. playback_speed_ >= 1 (or invalid/non-positive) draws
            // a fresh value every sample.
            size_t repeat_count = 1;
            if (playback_speed_ > 0.0f && playback_speed_ < 1.0f)
            {
                repeat_count = static_cast<size_t>(1.0f / playback_speed_ + 0.5f);
                if (repeat_count < 1)
                {
                    repeat_count = 1;
                }
            }

            if (noise_repeat_counter_ % repeat_count == 0)
            {
                uint32_t value = rnd_.Process();
                float centered = static_cast<float>(value) - kHalfUint32Range; // [-2^31, 2^31)
                centered_noise_ = centered / kHalfUint32Range;                 // [-1.0, 1.0)
            }

            ++noise_repeat_counter_;
            output.audio_l[i] = ProcessDelay(input.audio_l[i], true);
            output.audio_r[i] = ProcessDelay(output.audio_r[i], false);
            // centered_noise_ = ProcessDelay(true)
            // output.audio_l[i] = centered_noise_;
            // output.audio_r[i] = centered_noise_;
        }

        if (is_glitch_chunk_ && bitcrush_enabled_)
        {

            // Bitcrushing
            if (sample_counter_ % downsample == 0)
            {
                held_sample_l_ = Quantize(input.audio_l[i]);
                held_sample_r_ = Quantize(input.audio_r[i]);
            }

            output.audio_l[i] = held_sample_l_;
            output.audio_r[i] = held_sample_r_;

            ++sample_counter_;
        }
        rnd_.Process();
    }
}

bool app::audio::GlitchEngine::IsGlitchFetchRequired()
{
    if (stutter_probability_ <= 0.0f)
    {
        is_glitch_chunk_ = false;
        return false;
    }

    // Convert the LFSR's next 32-bit value into a uniform value in [0, 1) and roll it against
    // stutter_probability_: the higher the probability, the more chunks get diverted to a
    // random previously-detected beat position instead of continuing playback normally.
    const float random_unit = static_cast<float>(rnd_.Process()) / kLfsrMax;
    is_glitch_chunk_ = random_unit < stutter_probability_;
    return is_glitch_chunk_;
}

bool app::audio::GlitchEngine::IsPitchModRequired()
{
    if (pitch_mod_enabled_)
    {
        const float random_unit = static_cast<float>(rnd_.Process()) / kLfsrMax;
        return random_unit < pitch_mod_probability_;
    }
    return false;
}

float app::audio::GlitchEngine::GetNextPlaybackSpeed()
{

    const float random_unit = static_cast<float>(rnd_.Process()) / kLfsrMax;

    printf("Random Playback speed: %f\n", random_unit);

    return random_unit;
}
float app::audio::GlitchEngine::GetPreviousPlaybackSpeed()
{
    return previous_playback_speed_;
}
size_t app::audio::GlitchEngine::GlitchFetchFramePosition()
{
    const size_t last_frame_index = tracker_.GetLastFrameIndex();
    if (last_frame_index == 0)
    {
        // No beat has been detected yet (e.g. glitch enabled before any transient fired):
        // avoid a modulo-by-zero crash, nothing to pick from yet.
        return 0;
    }

    // beat_frame_tracker_ holds valid entries at ring indices [1, last_frame_index] (index 0 is
    // always BeatTracker::Reset()'s sentinel zero, never a real detected beat -- see
    // BeatTracker::Process()). Pick among those and look up the actual file frame position it
    // recorded, rather than returning the ring index itself.
    const size_t ring_index = 1 + (static_cast<size_t>(rnd_.Process()) % last_frame_index);
    size_t frame_pos = tracker_.GetBeatFrame(ring_index);
    // can be extended later on
    return frame_pos;
}

void app::audio::GlitchEngine::SetBitcrushEnable(bool enable)
{
    bitcrush_enabled_ = enable;
}

void app::audio::GlitchEngine::SetSampleRateReduction(int value)
{
    if (value < kMinSampleRateReduction)
    {
        sample_rate_reduction_ = kMinSampleRateReduction;
        return;
    }

    sample_rate_reduction_ = value;
}

void app::audio::GlitchEngine::SetReductionFactor(int value)
{
    reduction_factor_ = value;
}

void app::audio::GlitchEngine::SetStutterProbability(float value)
{
    if (value < 0.0f)
    {
        stutter_probability_ = 0.0f;
        return;
    }
    if (value > 1.0f)
    {
        stutter_probability_ = 1.0f;
        return;
    }
    stutter_probability_ = value;
}

void app::audio::GlitchEngine::SetPlaybackSpeed(float value)
{
    playback_speed_ = value;
}

void app::audio::GlitchEngine::EnableNoiseOutput(bool enable)
{
    enable_noise_output_ = enable;
}

void app::audio::GlitchEngine::EnablePitchMod(bool enable)
{
    pitch_mod_enabled_ = enable;
}

void app::audio::GlitchEngine::SetPitchModProbability(float value)
{

    if (value < 0.0f)
    {
        pitch_mod_probability_ = 0.0f;
        return;
    }
    if (value > 1.0f)
    {
        pitch_mod_probability_ = 1.0f;
        return;
    }
    pitch_mod_probability_ = value;
}

void app::audio::GlitchEngine::SavePreviousPlaybackSpeed(float prev_speed)
{
    previous_playback_speed_ = prev_speed;
}

float app::audio::GlitchEngine::Quantize(float sample) const
{
    if (reduction_factor_ < kMinReductionFactor)
    {
        return sample;
    }

    int32_t scaled = static_cast<int32_t>(sample * kInt16Max);
    scaled = (scaled / reduction_factor_) * reduction_factor_;
    return static_cast<float>(scaled) / kInt16Max;
}

float app::audio::GlitchEngine::ProcessDelay(float input, bool left_channel)
{

    float delayed_sample = 0;
    if (left_channel)
    {
        delayed_sample = buffer_left_[left_write_index_];
        // buffer_left_[left_write_index_] = input;
        buffer_left_[left_write_index_] = (1.0f - feedback_) * input + feedback_ * delayed_sample;
        left_write_index_ = (left_write_index_ + 1) % kBufferLen;
    }
    else
    {
        delayed_sample = buffer_right_[right_write_index_];
        // buffer_right_[right_write_index_] = input;
        buffer_right_[right_write_index_] = (1.0f - feedback_) * input + feedback_ * delayed_sample;
        right_write_index_ = (right_write_index_ + 1) % kBufferLen;
    }
    return (1.0f - wet_mix_) * input + wet_mix_ * delayed_sample;
}
