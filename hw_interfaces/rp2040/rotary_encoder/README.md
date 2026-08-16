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
  `rotary_encoder.c`, and reporting each detected full detent (physical "click") as a
  `kNavigationEvent` (`kUp` for clockwise, `kDown` for counter-clockwise). Edges are
  software-debounced (see `debounce_us` below) -- the original C driver had no debouncing at all.

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

### Debouncing

Every GPIO edge is timestamped (`time_us_64()`); any edge arriving less than `debounce_us` after
the last one that was accepted is treated as mechanical contact bounce and dropped outright (the
decoded quadrature state is not updated and no event is queued), so a later genuine edge is still
compared against the last *stable* state rather than a bounced one. Defaults to 1000us (1ms),
which comfortably exceeds typical encoder bounce (usually well under 1ms) while staying far below
the time between genuine detent-to-detent steps, even spinning the knob quickly by hand. Pass a
different value as the constructor's third argument if a particular encoder needs tuning:

```cpp
// Noisier encoder needing a longer debounce window.
hw_interface::PicoRotaryEncoderInputHandler encoder(/*pin_a=*/2, /*pin_b=*/3, /*debounce_us=*/2000);
```

`PicoRotaryEncoderInputHandler` can be used standalone (as above), or registered as one source
with `hw_interface::CompositeInputHandler` alongside other `IInputHandler` implementations (e.g.
an ADC-backed or button-backed source) so `UserInterface` doesn't need to know how many physical
input sources exist.

Unlike the original C driver (which invoked a caller-supplied callback directly from interrupt
context), `PollEvent()` is non-blocking and pulls from a small internal ring buffer filled by the
GPIO IRQ handler -- matching how every other `IInputHandler` implementation in this repository
(e.g. `ConsoleInputHandler`) is polled from the main/UI loop instead of driving it via callback.

### Detent-only reporting

A typical incremental encoder cycles through all 4 quadrature states (`00 -> 01 -> 11 -> 10 ->
00`, or the reverse) for every single physical click, coming to rest with both pins high between
clicks. Queuing an event on every individual valid quadrature transition (as the original C driver
and an earlier version of this port both did) fires 4 events per click, including at intermediate,
mid-click positions the user never intended to select. Instead, `HandleEdge()` only accumulates
the net direction of valid transitions since the encoder last left the rest position, and reports
a single step only once the encoder settles back at rest *and* the accumulated count amounts to
exactly one full detent (+/-4 valid transitions) in a consistent direction. Any other outcome at
rest (a partial nudge that bounced back, a reversed direction mid-click, a missed edge) is
discarded silently and the accumulator resets, so it never bleeds into the next click.
