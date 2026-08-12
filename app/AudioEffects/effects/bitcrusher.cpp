#include "bitcrusher.h"
#include <cmath>

int app::audio::BitCrusher::Init()
{ // Nothing to initialize
    return 0;
}

float app::audio::BitCrusher::Quantize(float sample) const
{
    int reduction_factor = static_cast<int>(reduction_factor_.value);
    if (reduction_factor <= 1)
    {
        return sample;
    }

    // Samples are expected in the [-1.0, 1.0] range, mirroring int16 PCM data. Scale up to
    // int16 range to reproduce the original integer quantization, then scale back down.
    constexpr float kInt16Max = 32767.0f;
    int32_t scaled = static_cast<int32_t>(sample * kInt16Max);
    scaled = (scaled / reduction_factor) * reduction_factor;
    return static_cast<float>(scaled) / kInt16Max;
}

void app::audio::BitCrusher::Process(float *input_l, float *input_r, float *output_l, float *output_r, size_t n_frames)
{
    int downsample = static_cast<int>(sample_rate_reduction_.value);
    if (downsample < 1)
    {
        downsample = 1;
    }

    for (size_t i = 0; i < n_frames; i++)
    {
        // Sample-and-hold logic: update the held samples only every 'downsample' steps.
        if (sample_counter_ % downsample == 0)
        {
            held_sample_l_ = Quantize(input_l[i]);
            held_sample_r_ = Quantize(input_r[i]);
        }

        output_l[i] = held_sample_l_;
        output_r[i] = held_sample_r_;

        ++sample_counter_;
    }
}

int app::audio::BitCrusher::UpdateParameter(size_t parameter_id, float parameter_value)
{
    if (parameter_id == reduction_factor_.id)
    {
        reduction_factor_.value = parameter_value;
    }
    else if (parameter_id == sample_rate_reduction_.id)
    {
        sample_rate_reduction_.value = parameter_value;
    }

    return 0;
}
