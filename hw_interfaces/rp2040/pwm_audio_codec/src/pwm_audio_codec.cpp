/**
 * @file pwm_audio_codec.cpp
 * @brief IAudioCodec implementation for RP2040 builds, driving true stereo 8-bit PWM output
 * through two fully independent 3-DMA-channel chains (one per stereo channel), each following
 * the classic RP2040 "PWM audio via DMA" technique: a trigger DMA channel paced by its slice's
 * PWM wrap DREQ retriggers a PWM DMA channel every cycle, which copies a single held 32-bit
 * value into that slice's CC register kRepetitionRate times before chaining to a sample DMA
 * channel that fetches the next 8-bit sample from the currently-playing PWM buffer.
 *
 * Both channels' PWM slices share the identical clkdiv/wrap and are started back-to-back in
 * Start(), so their wrap-boundary timing stays deterministically aligned (no drift, since both
 * count off the same sysclk with the same divider) -- there is no cross-channel polling or
 * synchronization logic. Only the left channel's trigger DMA completion interrupt is enabled;
 * that single ISR (delivered on DMA_IRQ_1, left free for other DMA users e.g. an SD card SPI
 * driver) refills BOTH channels' buffers in one pass, since fill_cb_ already delivers left+right
 * samples together in a single call -- there is no need for a second interrupt source.
 *
 * The PWM clock divider is computed dynamically at Init() (and again by SetOutputFrequency())
 * from the measured system clock via frequency_count_khz(), rather than a hardcoded constant.
 * The sample DMA channel of each chain writes each 8-bit sample directly into the byte of its
 * own single_sample location that corresponds to whichever PWM channel (A or B) its GPIO maps
 * to, so the same code works regardless of which channel each chosen GPIO uses.
 *
 * On top of that fixed hardware chain, this class keeps the "prime-ahead" double buffering from
 * the previous implementation: while each channel's sample_dma_chan streams through one PWM
 * buffer, ServiceRefill() (polled from the main loop, NOT called from the DMA IRQ) asks the
 * registered fill_cb_ for a fresh block of float stereo samples and converts them into the
 * other buffer pair, ready for the pass after next. The DMA IRQ itself (AcknoledgeIrq()) only
 * swaps buffer addresses and sets a "refill pending" flag -- it never calls fill_cb_ directly,
 * because fill_cb_ may perform blocking I/O (e.g. SD card reads via a mutex-guarded SPI DMA
 * transfer), which is unsafe to run inside a hardware interrupt handler: if the IRQ preempts the
 * main thread while it holds a lock the fill path also needs, an in-ISR call can deadlock.
 */

#include "pwm_audio_codec.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "hardware/clocks.h"

namespace hw_interface
{
    PicoAudioCodec *PicoAudioCodec::instance_ = nullptr;

    uint8_t PicoAudioCodec::FloatToPwmSample(float sample) const
    {
        float clamped = std::clamp(sample, -1.0f, 1.0f);
        // Map [-1, 1] to [0, kDefaultWrap], centered at the midpoint so the signal can be
        // AC-coupled through a capacitor on the output.
        float scaled = (clamped * 0.5f + 0.5f) * static_cast<float>(kDefaultWrap);
        return static_cast<uint8_t>(scaled + 0.5f);
    }

    void PicoAudioCodec::ConvertChannelToPwmBuffer(const float *samples, uint8_t *pwm_buffer) const
    {
        for (size_t i = 0; i < kBufferSize; i++)
        {
            pwm_buffer[i] = FloatToPwmSample(samples[i]);
        }
    }

    float PicoAudioCodec::ComputeClockDiv(float target_sample_rate_hz) const
    {
        return f_clk_sys_hz_ / static_cast<float>(kDefaultWrap + 1) / target_sample_rate_hz /
               static_cast<float>(kRepetitionRate);
    }

