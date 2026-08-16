/**
 * @file IDisplay.h
 * @brief Display interface.
 */

#pragma once
#include <cstdint>

namespace hw_interface
{
    class IDisplay
    {
    public:
        virtual ~IDisplay() = default;
        virtual int Init() = 0;
        virtual int ShowText(const char *text) = 0;
        virtual int DisplayFileInfo(const char *file_name, uint32_t duration_ms) = 0;
        virtual int DisplayAudioBufferContent(float *audio_left, float *audio_right, size_t n_frames) = 0;
    };
} // namespace hw_interface