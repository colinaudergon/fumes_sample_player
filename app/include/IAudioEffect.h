#pragma once

#include <cstddef>

namespace app
{

    struct AudioEffectParameter
    {
        size_t id;
        float value;
    };
    
    class IAudioEffect
    {
    public:
        virtual ~IAudioEffect() = default;
        virtual int Init() = 0;
        virtual void Process(float *input_l, float *input_r, float *output_l, float *output_r, size_t n_frames) = 0;
        virtual int UpdateParameter(size_t parameter_id, float parameter_value) = 0;
    };
}
