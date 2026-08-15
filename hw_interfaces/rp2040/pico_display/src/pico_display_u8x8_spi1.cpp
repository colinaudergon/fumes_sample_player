#include "pico_display_u8x8_spi1.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"

namespace
{
    // Sensible defaults (see PicoDisplay::Config) in case PicoU8x8Spi1Configure() is never
    // called; overwritten by whatever PicoDisplay::Init() passes in.
    pico_u8x8_spi1_config_t g_config{
        /*sck_gpio=*/26,
        /*mosi_gpio=*/27,
        /*cs_gpio=*/22,
        /*dc_gpio=*/21,
        /*reset_gpio=*/20,
        /*spi_baudrate_hz=*/4000000};
} // namespace

void PicoU8x8Spi1Configure(const pico_u8x8_spi1_config_t *config)
{
    if (config != nullptr)
    {
        g_config = *config;
    }
}

uint8_t u8x8_byte_pico_hw_spi1(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg)
    {
    case U8X8_MSG_BYTE_SEND:
        spi_write_blocking(spi1, static_cast<const uint8_t *>(arg_ptr), arg_int);
        break;
    case U8X8_MSG_BYTE_INIT:
        u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);
        break;
    case U8X8_MSG_BYTE_SET_DC:
        u8x8_gpio_SetDC(u8x8, arg_int);
        break;
    case U8X8_MSG_BYTE_START_TRANSFER:
        u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_enable_level);
        u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_DELAY_NANO, u8x8->display_info->post_chip_enable_wait_ns, nullptr);
        break;
    case U8X8_MSG_BYTE_END_TRANSFER:
        u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_DELAY_NANO, u8x8->display_info->pre_chip_disable_wait_ns, nullptr);
        u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);
        break;
    default:
        return 0;
    }
    return 1;
}

uint8_t u8x8_gpio_and_delay_pico_spi1(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    (void)arg_ptr;

    switch (msg)
    {
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        spi_init(spi1, g_config.spi_baudrate_hz);
        gpio_set_function(g_config.sck_gpio, GPIO_FUNC_SPI);
        gpio_set_function(g_config.mosi_gpio, GPIO_FUNC_SPI);
        // No MISO pin: the SSD1306 is a write-only SPI target, so RX is left unconfigured/free.

        gpio_init(g_config.cs_gpio);
        gpio_init(g_config.dc_gpio);
        gpio_init(g_config.reset_gpio);
        gpio_set_dir(g_config.cs_gpio, GPIO_OUT);
        gpio_set_dir(g_config.dc_gpio, GPIO_OUT);
        gpio_set_dir(g_config.reset_gpio, GPIO_OUT);
        // Chip select is active-low: drive it high (deselected) until a transfer starts.
        gpio_put(g_config.cs_gpio, 1);
        gpio_put(g_config.dc_gpio, 0);
        gpio_put(g_config.reset_gpio, 1);
        break;

    case U8X8_MSG_DELAY_NANO:
        // arg_int is in nanoseconds; RP2040's sleep_us() only has microsecond granularity, so
        // round up to the nearest whole microsecond. (The reference project this was ported
        // from instead did sleep_us(1000 * arg_int), which conflates nano- and milli-seconds
        // and oversleeps by ~1000x -- harmless there since it's only called around SPI
        // transfers, but corrected here.)
        if (arg_int > 0)
        {
            sleep_us((arg_int + 999) / 1000);
        }
        break;
    case U8X8_MSG_DELAY_100NANO:
        if (arg_int > 0)
        {
            sleep_us((arg_int * 100 + 999) / 1000);
        }
        break;
    case U8X8_MSG_DELAY_10MICRO:
        sleep_us(arg_int * 10);
        break;
    case U8X8_MSG_DELAY_MILLI:
        sleep_ms(arg_int);
        break;
    case U8X8_MSG_DELAY_I2C:
        break; // Not used: this display is SPI-only.

    case U8X8_MSG_GPIO_CS:
        gpio_put(g_config.cs_gpio, arg_int);
        break;
    case U8X8_MSG_GPIO_DC:
        gpio_put(g_config.dc_gpio, arg_int);
        break;
    case U8X8_MSG_GPIO_RESET:
        gpio_put(g_config.reset_gpio, arg_int);
        break;

    default:
        u8x8_SetGPIOResult(u8x8, 1); // Default return value for unhandled/unused messages.
        break;
    }
    return 1;
}