    void PicoAudioCodec::InitChannel(PwmDmaChannelSet &channel, uint gpio_pin, float clock_div, bool enable_irq)
    {
        channel.gpio_pin = gpio_pin;

        gpio_set_function(channel.gpio_pin, GPIO_FUNC_PWM);
        channel.pin_slice = pwm_gpio_to_slice_num(channel.gpio_pin);
        channel.pin_channel = pwm_gpio_to_channel(channel.gpio_pin);

        pwm_config config = pwm_get_default_config();
        pwm_config_set_clkdiv(&config, clock_div);
        pwm_config_set_wrap(&config, kDefaultWrap);
        pwm_init(channel.pin_slice, &config, true);

        channel.pwm_dma_chan = dma_claim_unused_channel(true);
        channel.trigger_dma_chan = dma_claim_unused_channel(true);
        channel.sample_dma_chan = dma_claim_unused_channel(true);

        channel.single_sample = 0;
        channel.single_sample_ptr = &channel.single_sample;

        // PWM DMA channel: repeatedly copies the fixed single_sample location into this slice's
        // CC register, paced by the PWM wrap DREQ; chains to sample_dma_chan once it has done so
        // kRepetitionRate times (i.e. once per audio sample).
        dma_channel_config pwm_dma_chan_config = dma_channel_get_default_config(channel.pwm_dma_chan);
        channel_config_set_transfer_data_size(&pwm_dma_chan_config, DMA_SIZE_32);
        channel_config_set_read_increment(&pwm_dma_chan_config, false);
        channel_config_set_write_increment(&pwm_dma_chan_config, false);
        channel_config_set_chain_to(&pwm_dma_chan_config, channel.sample_dma_chan);
        channel_config_set_dreq(&pwm_dma_chan_config, DREQ_PWM_WRAP0 + channel.pin_slice);

        dma_channel_configure(
            channel.pwm_dma_chan,
            &pwm_dma_chan_config,
            // Write to this slice's CC register.
            &pwm_hw->slice[channel.pin_slice].cc,
            // Read from single_sample.
            &channel.single_sample,
            // Transfer once per desired sample repetition.
            kRepetitionRate,
            // Don't start yet.
            false);

        // Trigger DMA channel: on every PWM wrap DREQ, writes the address of single_sample into
        // pwm_dma_chan's trigger-on-write register, (re)starting it for the next sample.
        dma_channel_config trigger_dma_chan_config = dma_channel_get_default_config(channel.trigger_dma_chan);
        channel_config_set_transfer_data_size(&trigger_dma_chan_config, DMA_SIZE_32);
        channel_config_set_read_increment(&trigger_dma_chan_config, false);
        channel_config_set_write_increment(&trigger_dma_chan_config, false);
        channel_config_set_dreq(&trigger_dma_chan_config, DREQ_PWM_WRAP0 + channel.pin_slice);

        dma_channel_configure(
            channel.trigger_dma_chan,
            &trigger_dma_chan_config,
            // Write to PWM DMA channel read address trigger.
            &dma_hw->ch[channel.pwm_dma_chan].al3_read_addr_trig,
            // Read from location containing the address of single_sample.
            &channel.single_sample_ptr,
            // Need to trigger once for each audio sample but as the PWM DREQ is used need to
            // multiply by the repetition rate.
            kRepetitionRate * kBufferSize,
            false);

        if (enable_irq)
        {
            // Fire an interrupt once this channel's trigger_dma_chan has streamed a full
            // buffer's worth of samples, so the IRQ handler can swap buffers and arm the next
            // pass for BOTH channels. Delivered on DMA_IRQ_1 (not IRQ_0) to leave IRQ_0 free for
            // other DMA users (e.g. SD card SPI). Only one channel (left) needs this enabled --
            // see the file-level comment.
            dma_channel_set_irq1_enabled(channel.trigger_dma_chan, true);
            instance_ = this;
            irq_set_exclusive_handler(DMA_IRQ_1, &PicoAudioCodec::IrqHandlerTrampoline);
            irq_set_enabled(DMA_IRQ_1, true);
        }

        // Sample DMA channel: fetches one 8-bit sample at a time from the currently-playing PWM
        // buffer into the byte of single_sample that corresponds to pin_channel (so it lands in
        // the correct half of the CC register regardless of which channel the pin maps to);
        // auto-increments through the buffer; re-armed by pwm_dma_chan's chain once per audio
        // sample, and its base read address is reset by the IRQ handler once per full buffer
        // pass.
        dma_channel_config sample_dma_chan_config = dma_channel_get_default_config(channel.sample_dma_chan);
        channel_config_set_transfer_data_size(&sample_dma_chan_config, DMA_SIZE_8);
        channel_config_set_read_increment(&sample_dma_chan_config, true);
        channel_config_set_write_increment(&sample_dma_chan_config, false);

        dma_channel_configure(
            channel.sample_dma_chan,
            &sample_dma_chan_config,
            // Write to the byte of single_sample for pin_channel (0 => low byte / channel A,
            // 1 => byte offset 2 / channel B).
            reinterpret_cast<uint8_t *>(&channel.single_sample) + 2 * channel.pin_channel,
            // Read from the buffer that will be playing first (primed in Start()).
            channel.pwm_buffer_0,
            // Only do one transfer (once per PWM DMA completion due to chaining).
            1,
            // Don't start yet.
            false);
    }

