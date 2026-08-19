/**
 * @file pico_display_u8x8_spi1.h
 * @brief u8x8 byte/gpio-and-delay callback pair wiring u8g2 to the RP2040's SPI1 peripheral,
 * ported from https://github.com/mwinters-stuff/u8g2-raspberrypi-pico-cpp-sdk-play's
 * u8g2functions.c/.h (which targets SPI0) to SPI1, with the hardcoded pin #defines replaced by
 * a runtime-configurable struct so multiple PicoDisplay instances/pin layouts don't require
 * recompiling this file.
 *
 * u8g2's u8x8_msg_cb typedef takes no user-data argument, so PicoU8x8Spi1Configure() must be
 * called before u8g2_InitDisplay() (which triggers U8X8_MSG_GPIO_AND_DELAY_INIT) to supply the
 * pin/SPI configuration these callbacks use.
 */

#pragma once

#include <stdint.h>

#include "pico/stdlib.h"

#include "u8g2.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        uint sck_gpio;
        uint mosi_gpio;
        uint cs_gpio;
        uint dc_gpio;
        uint reset_gpio;
        uint32_t spi_baudrate_hz;
    } pico_u8x8_spi1_config_t;

    /// @brief Stores `config` for use by the callbacks below. Must be called once before
    /// u8g2_InitDisplay(). Ignored (no-op) if `config` is null.
    void PicoU8x8Spi1Configure(const pico_u8x8_spi1_config_t *config);

    uint8_t u8x8_byte_pico_hw_spi1(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
    uint8_t u8x8_gpio_and_delay_pico_spi1(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

#ifdef __cplusplus
}
#endif
