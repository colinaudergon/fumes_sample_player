#include "pico_display.h"

#include <cstdio>

#include "pico_display_u8x8_spi1.h"

namespace hw_interface
{

    PicoDisplay::PicoDisplay()
        : PicoDisplay(Config{})
    {
    }

    PicoDisplay::PicoDisplay(const Config &config)
        : config_(config)
    {
    }

    int PicoDisplay::Init()
    {
        const pico_u8x8_spi1_config_t spi_config{
            config_.sck_gpio,
            config_.mosi_gpio,
            config_.cs_gpio,
            config_.dc_gpio,
            config_.reset_gpio,
            config_.spi_baudrate_hz};
        // Must happen before u8g2_InitDisplay(): u8x8_gpio_and_delay_pico_spi1() reads this
        // configuration when it handles U8X8_MSG_GPIO_AND_DELAY_INIT below.
        PicoU8x8Spi1Configure(&spi_config);

        u8g2_Setup_ssd1306_128x64_noname_f(&u8g2_, U8G2_R0, u8x8_byte_pico_hw_spi1, u8x8_gpio_and_delay_pico_spi1);
        u8g2_InitDisplay(&u8g2_);
        u8g2_SetPowerSave(&u8g2_, 0); // Wake the display up (it starts in power-save mode).

        u8g2_ClearBuffer(&u8g2_);
        u8g2_SendBuffer(&u8g2_);

        initialized_ = true;
        return 0;
    }

    void PicoDisplay::RenderLine(const char *line)
    {
        if (line == nullptr)
        {
            return;
        }
        u8g2_SetFont(&u8g2_, u8g2_font_6x13_tr);
        const int line_height = u8g2_GetMaxCharHeight(&u8g2_);
        u8g2_DrawStr(&u8g2_, 0, line_height, line);
        u8g2_SendBuffer(&u8g2_);
    }

    void PicoDisplay::ClearArea(const DisplayArea &area)
    {
        u8g2_SetDrawColor(&u8g2_, kUg8Black);
        u8g2_DrawBox(&u8g2_, area.x_pos, area.y_pos, area.width, area.height);
        u8g2_SetDrawColor(&u8g2_, kUg8White);
    }

    int PicoDisplay::ShowText(const char *text)
    {
        if (!initialized_ || text == nullptr)
        {
            return -1;
        }

        RenderLine(text);
        return 0;
    }

    int PicoDisplay::DisplayFileInfo(const char *file_name, uint32_t duration_ms)
    {
        if (!initialized_ || file_name == nullptr)
        {
            return -1;
        }

        (void)duration_ms; // currently unused

        RenderLine(file_name);
        return 0;
    }

    int PicoDisplay::DisplayAudioBufferContent(float *audio_left, float *audio_right, size_t n_frames)
    {
        if (!initialized_ || audio_left == nullptr || audio_right == nullptr || n_frames == 0)
        {
            return -1;
        }

        // 43 pxl by 128 waveform area; pxl (0, 64-43) is the top left corner of the area.

        ClearArea(kLiveViewArea);

        // Combine audio_left and audio_right into a single mono buffer of size 128
        // ((left[i] + right[i]) / 2), stepping through n_frames by 2 to fit the area's width.
        constexpr size_t kStep = 2;
        for (size_t x = 0; x < kLiveViewArea.width; ++x)
        {
            size_t frame_index = x * kStep;
            if (frame_index >= n_frames)
            {
                break;
            }

            float mono_sample = (audio_left[frame_index] + audio_right[frame_index]) / 2.0f;

            // Clamp to the expected [-1, 1] range so an out-of-range sample can't map outside
            // the waveform area below.
            if (mono_sample > 1.0f)
            {
                mono_sample = 1.0f;
            }
            else if (mono_sample < -1.0f)
            {
                mono_sample = -1.0f;
            }

            uint8_t mapped_height = static_cast<uint8_t>((mono_sample + 1.0f) / 2.0f * (kLiveViewArea.height- 1));
            uint8_t y = kLiveViewArea.y_pos + (kLiveViewArea.height - 1 - mapped_height);

            u8g2_DrawPixel(&u8g2_, kLiveViewArea.x_pos + static_cast<uint8_t>(x), y);
        }

        u8g2_SendBuffer(&u8g2_);
        return 0;
    }

} // namespace hw_interface