    int PicoAudioCodec::Init()
    {
        uint f_clk_sys_khz = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
        f_clk_sys_hz_ = static_cast<float>(f_clk_sys_khz) * 1000.0f;
        float clock_div = ComputeClockDiv(kDefaultSampleRateHz);

        InitChannel(channel_l_, kGpioOutputLeft, clock_div, /*enable_irq=*/true);
        InitChannel(channel_r_, kGpioOutputRight, clock_div, /*enable_irq=*/false);

        return 0;
    }

    int PicoAudioCodec::RegisterFillCallback(buffer_fill_cb cb)
    {
        fill_cb_ = cb;
        return 0;
    }

    void PicoAudioCodec::Start()
    {
        // Prime both halves of the double buffer before the DMA chains start consuming them:
        // buffer_0_ becomes the first one played, buffer_1_ is filled ready for the pass right
        // after it.
        if (fill_cb_ != nullptr)
        {
            fill_cb_(&buffer_0_, &buffer_1_);
            ConvertChannelToPwmBuffer(buffer_0_.buffer_left, channel_l_.pwm_buffer_0);
            ConvertChannelToPwmBuffer(buffer_0_.buffer_right, channel_r_.pwm_buffer_0);

            fill_cb_(&buffer_1_, &buffer_0_);
            ConvertChannelToPwmBuffer(buffer_1_.buffer_left, channel_l_.pwm_buffer_1);
            ConvertChannelToPwmBuffer(buffer_1_.buffer_right, channel_r_.pwm_buffer_1);
        }

        playing_index_ = 0;
        dma_hw->ch[channel_l_.sample_dma_chan].al1_read_addr = reinterpret_cast<uintptr_t>(channel_l_.pwm_buffer_0);
        dma_hw->ch[channel_r_.sample_dma_chan].al1_read_addr = reinterpret_cast<uintptr_t>(channel_r_.pwm_buffer_0);

        irq_set_enabled(DMA_IRQ_1, true);
        // Kick things off with both trigger DMA channels, back-to-back so their wrap-boundary
        // timing stays aligned.
        dma_channel_start(channel_l_.trigger_dma_chan);
        dma_channel_start(channel_r_.trigger_dma_chan);
    }

    void PicoAudioCodec::Stop()
    {
        irq_set_enabled(DMA_IRQ_1, false);
        dma_channel_abort(channel_l_.trigger_dma_chan);
        dma_channel_abort(channel_l_.pwm_dma_chan);
        dma_channel_abort(channel_l_.sample_dma_chan);
        dma_channel_abort(channel_r_.trigger_dma_chan);
        dma_channel_abort(channel_r_.pwm_dma_chan);
        dma_channel_abort(channel_r_.sample_dma_chan);
    }

    void PicoAudioCodec::RefillNextBuffer()
    {
        if (fill_cb_ == nullptr)
        {
            return;
        }

        // The buffer pair that just finished playing (i.e. NOT the one playing_index_ now
        // points to, since AcknoledgeIrq() flips it before calling this) is free to be refilled
        // with fresh audio for the pass after next.
        uint8_t *filled_pwm_buffer_l = nullptr;
        if (playing_index_ == 0)
        {
            fill_cb_(&buffer_1_, &buffer_0_);
            ConvertChannelToPwmBuffer(buffer_1_.buffer_left, channel_l_.pwm_buffer_1);
            ConvertChannelToPwmBuffer(buffer_1_.buffer_right, channel_r_.pwm_buffer_1);
            filled_pwm_buffer_l = channel_l_.pwm_buffer_1;
        }
        else
        {
            fill_cb_(&buffer_0_, &buffer_1_);
            ConvertChannelToPwmBuffer(buffer_0_.buffer_left, channel_l_.pwm_buffer_0);
            ConvertChannelToPwmBuffer(buffer_0_.buffer_right, channel_r_.pwm_buffer_0);
            filled_pwm_buffer_l = channel_l_.pwm_buffer_0;
        }

#ifdef PWM_AUDIO_CODEC_DEBUG_PRINT
        // TEMPORARY diagnostic: confirm real (non-flat) audio is actually reaching the PWM
        // buffers, and how often refills actually happen (vs. just being polled). Remove once
        // the "no audio output" issue is resolved.
        static uint32_t refill_count = 0;
        refill_count++;
        uint8_t min_val = filled_pwm_buffer_l[0];
        uint8_t max_val = filled_pwm_buffer_l[0];
        for (size_t i = 1; i < kBufferSize; i++)
        {
            min_val = std::min(min_val, filled_pwm_buffer_l[i]);
            max_val = std::max(max_val, filled_pwm_buffer_l[i]);
        }
        if ((refill_count % 51) == 0)
        {
            printf("[pwm_audio_codec] refill #%lu: L pwm min=%u max=%u (midpoint=%u)\n",
                   static_cast<unsigned long>(refill_count), min_val, max_val, kDefaultWrap / 2);
        }
#endif
    }

