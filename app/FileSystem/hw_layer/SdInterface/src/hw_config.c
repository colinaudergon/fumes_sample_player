/**
 * @file hw_config.c
 * @brief Project-specific hardware configuration for the vendored
 * carlk3/no-OS-FatFS-SD-SPI-RPi-Pico SD-over-SPI driver (lib/no-OS-FatFS-SD-SPI-RPi-Pico).
 *
 * Implements the sd_get_num()/sd_get_by_num()/spi_get_num()/spi_get_by_num() functions
 * declared in the vendored hw_config.h. sd_init_driver() (called from
 * SdBlockDevice::Init(), see SdBlockDevice.cpp) uses these to discover which SPI
 * controller(s) and SD card(s) exist and to configure their GPIOs.
 *
 * IMPORTANT: The pin assignments and SPI baud rate below are a PLACEHOLDER, copied from the
 * vendored library's documented default wiring (see its README's SPI0 pinout table). They do
 * NOT reflect this project's actual hardware yet -- replace them with the real SD card socket
 * wiring once it is known, before relying on this on real hardware.
 */

#include "hw_config.h"

static spi_t spi0_config = {
    .hw_inst = spi0,
    .miso_gpio = 4,
    .mosi_gpio = 7,
    .sck_gpio = 6,
    // .baud_rate = 12500 * 1000, // 12.5 MHz, placeholder (see file header comment)
    .baud_rate = 20000 * 1000, // 25 MHz, placeholder (see file header comment)
};

static sd_card_t sd0_config = {
    .pcName = "0:",
    .spi = &spi0_config,
    .ss_gpio = 8,
    .use_card_detect = false, // No card-detect GPIO wired yet (placeholder hardware)
};

size_t sd_get_num() { return 1; }

sd_card_t *sd_get_by_num(size_t num)
{
    if (num == 0)
    {
        return &sd0_config;
    }
    return NULL;
}

size_t spi_get_num() { return 1; }

spi_t *spi_get_by_num(size_t num)
{
    if (num == 0)
    {
        return &spi0_config;
    }
    return NULL;
}
