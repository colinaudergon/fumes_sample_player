#pragma once
#include <cstddef>
#include <cstdint>
#include "../../include/IAudioEffect.h"

namespace app::audio
{
    class BitCrusher : public IAudioEffect
    {
    public:
        int Init() override;
        void Process(float *input_l, float *input_r, float *output_l, float *output_r, size_t n_frames) override;
        int UpdateParameter(size_t parameter_id, float parameter_value) override;

    private:
        float Quantize(float sample) const;

        static constexpr size_t kReductionFactorParameterId = 1;
        static constexpr size_t kSampleRateReductionParameterId = 2;

        // Reduction factor quantizes the sample amplitude (bit depth reduction). A value of 1
        // leaves the signal untouched; higher values coarsen the resolution.
        AudioEffectParameter reduction_factor_ = {
            .id = kReductionFactorParameterId,
            .value = 128.0f,
        };

        // Number of samples over which a single value is held (sample rate reduction /
        // decimation). A value of 1 leaves the signal untouched.
        AudioEffectParameter sample_rate_reduction_ = {
            .id = kSampleRateReductionParameterId,
            .value = 64.0f,
        };

        // Sample-and-hold state, kept across Process() calls since n_frames may not be a
        // multiple of the current downsample factor. Both channels share the same sample
        // counter since they are decimated in lockstep (same frame index).
        float held_sample_l_ = 0.0f;
        float held_sample_r_ = 0.0f;
        size_t sample_counter_ = 0;
    };
} // namespace app::audio
