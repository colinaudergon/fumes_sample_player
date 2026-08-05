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

    private:
        size_t display_height_;
        static constexpr char* kDefaultStyle = "\033[0m";
        static constexpr char* kRedColorStyle = "\033[31m";

    };

} // namespace hw_interface