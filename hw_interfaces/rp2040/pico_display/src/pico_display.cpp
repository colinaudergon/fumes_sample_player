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

    void PicoDisplay::RenderLines(const char *line1, const char *line2)
    {
        u8g2_SetFont(&u8g2_, u8g2_font_6x13_tr);
        const int line_height = u8g2_GetMaxCharHeight(&u8g2_);

        u8g2_ClearBuffer(&u8g2_);
        u8g2_DrawStr(&u8g2_, 0, line_height, line1);
        if (line2 != nullptr)
        {
            u8g2_DrawStr(&u8g2_, 0, line_height * 2 + 2, line2);
        }
        u8g2_SendBuffer(&u8g2_);
    }

    int PicoDisplay::ShowText(const char *text)
    {
        if (!initialized_ || text == nullptr)
        {
            return -1;
        }

        RenderLines(text, nullptr);
        return 0;
    }

    int PicoDisplay::DisplayFileInfo(const char *file_name, uint32_t duration_ms)
    {
        if (!initialized_ || file_name == nullptr)
        {
            return -1;
        }

        char duration_line[32];

        const uint32_t total_seconds = duration_ms / 1000;
        const uint32_t minutes = total_seconds / 60;
        const uint32_t seconds = total_seconds % 60;

        std::snprintf(duration_line, sizeof(duration_line), "%lum%02lus", static_cast<unsigned long>(minutes),
                      static_cast<unsigned long>(seconds));

        RenderLines(file_name, duration_line);
        return 0;
    }

} // namespace hw_interface
