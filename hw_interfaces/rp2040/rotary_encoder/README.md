# rotary_encoder / gpio_interrupts

C++ ports of gv_lib's `pico_gv_drivers/rotary_encoder` and `pico_gv_drivers/gpio_interrupts` C
drivers, adapted to this repository's architecture.

- `gpio_interrupts`: `hw_interface::GpioInterruptDispatcher` is a small static utility that
  replaces the original driver's global `irq_handlers[]` table + `malloc()`'d entries. The Pico
  SDK only allows a single process-wide callback to be registered via
  `gpio_set_irq_enabled_with_callback()`; this class *is* that callback, fanning interrupts back
  out to whichever per-pin handler was registered via `RegisterHandler()`.

- `rotary_encoder`: `hw_interface::PicoRotaryEncoderInputHandler` implements
  `hw_interface::IInputHandler` (see `hw_interfaces/include/IInputHandler.h`), decoding a 2-pin
  quadrature encoder using the same Gray-code transition table as the original
  `rotary_encoder.c`, and reporting each detected step as a `kNavigationEvent` (`kUp` for
  clockwise, `kDown` for counter-clockwise).

## Usage

```cpp
#include "pico_rotary_encoder_input_handler.h"

hw_interface::PicoRotaryEncoderInputHandler encoder(/*pin_a=*/2, /*pin_b=*/3);
encoder.Init();

hw_interface::InputEvent event;
if (encoder.PollEvent(event))
{
    // event.type == kNavigationEvent, event.navigationDirection is kUp/kDown
}
```

`PicoRotaryEncoderInputHandler` can be used standalone (as above), or registered as one source
with `hw_interface::CompositeInputHandler` alongside other `IInputHandler` implementations (e.g.
an ADC-backed or button-backed source) so `UserInterface` doesn't need to know how many physical
input sources exist.

Unlike the original C driver (which invoked a caller-supplied callback directly from interrupt
context), `PollEvent()` is non-blocking and pulls from a small internal ring buffer filled by the
GPIO IRQ handler -- matching how every other `IInputHandler` implementation in this repository
(e.g. `ConsoleInputHandler`) is polled from the main/UI loop instead of driving it via callback.
