#include "EffectController.h"

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
    switch (static_cast<EffectId>(selected_effect_))
    {
    case EffectId::kDistorsion:
        dist_.Process(input.audio_l, input.audio_r, output.audio_l, output.audio_r, n_frames);
        break;
    case EffectId::kDelay:
        delay_.Process(input.audio_l, input.audio_r, output.audio_l, output.audio_r, n_frames);
        break;
    case EffectId::kCloudsReverb:
        cloud_reverb_.Process(input.audio_l, input.audio_r, output.audio_l, output.audio_r, n_frames);
        break;
    case EffectId::kBitCrusher:
        bit_crusher_.Process(input.audio_l, input.audio_r, output.audio_l, output.audio_r, n_frames);
        break;
    case EffectId::kNone:
    default:
        break;
    }
}
