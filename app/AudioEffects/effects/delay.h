#pragma once
#include <cstddef>
#include <cstdint>
#include "../../include/IAudioEffect.h"
namespace app::audio
{

    class Delay : public IAudioEffect
    {
    public:
        int Init() override;
        void Process(float *input_l, float *input_r, float *output_l, float *output_r, size_t n_frames) override;
        int UpdateParameter(size_t parameter_id, float parameter_value) override;
        private:
        float ProcessSample(float input, bool left_channel);
        AudioEffectParameter delay_time_ms_ = {
            .id = 0,
            .value = 0
        };
        // The generic interface exchanges parameters as float (AudioEffectParameter::value), but
        // the delay line itself needs a whole number of milliseconds/samples to index its
        // buffer. Kept in sync with delay_time_ms_.value inside UpdateParameter() rather than
        // changing AudioEffectParameter's type, which would break other effects (e.g.
        // Distorsion's threshold/mix levels) that are legitimately fractional.
        uint32_t delay_time_ms_value_ = 0;
        
        AudioEffectParameter feedback_ = {
            .id = 1,
            .value = 0.9,
        };

        AudioEffectParameter wet_mix_ = {
            .id = 2,
            // .value = 0.99
            .value = 0.9
        };

        size_t left_write_index_{0};
        size_t right_write_index_{0};

        static constexpr size_t kBufferLen = 4096*2;
        float buffer_left_[kBufferLen];
        float buffer_right_[kBufferLen];

    };
} // namespace app::audio_
