#pragma once
#include <cstddef>
#include <cstdint>
#include "../../include/IAudioEffect.h"
#include "mutable_instrument/mu_dsp.h"
#include "mutable_instrument/fx_engine.h"

namespace app::audio
{

    class CloudsReverb : public IAudioEffect
    {
    public:
        int Init() override;
        void Process(float *input_l, float *input_r, float *output_l, float *output_r, size_t n_frames) override;
        int UpdateParameter(size_t parameter_id, float parameter_value) override;

    private:
        // reverb_.set_amount(reverb_amount * 0.54f);
        // reverb_.set_diffusion(0.7f);
        // reverb_.set_time(0.35f + 0.63f * reverb_amount);
        // reverb_.set_input_gain(0.2f);
        // reverb_.set_lp(0.6f + 0.37f * feedback);
        // reverb_.Process(out_, size);

        typedef FxEngine<16384, FORMAT_12_BIT> E;
        E engine_;

        float lp_decay_1_;
        float lp_decay_2_;
        static constexpr size_t kAmountParameterId = 1;
        static constexpr size_t kDiffusionParameterId = 2;
        static constexpr size_t kTimeParameterId = 3;
        static constexpr size_t kInputGainParameterId = 4;
        static constexpr size_t kLpParameterId = 5;

        AudioEffectParameter amount_ = {
            .id = kAmountParameterId,
            .value = 0.54f,
        };
        AudioEffectParameter diffusion_ = {
            .id = kAmountParameterId,
            .value = 0.7f,
        };
        AudioEffectParameter time_ = {
            .id = kDiffusionParameterId,
            .value = 0.35f + 0.63f,
        };
        AudioEffectParameter input_gain_ = {
            .id = kInputGainParameterId,
            .value =0.2f,
        };
        AudioEffectParameter low_pass_ = {
            .id = kLpParameterId,
            .value = 0.6f + 0.37f,
        };
    };
} // namespace app::audio