    void PicoAudioCodec::AcknoledgeIrq()
    {
        // The buffer pair not currently playing was refilled ahead of time (either by the
        // previous ServiceRefill() call, or by Start()'s priming), so it's ready to become the
        // new "playing" buffer pair for both channels. This only swaps DMA addresses -- cheap
        // and IRQ-safe -- and flags that a refill is now needed; the actual fill_cb_ call
        // (which may block, e.g. on SD card I/O) happens later in ServiceRefill(), polled from
        // the main loop, NOT here.
        playing_index_ = 1 - playing_index_;
        uint8_t *ready_pwm_buffer_l = (playing_index_ == 0) ? channel_l_.pwm_buffer_0 : channel_l_.pwm_buffer_1;
        uint8_t *ready_pwm_buffer_r = (playing_index_ == 0) ? channel_r_.pwm_buffer_0 : channel_r_.pwm_buffer_1;

        dma_hw->ch[channel_l_.sample_dma_chan].al1_read_addr = reinterpret_cast<uintptr_t>(ready_pwm_buffer_l);
        dma_hw->ch[channel_r_.sample_dma_chan].al1_read_addr = reinterpret_cast<uintptr_t>(ready_pwm_buffer_r);

        dma_hw->ch[channel_l_.trigger_dma_chan].al3_read_addr_trig = reinterpret_cast<uintptr_t>(&channel_l_.single_sample_ptr);
        dma_hw->ch[channel_r_.trigger_dma_chan].al3_read_addr_trig = reinterpret_cast<uintptr_t>(&channel_r_.single_sample_ptr);

        // Only the left channel's trigger DMA has its completion IRQ enabled, so only its bit
        // needs clearing.
        dma_hw->ints1 = (1u << channel_l_.trigger_dma_chan);

        // Flag that the buffer pair which just finished playing needs to be refilled, ready for
        // the pass after next. Deferred to ServiceRefill() -- see its comment for why.
        refill_pending_ = true;
    }

    void PicoAudioCodec::IrqHandlerTrampoline()
    {
        if (instance_ != nullptr)
        {
            instance_->AcknoledgeIrq();
        }
    }

    bool PicoAudioCodec::ServiceRefill()
    {
        if (!refill_pending_)
        {
            return false;
        }
        refill_pending_ = false;
        RefillNextBuffer();
        return true;
    }

    int PicoAudioCodec::SetOutputFrequency(float new_frequency_hz)
    {
        if (f_clk_sys_hz_ <= 0.0f)
        {
            // Init() hasn't run yet (no measured system clock to compute a divider from).
            return -1;
        }

        if (new_frequency_hz <= 0.0f)
        {
            return -1;
        }

        float raw_clock_div = ComputeClockDiv(new_frequency_hz);
        if (!std::isfinite(raw_clock_div) || raw_clock_div < 1.0f || raw_clock_div >= 256.0f)
        {
            // Outside the RP2040 PWM clock divider's valid [1, 256) range: not representable.
            return -1;
        }

        // The hardware clock divider is an 8.4 fixed-point value (1/16th steps); quantize to the
        // nearest representable step before applying, matching what pwm_set_clkdiv() would do.
        float quantized_clock_div = std::round(raw_clock_div * 16.0f) / 16.0f;
        if (quantized_clock_div < 1.0f)
        {
            quantized_clock_div = 1.0f;
        }
        else if (quantized_clock_div >= 256.0f)
        {
            quantized_clock_div = 255.9375f;
        }

        // Both channels' slices must stay in lockstep (same sample rate), so apply the same
        // quantized divider to both.
        pwm_set_clkdiv(channel_l_.pin_slice, quantized_clock_div);
        pwm_set_clkdiv(channel_r_.pin_slice, quantized_clock_div);

        float actual_frequency_hz = f_clk_sys_hz_ / static_cast<float>(kDefaultWrap + 1) /
                                     quantized_clock_div / static_cast<float>(kRepetitionRate);
        int rounded_actual_hz = static_cast<int>(std::lround(actual_frequency_hz));

        if (std::fabs(actual_frequency_hz - new_frequency_hz) < 0.5f)
        {
            return 0;
        }
        return rounded_actual_hz;
    }
} // namespace hw_interface
