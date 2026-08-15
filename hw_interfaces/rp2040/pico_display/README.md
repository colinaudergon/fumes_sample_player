# pico_display

`PicoDisplay` is an RP2040-specific `hw_interface::IDisplay` implementation
(see `hw_interfaces/include/IDisplay.h`) driving an SSD1306 128x64 SPI OLED via the
[u8g2](https://github.com/olikraus/u8g2) graphics library (vendored as a git submodule at
`lib/u8g2`), over the RP2040's **SPI1** peripheral.

Ported from
[mwinters-stuff/u8g2-raspberrypi-pico-cpp-sdk-play](https://github.com/mwinters-stuff/u8g2-raspberrypi-pico-cpp-sdk-play),
which wires u8g2 to SPI0 for a uc1701-based MKS MINI12864 panel. `pico_display_u8x8_spi1.h`/`.cpp`
is the SPI1 equivalent of that project's `u8g2functions.h`/`.c` (the u8x8 byte/gpio-and-delay
callback pair u8g2 calls into), with hardcoded pin `#define`s replaced by a runtime-configurable
struct (`pico_u8x8_spi1_config_t`) so pins aren't baked in at compile time.

## Wiring

| Pico pin (default `PicoDisplay::Config`) | SSD1306 pin |
| ----------------------------------------- | ----------- |
| GPIO26 (SPI1 SCK)                         | SCK/CLK     |
| GPIO27 (SPI1 TX)                          | SDA/MOSI    |
| GPIO22 (plain GPIO out)                   | CS          |
| GPIO21 (plain GPIO out)                   | DC/A0       |
| GPIO20 (plain GPIO out)                   | RES/RESET   |
| 3V3                                       | VCC         |
| GND                                       | GND         |

These GPIOs were chosen to avoid every other GPIO already claimed elsewhere in this project
(status LED: GPIO0; SD card SPI0: GPIO4/6/7/8; PWM audio out: GPIO10/12). There is no MISO
connection: the SSD1306 is a write-only SPI target.

## Usage

```cpp
#include "pico_display.h"

hw_interface::PicoDisplay display; // default constructor uses the default Config above

if (display.Init() != 0)
{
    // handle init failure
}

display.ShowText("Hello, world!");
display.DisplayFileInfo("kick_01.wav", 1234);
```

To use different pins, construct with an explicit `Config`:

```cpp
hw_interface::PicoDisplay::Config config;
config.sck_gpio = 26;
config.mosi_gpio = 27;
config.cs_gpio = 22;
config.dc_gpio = 21;
config.reset_gpio = 20;
hw_interface::PicoDisplay display(config);
```

Because `PicoDisplay` is accessed through `IDisplay`, application code (e.g.
`app::ui::UserInterface`) that only depends on the interface can swap in another implementation
(e.g. the native/Linux `hw_interface::Display` terminal renderer) without any change beyond how
the concrete display is constructed.
