/**
 * @file pico_rotary_encoder_input_handler.h
 * @brief C++ port of gv_lib's pico_gv_drivers/rotary_encoder C driver as an
 * hw_interface::IInputHandler implementation.
 *
 * Decodes a 2-pin (A/B) quadrature rotary encoder from GPIO edge interrupts, dispatched through
 * GpioInterruptDispatcher (see hw_interfaces/rp2040/gpio_interrupts), and reports each detected
 * DETENT (physical "click") as a kNavigationEvent InputEvent (kUp for clockwise, kDown for
 * counter-clockwise) -- suitable for use standalone or as one source registered with
 * CompositeInputHandler alongside other IInputHandler implementations.
 *
 * A typical incremental encoder (e.g. the common EC11 module) cycles through all 4 quadrature
 * states (00 -> 01 -> 11 -> 10 -> 00, or the reverse) for every single detent/click, coming to
 * rest with both switches released (both pins pulled high) between clicks. Emitting an event on
 * every individual valid quadrature transition -- rather than only once per full detent -- means
 * events fire mid-click, between the physical clicks the user actually feels; this handler
 * accumulates the net number/direction of valid single-step transitions since the encoder last
 * left the rest position, and only reports a step once it settles back at rest AND that
 * accumulated count amounts to exactly one full detent's worth (+/-4) in a consistent direction.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "pico/stdlib.h"

#include "IInputHandler.h"

namespace hw_interface
{

    class PicoRotaryEncoderInputHandler : public IInputHandler
    {
    public:
        /// @param pin_a GPIO connected to the encoder's "A" quadrature channel.
        /// @param pin_b GPIO connected to the encoder's "B" quadrature channel.
        /// @param debounce_us Minimum time (microseconds) that must elapse after a processed
        /// edge before another edge is accepted; any edge arriving sooner is treated as contact
        /// bounce and ignored outright (state not updated, no event queued). Defaults to
        /// kDefaultDebounceUs; tune higher if a particular encoder still produces spurious
        /// steps, or lower if fast manual rotation starts dropping real steps.
        explicit PicoRotaryEncoderInputHandler(uint pin_a, uint pin_b, uint32_t debounce_us = kDefaultDebounceUs);
        ~PicoRotaryEncoderInputHandler() override = default;

        /// @brief Registers GPIO edge interrupts on both pins and captures their initial state.
        /// @return 0 on success, non-zero if either pin failed to register (see
        /// GpioInterruptDispatcher::RegisterHandler()).
        int Init() override;

        /// @brief Non-blocking: pops the oldest pending rotation step (if any) off the internal
        /// queue filled by the GPIO IRQ handler.
        bool PollEvent(InputEvent &out) override;

    private:
        // Small enough to absorb a burst of fast manual rotation between two PollEvent() calls
        // without dropping steps, without requiring dynamic allocation.
        static constexpr size_t kQueueCapacity = 64;

        // Mechanical/optical encoder contact bounce typically settles within ~0.5-1ms, while
        // genuine detent-to-detent transitions (even spun quickly by hand) are usually tens of
        // milliseconds apart -- 1ms is a conservative default with headroom on both sides.
        static constexpr uint32_t kDefaultDebounceUs = 1000;

        // Packed (pin_a << 1 | pin_b) reading for the "rest" position between clicks: with both
        // GPIOs pulled up and each switch shorting to GND when pressed by a detent cam (the
        // wiring documented in hw_interfaces/rp2040/rotary_encoder/README.md), both switches are
        // open (both pins high) at rest.
        static constexpr uint8_t kRestState = 0b11;

        // Net valid single-step transitions per full detent click for a standard 4x (quarter-
        // step) incremental encoder that cycles through all 4 quadrature states once per click
        // (see class-level comment).
        static constexpr int8_t kStepsPerDetent = 4;

        const uint pin_a_;
        const uint pin_b_;
        const uint32_t debounce_us_;

        // Packed (pin_a << 1 | pin_b) reading from the last processed edge; combined with the
        // newly read pins to look up the transition's direction (or "no direction", for
        // bounce/invalid transitions) -- same table as the original rotary_encoder.c.
        uint8_t quadrature_state_ = 0;

        // Running total of valid single-step transitions (+1 per clockwise step, -1 per
        // counter-clockwise step) accumulated since quadrature_state_ last left kRestState.
        // Reset to 0 every time quadrature_state_ returns to kRestState, regardless of whether
        // it amounted to a full detent -- see HandleEdge().
        int8_t step_accumulator_ = 0;

        // time_us_64() timestamp of the last edge that passed the debounce check (0 until the
        // first one). Only ever written/read from GPIO IRQ context (HandleEdge()), so no
        // interrupt-disabling guard is needed around it (unlike the ring buffer below, which is
        // also read from PollEvent() on the main/UI thread).
        uint64_t last_edge_time_us_ = 0;

        // Ring buffer of pending direction events: pushed from GPIO IRQ context (HandleEdge()),
        // popped from PollEvent() on the main/UI thread. Access to the shared indices/count is
        // wrapped in save_and_disable_interrupts()/restore_interrupts() on both sides since
        // RP2040 has no atomic read-modify-write for plain integers across IRQ preemption.
        NavigationDirection event_queue_[kQueueCapacity]{};
        size_t queue_head_ = 0;
        size_t queue_tail_ = 0;
        size_t queue_count_ = 0;

        void HandleEdge();
        static void GpioIrqTrampoline(void *context);
    };

} // namespace hw_interface
