
#pragma once
#include <cstddef>
#include <cstdint>
#include "../../include/IAudioEffect.h"

namespace app::audio
{
    class Distorsion : public IAudioEffect
    {
    public:
        int Init() override;
        void Process(float *input_l, float *input_r, float *output_l, float *output_r, size_t n_frames) override;
        int UpdateParameter(size_t parameter_id, float parameter_value) override;
    private:
        AudioEffectParameter mix_lvl_ = {
            .id = 0,
            .value = 0.0f};
        AudioEffectParameter threshold_lvl_ = {
            .id = 1,
            .value = 0.3f,
    };

    float neg_threshold = -0.3f;
};
} // namespace app::audio