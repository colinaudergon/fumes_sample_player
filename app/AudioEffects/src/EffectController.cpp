#include "EffectController.h"
#include <algorithm>

app::audio::EffectController::EffectController()
{
    dist_.Init();
    delay_.Init();
    cloud_reverb_.Init();
    bit_crusher_.Init();
}

void app::audio::EffectController::SelectEffect(size_t effect_index)
{
    selected_effect_ = effect_index;
    if (selected_effect_ > kNumberOfEffects)
    {
        selected_effect_ = kNumberOfEffects;
    }
}

void app::audio::EffectController::Process(wav::audio_frame_t &input, wav::audio_frame_t &output, size_t n_frames)
{
    // Safety clamp: scratch buffers are sized for the largest n_frames ever requested per
    // callback (see kMaxChainFrames). This should never actually trigger.
    if (n_frames > kMaxChainFrames)
    {
        n_frames = kMaxChainFrames;
    }

    float *stage_in_l = input.audio_l;
    float *stage_in_r = input.audio_r;
    size_t processed_count = 0;

    for (size_t i = 0; i < kNumMaxChainedEffect; i++)
    {
        EffectId effect = chained_effect_[i];
        if (effect == EffectId::kNone)
        {
            break;
        }

        // Ping-pong between two scratch buffers so each stage reads the previous stage's
        // output rather than the original input.
        float *stage_out_l = (processed_count % 2 == 0) ? scratch_a_l_ : scratch_b_l_;
        float *stage_out_r = (processed_count % 2 == 0) ? scratch_a_r_ : scratch_b_r_;

        switch (effect)
        {
        case EffectId::kDistorsion:
            dist_.Process(stage_in_l, stage_in_r, stage_out_l, stage_out_r, n_frames);
            break;
        case EffectId::kDelay:
            delay_.Process(stage_in_l, stage_in_r, stage_out_l, stage_out_r, n_frames);
            break;
        case EffectId::kCloudsReverb:
            cloud_reverb_.Process(stage_in_l, stage_in_r, stage_out_l, stage_out_r, n_frames);
            break;
        case EffectId::kBitCrusher:
            bit_crusher_.Process(stage_in_l, stage_in_r, stage_out_l, stage_out_r, n_frames);
            break;
        case EffectId::kNone:
        default:
            continue;
        }

        stage_in_l = stage_out_l;
        stage_in_r = stage_out_r;
        ++processed_count;
    }

    if (processed_count == 0)
    {
        return;
    }

    std::copy(stage_in_l, stage_in_l + n_frames, output.audio_l);
    std::copy(stage_in_r, stage_in_r + n_frames, output.audio_r);
}
