#include "Display.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

hw_interface::Display::Display(size_t height)
    : display_height_(height > 0 ? height : kDefaultHeight)
{
}

int hw_interface::Display::Init()
{
    return 0;
}

void hw_interface::Display::ShowWave(float *buffer, size_t buffer_size)
{
    if (buffer == nullptr || buffer_size == 0)
    {
        return;
    }

    // Print top row (highest amplitude level) down to row 1 (baseline). For each column, print
    // '*' if that sample's bar reaches the current row, ' ' otherwise -- e.g. for
    // buffer = {1.0, 0.25, 0.5, 0.75, 0.25, 0.5, 1.0} and display_height_ = 4:
    //   *     *
    //   **   **
    //   *** ***
    //   *******
    for (size_t row = display_height_; row >= 1; --row)
    {
        for (size_t col = 0; col < buffer_size; col++)
        {
            const float amplitude = std::min(std::fabs(buffer[col]), 1.0f);
            const size_t bar_height = static_cast<size_t>(amplitude * static_cast<float>(display_height_) + 0.5f);
            std::putchar(bar_height >= row ? '*' : ' ');
        }
        std::putchar('\n');

        if (row == 1)
        {
            break; // row is unsigned: stop here instead of decrementing past 0.
        }
    }
}

int hw_interface::Display::ShowText(const char *text)
{
    if (text == nullptr)
    {
        return -1;
    }
    std::printf("%s",text);
    return 0;
}

int hw_interface::Display::DisplayFileInfo(const char *file_name, uint32_t duration_ms)
{
    if(file_name == nullptr)
    {
        return -1;
    }
    std::printf("Filename: %s\n",file_name);
    std::printf("Duration: %d\n",duration_ms);

    return 0;
}

// Matches PicoDisplay::DisplayAudioBufferContent -- both are placeholder stubs for now;
// no rendering (waveform/VU meter) has been wired up to this callback on either platform yet.
int hw_interface::Display::DisplayAudioBufferContent(float *audio_left, float *audio_right, size_t n_frames)
{
    (void)audio_left;
    (void)audio_right;
    (void)n_frames;
    return 0;
}
