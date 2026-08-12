/**
 * @file EffectController.h
 * @brief
 */

#pragma once

#include "wav_file_handler.h"
#include "../../include/IAudioEffect.h"
#include "effects/distorsion.h"
#include "effects/delay.h"
#include "effects/clouds_reverb.h"
#include "effects/bitcrusher.h"

namespace app::audio
{

    enum class EffectId: uint8_t
    {
        kNone,
        kDistorsion,
        kDelay,
        kCloudsReverb,
        kBitCrusher
    };

    class EffectController
    {
    public:
        EffectController();
        ~EffectController() {};
        void SelectEffect(size_t effect_index);
        void Process(wav::audio_frame_t &input, wav::audio_frame_t &output, size_t n_frames);

    private:
        Distorsion dist_;
        Delay delay_;
        CloudsReverb cloud_reverb_;
        BitCrusher bit_crusher_;
        static constexpr size_t kNumberOfEffects = 4;
        size_t selected_effect_{3};

        static constexpr size_t kNumMaxChainedEffect = 4;
        EffectId chained_effect_[kNumMaxChainedEffect] = {EffectId::kBitCrusher,EffectId::kCloudsReverb,EffectId::kNone,EffectId::kNone};

        // Chaining effects means each stage must read the *previous* stage's output rather than
        // the original input (see EffectController::Process). Since IAudioEffect::Process takes
        // separate input/output pointers, we ping-pong between two scratch buffers instead of
        // relying on effects supporting in-place processing. Sized to match
        // AudioPlayer::kMaxOutputFrames, the largest n_frames ever requested per callback.
        static constexpr size_t kMaxChainFrames = 4096;
        float scratch_a_l_[kMaxChainFrames];
        float scratch_a_r_[kMaxChainFrames];
        float scratch_b_l_[kMaxChainFrames];
        float scratch_b_r_[kMaxChainFrames];
    };
}