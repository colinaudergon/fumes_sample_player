/**
 * @file display.h
 * @brief display
 */

#pragma once

#include <cstddef>

#include "IDisplay.h"

namespace hw_interface
{

    class Display : public IDisplay
    {
    public:
        static constexpr size_t kDefaultHeight = 20;

        explicit Display(size_t height = kDefaultHeight);
        ~Display() override = default;

        int Init() override;
        /// @brief Renders buffer as an ASCII waveform in the terminal: one column per sample,
        /// one row printed per line, '*' where a column's amplitude reaches that row and ' '
        /// otherwise (rows printed top-down, tallest/loudest samples reach the topmost rows).
        void ShowWave(float *buffer, size_t buffer_size);
        int ShowText(const char* text) override;
        int DisplayFileInfo(const char* file_name, uint32_t duration_ms) override;
        int DisplayAudioBufferContent(float *audio_left, float *audio_right, size_t n_frames) override;
    private:
        size_t display_height_;
    };

} // namespace hw_interface1