# pico_adc

`PicoAdcController` is the RP2040-specific implementation of the hardware-agnostic
`hw_interface::IAdcController` interface (see `hw_interfaces/include/IAdcController.h`). It
drives the Pico's on-board ADC to read up to 4 channels in a background round-robin timer,
with optional per-channel deadband filtering to ignore small/noisy fluctuations.

Configuration is passed through `IAdcController::Init()`'s generic, packed `AdcConfig{data, size}`
blob rather than hardware-specific parameters, so the interface itself stays free of any
assumption about the underlying ADC. `PicoAdcController` defines its own `PicoAdcConfig` (4 GPIO
numbers) and unpacks it from the blob.

## Usage

```cpp
#include "pico_adc.h"

using hw_interface::AdcConfig;
using hw_interface::IAdcController;
using hw_interface::PicoAdcConfig;
using hw_interface::PicoAdcController;

PicoAdcController adc_controller;

// GPIO 26-29 map to ADC channels 0-3 on the Pico.
PicoAdcConfig config{26, 27, 28, 29};
if (adc_controller.Init(AdcConfig{&config, sizeof(config)}) != 0)
{
    // handle init failure (invalid GPIO, etc.)
}

// Optional: ignore raw readings that don't move by more than +/-50 counts.
adc_controller.SetAdcDeadBand(/*adc_id=*/0, /*deadband_low=*/50, /*deadband_high=*/50);

adc_controller.StartReading(); // begins sampling all 4 channels on a background timer

float value = 0.0f;
if (adc_controller.IsReadingValid() && adc_controller.GetNormalizedReading(0, value) == 0)
{
    // `value` is channel 0's latest reading, normalized to [0.0, 1.0]
}

adc_controller.StopReading();
```

Because `PicoAdcController` is accessed through `IAdcController`, application code that only
depends on the interface can swap in another implementation (e.g. a Linux ADC emulator for
testing) without any change beyond how the concrete controller is constructed and configured.
