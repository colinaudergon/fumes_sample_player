#include "distorsion.h"

int app::audio::Distorsion::Init()
{ // Nothing to initialize
    return 0;
}

void app::audio::Distorsion::Process(float *input_l, float *input_r, float *output_l, float *output_r, size_t n_frames)
{
    for (size_t i = 0; i < n_frames; i++)
    {
        if (input_r[i] > threshold_lvl_.value)
        {
            output_r[i] = threshold_lvl_.value;
        }

        else if(input_r[i] < neg_threshold)
        {
            output_r[i] =  neg_threshold;
        }

        if (input_l[i] > threshold_lvl_.value)
        {
            output_l[i] = threshold_lvl_.value;
        }

        else if(input_l[i] < neg_threshold)
        {
            output_l[i] =  neg_threshold;
        }
    }
}

int app::audio::Distorsion::UpdateParameter(size_t parameter_id, float parameter_value)
{
    if (parameter_id == threshold_lvl_.id)
    {
        threshold_lvl_.value = parameter_value;
        neg_threshold = threshold_lvl_.value * (-1.0f);
    }

    return 0;
}