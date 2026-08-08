#include "EffectController.h"

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
    switch (selected_effect_)
    {
    case static_cast<size_t>(EffectId::kDistorsion):
        dist_.Process(input.audio_l,input.audio_r,output.audio_l,output.audio_r,n_frames);
        break;
        case static_cast<size_t>(EffectId::kDelay):
        delay_.Process(input.audio_l,input.audio_r,output.audio_l,output.audio_r,n_frames);
        break;
    default:
        break;
    }
}
