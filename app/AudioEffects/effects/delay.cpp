#include "delay.h"

int app::audio::Delay::Init()
{ // Nothing to initialize
    return 0;
}

void app::audio::Delay::Process(float *input_l, float *input_r, float *output_l, float *output_r, size_t n_frames)
{
    // TODO: implement delay line processing.
    for (size_t i = 0; i < n_frames; i++)
    {
        output_l[i] = ProcessSample(input_l[i], true);
        output_r[i] = ProcessSample(input_r[i], false);
    }
}

int app::audio::Delay::UpdateParameter(size_t parameter_id, float parameter_value)
{
    if (parameter_id == delay_time_ms_.id)
    {
        delay_time_ms_.value = parameter_value;
        // Clamp to 0 before converting: a negative float cast to uint32_t would wrap around to
        // a huge value instead of saturating at 0.
        delay_time_ms_value_ = (parameter_value > 0.0f) ? static_cast<uint32_t>(parameter_value) : 0;
    }
    else if (parameter_id == feedback_.id)
    {
        feedback_.value = parameter_value;
    }

    return 0;
}

float app::audio::Delay::ProcessSample(float input, bool left_channel)
{
    float delayed_sample = 0;
    if (left_channel)
    {
        delayed_sample = buffer_left_[left_write_index_];
        // buffer_left_[left_write_index_] = input;
        buffer_left_[left_write_index_] = (1.0f - feedback_.value) * input + feedback_.value * delayed_sample;
        ;
        left_write_index_ = (left_write_index_ + 1) % kBufferLen;
    }
    else
    {
        delayed_sample = buffer_right_[right_write_index_];
        // buffer_right_[right_write_index_] = input;
        buffer_right_[right_write_index_] = (1.0f - feedback_.value) * input + feedback_.value * delayed_sample;
        right_write_index_ = (right_write_index_ + 1) % kBufferLen;
    }

    return (1.0f - wet_mix_.value) * input + wet_mix_.value * delayed_sample;
}
