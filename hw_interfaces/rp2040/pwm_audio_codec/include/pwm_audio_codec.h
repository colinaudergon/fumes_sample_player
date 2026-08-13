#pragma once
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/regs/intctrl.h"

#include "../../../include/IAudioCodec.h"

namespace hw_interface
{

    class PicoAudioCodec : public IAudioCodec
    {
    public:
        PicoAudioCodec() = default;
        ~PicoAudioCodec() override = default;

        int Init() override;
        int RegisterFillCallback(buffer_fill_cb cb) override;
        void Start();
        void Stop();

        // Performs one pending buffer refill (calling fill_cb_, which may itself block -- e.g.
        // on SD card I/O) if the DMA IRQ has flagged that one is needed, and returns true in
        // that case; returns false if no refill was pending. Must be polled frequently from the
        // main loop (NOT called from IRQ context): fill_cb_ can take an arbitrarily long time
        // (e.g. a blocking, mutex-guarded SD/SPI DMA transfer), which is unsafe to run inside a
        // hardware interrupt handler -- if the ISR preempts the main thread while it holds a
        // lock the fill path also needs, a naive in-ISR call can deadlock forever. AcknoledgeIrq()
        // therefore only swaps DMA buffer addresses (cheap, IRQ-safe) and sets a pending flag;
        // the actual refill work happens here, safely outside interrupt context.
        bool ServiceRefill();
        void AcknoledgeIrq();

        // Changes the effective audio sample (PWM update) rate at runtime by recomputing and
        // re-applying both channels' PWM slice clock dividers, without tearing down or rebuilding
        // the DMA chains set up by Init(). The RP2040's clock divider is an 8.4 fixed-point value
        // (1/16th steps), so most requested frequencies cannot be hit exactly -- the actually
        // configured frequency is reported back to the caller.
        //
        // Returns:
        //   < 0  - failure (codec not initialized yet, or new_frequency_hz is not a positive,
        //          representable value given the current system clock and repetition rate).
        //   == 0 - success, new_frequency_hz was set exactly.
        //   > 0  - the requested frequency could not be hit exactly; the returned value is the
        //          actual frequency (Hz, rounded to the nearest integer) that was set instead.
        int SetOutputFrequency(float new_frequency_hz);

    private:
        // Two adjacent-numbered placeholder GPIOs chosen so they land on different PWM slices
        // (RP2040 maps gpio -> slice as (gpio >> 1) & 7, so pins 2 apart are always on different
        // slices) -- easy to change later.
        static constexpr uint kGpioOutputLeft = 10;
        static constexpr uint kGpioOutputRight = 12;
        static constexpr uint kRepetitionRate = 4;
        static constexpr uint kDefaultWrap = 254;
        static constexpr float kDefaultSampleRateHz = 44100.0f;

        // Number of mono samples per half of the double buffer. Streamed from the registered
        // fill_cb_ one half at a time while the other half is being played out by DMA.
        static constexpr size_t kBufferSize = 256;

        // Each stereo channel (left/right) gets its own GPIO pin, own PWM slice, and its own
        // independent 3-channel DMA chain (trigger + PWM + sample), following the classic RP2040
        // "PWM audio via DMA" technique. Both slices are configured with the identical
        // clkdiv/wrap, and both trigger DMA channels are started back-to-back in Start(), so
        // their wrap-boundary timing stays deterministically aligned (no drift, since both count
        // off the same sysclk with the same divider) -- no cross-channel polling or
        // synchronization logic is needed. Only ONE of the two trigger channels (left's) has its
        // completion interrupt enabled; that single ISR refills BOTH channels' buffers in one
        // pass, since fill_cb_ already delivers left+right samples together in a single call.
        struct PwmDmaChannelSet
        {
            uint gpio_pin = 0;
            uint pin_slice = 0;
            // Which PWM channel (PWM_CHAN_A == 0, PWM_CHAN_B == 1) gpio_pin maps to on
            // pin_slice. Determined at Init() time; used to target the correct byte of the
            // 32-bit CC register.
            uint pin_channel = 0;

            int pwm_dma_chan = -1;
            int trigger_dma_chan = -1;
            int sample_dma_chan = -1;

