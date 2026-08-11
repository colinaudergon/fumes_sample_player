# USB Mass-Storage Bandwidth Feasibility Analysis

This is a design/decision record, not an implementation. No `IBlockDevice` code exists (or is
added by this document) for USB mass storage — it only answers whether a *future* USB MSC
`IBlockDevice` (analogous to the existing `SdBlockDevice`, see
`app/FileSystem/hw_layer/SdInterface/`) could keep this app's audio output buffer fed.

## Context

[`rppicomidi/pico-usb-host-msc-demo`](https://github.com/rppicomidi/pico-usb-host-msc-demo) is
the reference implementation this project would model a USB host MSC block device on: RP2040
native USB hardware, TinyUSB host stack, MSC bulk transfers, elm-chan FatFs. That project's own
README states:

> The RP2040's native USB hardware transfer rate maxes out at USB Full Speed (12Mbps).
> Furthermore, it is not designed to be high performance in host mode. Expect a transfer limit of
> a bit less than 64 Kbytes/second. ... If you plan to stream audio or video, it is probably not
> [fine].

That ~64 KB/s figure is a real-world, already-overhead-inclusive (USB protocol + FatFs + MSC
command/response round trips) sustained throughput ceiling for this exact hardware/software
combination — not a theoretical best case. It is the number this analysis checks against.

## Required sustained byte rate

`AudioPlayer::Read()` (`app/AudioPlayer/src/AudioPlayer.cpp`,
`app/AudioPlayer/include/AudioPlayer.h`) computes, per output callback:

```
n_frames_out = n_frames * playback_speed_
```

i.e. the number of **raw source-file frames** that must be read from storage scales linearly
with the configured playback speed (`SetPlaybackSpeed()`, clamped to
`[kMinPlayBackSpeed = 0.01, kMaxPlaybackSpeed = 4.0]`). The output side itself runs at a fixed
rate (`NullAudioCodec::kSampleRate = 44100` Hz, delivering up to
`kMaxFramesPerCallback = 4096` frames per callback), but the *source* read rate depends on the
file's own native format, which is fully general — `wav_format.h`'s `sampleRate`,
`numberOfChannel`s, and `bitsPerSample` are read straight from each file's header, with nothing
in the app pinning them to one fixed format.

This gives the required sustained storage-read byte rate:

```
required_bytes_per_sec = file_sample_rate * channels * (bits_per_sample / 8) * playback_speed
```

This must stay below the storage backend's sustained throughput, or the source scratch buffers
(`AudioPlayer::time_adjust_source_l_`/`_r_`, refilled via
`kMaxReadFrames = 1024`-frame chunks) will underrun and playback will stall/glitch, regardless
of how large any prefetch buffer is (see "Mitigation options" below).

## Worked examples

All values assume the ~64 KB/s (65,536 B/s) ceiling reported for RP2040 native USB host MSC.
1.0x is normal-speed playback; 4.0x is `kMaxPlaybackSpeed`, the fastest speed the app currently
allows.

| Source format                          | Required B/s @1.0x | ≈ KiB/s @1.0x | Fits in 64 KB/s? | Required B/s @4.0x | ≈ KiB/s @4.0x | Fits in 64 KB/s? |
|-----------------------------------------|--------------------:|--------------:|:----------------:|--------------------:|--------------:|:----------------:|
| 44.1 kHz / 16-bit / **stereo**           | 176,400              | 172.3         | ❌ No             | 705,600              | 689.1         | ❌ No             |
| 44.1 kHz / 24-bit / stereo               | 264,600              | 258.4         | ❌ No             | 1,058,400             | 1,033.6       | ❌ No             |
| 44.1 kHz / 16-bit / **mono**             | 88,200               | 86.1          | ❌ No             | 352,800               | 344.5         | ❌ No             |
| 32 kHz / 16-bit / mono                   | 64,000               | 62.5          | ⚠️ Borderline     | 256,000               | 250.0         | ❌ No             |
| 22.05 kHz / 16-bit / mono                | 44,100               | 43.1          | ✅ Yes            | 176,400               | 172.3         | ❌ No             |
| 44.1 kHz / **8-bit** / mono              | 44,100               | 43.1          | ✅ Yes            | 176,400               | 172.3         | ❌ No             |
| 16 kHz / 8-bit / mono                    | 16,000               | 15.6          | ✅ Yes            | 64,000                | 62.5          | ⚠️ Borderline     |
| 8 kHz / 8-bit / mono                     | 8,000                | 7.8           | ✅ Yes            | 32,000                | 31.3          | ✅ Yes            |

## Conclusion

**No — ~64 KB/s is not sufficient for this app's common/default case:** a standard CD-quality
WAV file (44.1 kHz, 16-bit, stereo) needs ~172 KiB/s just to play at 1.0x speed, already ~2.7×
over the ceiling. Even a *mono* 44.1 kHz/16-bit file (~86 KiB/s) exceeds it at normal speed.

USB MSC at this throughput is only comfortably viable for **low-bandwidth sources**: mono files
at reduced sample rates and/or reduced bit depth (roughly ≤32 kHz/16-bit mono, or ≤44.1 kHz/8-bit
mono), and only at playback speeds close to 1.0x — `SetPlaybackSpeed()`'s upper range
(`kMaxPlaybackSpeed = 4.0`) multiplies the required rate proportionally, so even the
lowest-bandwidth formats above become marginal/insufficient at higher speeds.

## Mitigation options (not implemented — tradeoffs only)

- **Restrict USB-sourced playback to low-bandwidth formats.** Enforce (or document) a
  mono/low-sample-rate/low-bit-depth requirement for files played from a USB-backed volume,
  falling back to silence/refusal-to-load for files that would exceed the budget.
- **Larger RAM prefetch buffer / pre-roll delay.** This only smooths short-term *burst* jitter
  (USB transfer scheduling gaps, directory lookups, etc.) — it cannot fix a sustained average
  shortfall. If required bytes/sec > available sustained throughput, a bigger buffer just delays
  the eventual underrun; it doesn't prevent it.
- **Cap `SetPlaybackSpeed()` to ≤1.0x when the active file lives on a USB block device**, since
  the required rate scales linearly with speed and speed >1.0x makes an already-tight budget
  worse.
- **Evaluate `Pico-PIO-USB`** (PIO + 2 GPIO pins) as an alternative host transport instead of
  RP2040's native USB hardware. The reference demo's README notes its throughput has not been
  characterized, so this would need its own measurement before being relied on.
- **Keep `SdBlockDevice` (SD-over-SPI, already implemented in
  `app/FileSystem/hw_layer/SdInterface/`) as the primary/only backend for full-quality or
  variable-speed playback**, treating any future USB MSC backend as best-effort/low-bandwidth-only
  storage rather than a drop-in replacement.
