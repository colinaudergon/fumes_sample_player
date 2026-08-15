/**
 * @file pico_display.h
 * @brief hw_interface::IDisplay implementation driving a u8g2-supported SPI display
 * (SSD1306 128x64, SPI1) on the RP2040, built on the u8g2 library
 * (https://github.com/olikraus/u8g2, vendored at lib/u8g2) and the SPI1 u8x8 glue in
 * pico_display_u8x8_spi1.h -- ported from
 * https://github.com/mwinters-stuff/u8g2-raspberrypi-pico-cpp-sdk-play.
 */

#pragma once

#include <cstdint>

#include "pico/stdlib.h"

#include "u8g2.h"

#include "IDisplay.h"

namespace hw_interface
{

    class PicoDisplay : public IDisplay
    {
    public:
        /// @brief GPIO/SPI configuration for the display. Defaults use SPI1 on GPIO26 (SCK) /
        /// GPIO27 (MOSI), with CS/DC/RESET as plain GPIO outputs on GPIO22/21/20 -- chosen to
        /// avoid the GPIOs already claimed elsewhere in this proje ct (status LED: GPIO0; SD
        /// card SPI0: GPIO4/6/7/8; PWM audio out: GPIO10/12).
        struct Config
        {
            uint sck_gpio = 14;
            uint mosi_gpio = 15;
            uint cs_gpio = 1;
            uint dc_gpio = 3;
            uint reset_gpio = 2;
            uint32_t spi_baudrate_hz = 2000000;
        };

        PicoDisplay();
        explicit PicoDisplay(const Config &config);
        ~PicoDisplay() override = default;

        /// @brief Configures SPI1 + GPIOs (via pico_display_u8x8_spi1.h) and brings up the
        /// display through u8g2.
        /// @return 0 on success. u8g2's own init path has no failure signaling, so this always
        /// succeeds once called; kept non-void to match IDisplay::Init()'s contract.
        int Init() override;

        /// @brief Clears the display and draws `text` as a single left-aligned line.
        int ShowText(const char *text) override;

        /// @brief Clears the display and draws `file_name` and a "Duration: <ms> ms" line
        /// beneath it.
        int DisplayFileInfo(const char *file_name, uint32_t duration_ms) override;

    private:
        Config config_;
        u8g2_t u8g2_{};
        bool initialized_ = false;

        /// @brief Clears the buffer, draws up to two stacked lines (line2 skipped if null), and
        /// flushes to the display. Shared by ShowText()/DisplayFileInfo().
        void RenderLines(const char *line1, const char *line2);
    };

} // namespace hw_interface