            // Fixed location the sample DMA channel writes each fetched 8-bit sample to (into
            // the byte of single_sample corresponding to pin_channel), and the PWM DMA channel
            // repeatedly copies (kRepetitionRate times) into the PWM slice's CC register.
            uint32_t single_sample = 0;
            uint32_t *single_sample_ptr = nullptr;

            // 8-bit PWM compare values for this channel, DMA'd one byte at a time.
            uint8_t pwm_buffer_0[kBufferSize] = {};
            uint8_t pwm_buffer_1[kBufferSize] = {};
        };

        // Converts a [-1, 1] float sample into an 8-bit PWM compare value (0..kDefaultWrap),
        // centered at the midpoint so the signal can be AC-coupled through a capacitor on the
        // output.
        uint8_t FloatToPwmSample(float sample) const;

        // Quantizes one channel's float samples (kBufferSize of them) into pwm_buffer via
        // FloatToPwmSample(). No downmixing -- each stereo channel drives its own pin.
        void ConvertChannelToPwmBuffer(const float *samples, uint8_t *pwm_buffer) const;

        // Configures one channel's PWM slice + 3-DMA-channel chain. If enable_irq is true, also
        // wires this channel's trigger DMA completion up to DMA_IRQ_1 (used for the left
        // channel only -- see PwmDmaChannelSet's comment above).
        void InitChannel(PwmDmaChannelSet &channel, uint gpio_pin, float clock_div, bool enable_irq);

        // Refills whichever float/PWM buffer pair just finished playing (the one NOT pointed to
        // by playing_index_) via fill_cb_, converting the result to PWM samples for both
        // channels, ready for the pass after next. Called from ServiceRefill() (main-loop
        // context) -- NOT from the DMA IRQ, since fill_cb_ may block (e.g. on SD card I/O).
        void RefillNextBuffer();

        // Recomputes the PWM clock divider for a target sample rate from the measured system
        // clock, following AudioDriver's formula:
        //   clock_div = (f_clk_sys_hz) / (wrap + 1) / target_sample_rate_hz / repetition_rate
        // Returns the unclamped, unquantized divider (may be out of the hardware's valid
        // [1, 256) range -- callers must validate before applying).
        float ComputeClockDiv(float target_sample_rate_hz) const;

        // Hardware IRQs take a plain function pointer (no user-data argument), so the exclusive
        // DMA_IRQ_1 handler is this static trampoline forwarding to the single instance.
        static void IrqHandlerTrampoline();
        static PicoAudioCodec *instance_;

        PwmDmaChannelSet channel_l_;
        PwmDmaChannelSet channel_r_;

        // System clock frequency (Hz), measured once at Init() time via frequency_count_khz();
        // reused by SetOutputFrequency() to recompute the clock divider for a new sample rate
        // without re-measuring.
        float f_clk_sys_hz_ = 0.0f;

        buffer_fill_cb fill_cb_ = nullptr;

        // Index (0 or 1) of the PWM buffers currently being read by each channel's
        // sample_dma_chan; the other one is either being refilled or already holds fresh data
        // ready for the next pass. Written from AcknoledgeIrq() (IRQ context) and read from
        // ServiceRefill() (main-loop context), so it's marked volatile -- a single-byte
        // read/write is atomic on Cortex-M0+, so no additional locking is needed, though a
        // refill racing a same-instant buffer swap can still occasionally target the wrong half
        // under heavy main-loop starvation (see ServiceRefill()'s caveat).
        volatile uint8_t playing_index_ = 0;

        // Set by AcknoledgeIrq() (IRQ context) once a buffer half has finished playing and needs
        // fresh data; cleared by ServiceRefill() (main-loop context) once it has serviced that
        // request. Volatile for the same cross-context-visibility reason as playing_index_.
        volatile bool refill_pending_ = false;

        float buffer_0_left_[kBufferSize] = {};
        float buffer_0_right_[kBufferSize] = {};
        float buffer_1_left_[kBufferSize] = {};
        float buffer_1_right_[kBufferSize] = {};

        audio_buffer_t buffer_0_{buffer_0_left_, buffer_0_right_, kBufferSize};
        audio_buffer_t buffer_1_{buffer_1_left_, buffer_1_right_, kBufferSize};
    };
}
