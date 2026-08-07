/*
 * @file AudioTypes.h
 * @brief Common audio data types shared across the app::audio namespace (AudioPlayer,
 * AudioEffects, wav parsing, etc).
 */

#pragma once

#include <cstddef>

namespace app::audio
{
    typedef struct
    {
        float *audio_l;
        float *audio_r;
        size_t n_frames;
    } audio_frame_t;
} // namespace app::audio
