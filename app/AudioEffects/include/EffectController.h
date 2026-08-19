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
        size_t selected_effect_{static_cast<size_t>(EffectId::kCloudsReverb)};
    };
}