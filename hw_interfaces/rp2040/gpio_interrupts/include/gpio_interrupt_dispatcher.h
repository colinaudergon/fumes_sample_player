/**
 * @file gpio_interrupt_dispatcher.h
 * @brief C++ port of gv_lib's pico_gv_drivers/gpio_interrupts C driver.
 *
 * The Pico SDK only allows a single process-wide callback to be registered via
 * gpio_set_irq_enabled_with_callback(); GpioInterruptDispatcher *is* that single callback,
 * fanning interrupts back out to whichever per-pin handler was registered for the GPIO that
 * fired, mirroring the original driver's `irq_handlers[]` table but without the raw
 * malloc()'d gv_gpio_irq_handler_t entries.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "pico/stdlib.h"

namespace hw_interface
{

    /// @brief Signature for a per-pin GPIO interrupt handler. `context` is whatever opaque
    /// pointer was passed to RegisterHandler() (typically the owning driver instance).
    using GpioIrqHandler = void (*)(void *context);

    class GpioInterruptDispatcher
    {
    public:
        /// @brief Initializes `pin`, enables its pull-up, enables `condition` (e.g.
        /// GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL) IRQs on it, and routes future interrupts on
        /// that pin to fn(context).
        /// @return true on success, false if `pin` is out of range or `fn` is null.
        static bool RegisterHandler(uint pin, uint32_t condition, GpioIrqHandler fn, void *context);

    private:
        struct Entry
        {
            GpioIrqHandler fn = nullptr;
            void *context = nullptr;
        };

        // RP2040 exposes GPIO0-29.
        static constexpr size_t kNumGpioPins = 30;
        static Entry handlers_[kNumGpioPins];

        // Single callback registered with the Pico SDK for every pin; looks up and invokes
        // whichever handler (if any) was registered for `gpio`.
        static void HandleInterrupt(uint gpio, uint32_t events);
    };

} // namespace hw_interface
