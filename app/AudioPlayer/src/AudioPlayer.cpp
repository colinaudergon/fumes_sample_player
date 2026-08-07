
#include "AudioPlayer.h"

#include <algorithm>
#include <cstring>
#include <cmath>

int app::audio::AudioPlayer::Init(AudioPlayerConfiguration &configuration)
{
    n_channels = configuration.n_channels;
    playback_speed_ = configuration.playback_speed;
    return 0;
}

int app::audio::AudioPlayer::LoadFile(const char *path)
{
    if (path == nullptr)
    {
        return static_cast<int>(FsResult::kInvalidParameter);
    }

    is_playing_ = false;

    if (file_ != nullptr)
    {
        file_system_.Close(file_);
        file_ = nullptr;
    }

    FsFile *file = nullptr;
    FsResult open_result = file_system_.Open(&file, path, FsOpenMode::kRead);
    if (open_result != FsResult::kOk)
    {
        return static_cast<int>(open_result);
    }

    uint8_t header_buffer[kWavHeaderSize];
    size_t bytes_read = 0;
    FsResult read_result = file_system_.Read(file, header_buffer, sizeof(header_buffer), &bytes_read);
    if (read_result != FsResult::kOk || bytes_read != sizeof(header_buffer))
    {
        file_system_.Close(file);
        return (read_result != FsResult::kOk) ? static_cast<int>(read_result) : static_cast<int>(FsResult::kNoFile);
    }

    int header_result = wav_file_handler_.ReadHeader(header_buffer, bytes_read);
    if (header_result != 0)
    {
        file_system_.Close(file);
        return header_result;
    }

    int data_chunk_result = wav_file_handler_.FindDataChunk(header_buffer, bytes_read);
    if (data_chunk_result != 0)
    {
        file_system_.Close(file);
        return data_chunk_result;
    }

    // Keep the file open (positioned right after the header, at the start of the data chunk)
    // so Read() can stream sample data from it.
    file_ = file;

    std::strncpy(loaded_file_path_, path, kMaxFilePathLength - 1);
    loaded_file_path_[kMaxFilePathLength - 1] = '\0';

    return 0;
}

int app::audio::AudioPlayer::Read(wav::audio_frame_t &output, size_t n_frames)
{
    if (output.audio_l == nullptr || output.audio_r == nullptr)
    {
        output.n_frames = 0;
        return -1;
    }

    size_t n_frames_out = static_cast<size_t>(static_cast<float>(n_frames) * playback_speed_);

    // Clamp to the pre-resample scratch buffers' capacity. Together with SetPlaybackSpeed()'s
    // [kMinPlayBackSpeed, kMaxPlaybackSpeed] clamp and the kMaxOutputFrames precondition on
    // n_frames, this should never actually trigger -- it's just a last-resort safety net so a
    // caller violating that precondition can't overflow time_adjust_source_l_/r_.
    if (n_frames_out > kMaxSourceFrames)
    {
        n_frames_out = kMaxSourceFrames;
    }

    if (is_freezed_ && is_playing_)
    {
        return static_cast<int>(n_frames);
    }

    const size_t bytes_per_sample = static_cast<size_t>(wav_file_handler_.GetBitsPerSample()) / 8;
    const size_t channels = wav_file_handler_.GetNumChannels();
    const size_t frame_bytes = bytes_per_sample * channels;

    if (frame_bytes == 0)
    {
        output.n_frames = 0;
        return -1;
    }

    if (file_ == nullptr || wav_file_handler_.IsEndOfFile() || is_playing_ == false)
    {
        is_playing_ = false;
        // No more sample data to stream: fill the whole requested buffer with silence instead
        // of falling into the read loop below (which would just spin on zero-byte reads) and
        // leaving stale samples from a previous callback in place.
        FillWithZeros(output, n_frames);
        return static_cast<int>(n_frames);
    }

    // Read up to n_frames_out raw source frames (at the file's native sample rate, i.e. before
    // any speed adjustment) into the pre-resample scratch buffers.
    size_t frames_remaining = n_frames_out;
    size_t total_frames_read = 0;

    while (frames_remaining > 0)
    {
        const size_t chunk_frames = std::min(frames_remaining, kMaxReadFrames);
        const size_t bytes_to_read = chunk_frames * frame_bytes;

        size_t bytes_read = 0;
        FsResult read_result = file_system_.Read(file_, read_scratch_buffer_, bytes_to_read, &bytes_read);
        if (read_result != FsResult::kOk)
        {
            break;
        }

        // Convert + copy happen in the same pass: WavFileHandler::ReadData() writes straight
        // from the raw byte buffer into these scratch slices, no intermediate float buffer.
        wav::audio_frame_t chunk_output{
            time_adjust_source_l_ + total_frames_read,
            time_adjust_source_r_ + total_frames_read,
            0};

        int converted = wav_file_handler_.ReadData(read_scratch_buffer_, bytes_read, chunk_output, chunk_frames);
        if (converted <= 0)
        {
            break;
        }

        total_frames_read += static_cast<size_t>(converted);
        frames_remaining -= chunk_frames;

        if (static_cast<size_t>(converted) < chunk_frames)
        {
            // Short read: end of data chunk reached partway through this buffer.
            break;
        }
    }

    if (total_frames_read == 0)
    {
        // Hit EOF immediately: nothing to resample, output is silence.
        is_playing_ = false;
        FillWithZeros(output, n_frames);
        return static_cast<int>(n_frames);
    }

    // AdjustTime() stretches/compresses the total_frames_read raw source frames into exactly
    // n_frames output frames -- this is what actually implements playback_speed_ (rather than
    // just reading more/fewer raw frames directly into `output`, which would either overflow
    // output's capacity when playback_speed_ > 1.0 or leave part of it stale when < 1.0).
    wav::audio_frame_t source_frame{time_adjust_source_l_, time_adjust_source_r_, total_frames_read};
    AdjustTime(source_frame, output, n_frames);

    return static_cast<int>(output.n_frames);
}

void app::audio::AudioPlayer::FillWithZeros(wav::audio_frame_t &output, size_t n_frames)
{
    if (output.audio_l != nullptr)
    {
        std::fill(output.audio_l, output.audio_l + n_frames, 0.0f);
    }
    if (output.audio_r != nullptr)
    {
        std::fill(output.audio_r, output.audio_r + n_frames, 0.0f);
    }
    output.n_frames = n_frames;
}

void app::audio::AudioPlayer::AdjustTime(wav::audio_frame_t &input, wav::audio_frame_t &output, size_t n_frames)
{
    // n_frames is the desired OUTPUT frame count (already computed by the caller, e.g.
    // n_frames_out = n_frames * playback_speed_ in Read()); the source frame count comes from
    // input.n_frames instead.
    size_t num_src_samples = input.n_frames;

    if (input.audio_l == nullptr || input.audio_r == nullptr ||
        output.audio_l == nullptr || output.audio_r == nullptr ||
        num_src_samples == 0 || n_frames == 0)
    {
        output.n_frames = 0;
        return;
    }

    for (size_t out_sample = 0; out_sample < n_frames; out_sample++)
    {
        // Spreads output samples evenly across the full source range [0, num_src_samples], the
        // same percent-based mapping TimeAdjust() uses, so the first/last output samples line up
        // with the first/last source samples. When n_frames == 1 there's no "span" to spread
        // across, so just sample the very start of the source buffer.
        float percent = (n_frames > 1)
                            ? static_cast<float>(out_sample) / static_cast<float>(n_frames - 1)
                            : 0.0f;

        float src_sample_float = static_cast<float>(num_src_samples) * percent;

        // ApplyInterpolation() clamps src_sample_float (and its "next sample") to
        // num_src_samples - 1 internally, so percent == 1.0 (src_sample_float ==
        // num_src_samples, one past the last valid index) is safe here.
        output.audio_l[out_sample] = ApplyInterpolation(input.audio_l, src_sample_float, num_src_samples);
        output.audio_r[out_sample] = ApplyInterpolation(input.audio_r, src_sample_float, num_src_samples);
    }

    output.n_frames = n_frames;
}

float app::audio::AudioPlayer::ApplyInterpolation(float *input, float single_value, size_t n_frames)
{
    if (input == nullptr || n_frames == 0)
    {
        return 0.0f;
    }

    size_t sample = static_cast<size_t>(single_value);
    if (sample >= n_frames)
    {
        sample = n_frames - 1;
    }

    float fraction = single_value - std::floorf(single_value);

    size_t next_sample = sample + 1;
    if (next_sample >= n_frames)
    {
        // No next sample available (last frame in the buffer): fall back to the current sample
        // so we never read/interpolate past the end of the buffer.
        next_sample = sample;
    }

    return input[sample] + fraction * (input[next_sample] - input[sample]);
}

int app::audio::AudioPlayer::Start()
{
    is_playing_ = true;
    is_freezed_ = false;
    return 0;
}

int app::audio::AudioPlayer::Stop()
{
    if (file_ != nullptr)
    {
        file_system_.Close(file_);
        file_ = nullptr;
        is_playing_ = false;
        is_freezed_ = false;
    }
    return 0;
}

int app::audio::AudioPlayer::SetLooping(bool looping)
{
    return 0;
}

int app::audio::AudioPlayer::SetPlaybackSpeed(float speed)
{
    playback_speed_ = speed;
    if (playback_speed_ <= kMinPlayBackSpeed)
    {
        playback_speed_ = kMinPlayBackSpeed;
    }
    else if (playback_speed_ > kMaxPlaybackSpeed)
    {
        playback_speed_ = kMaxPlaybackSpeed;
    }
    return 0;
}

float app::audio::AudioPlayer::GetPlaybackSpeed()
{
    return playback_speed_;
}

void app::audio::AudioPlayer::Freeze(bool enable)
{
    is_freezed_ = enable && is_playing_;
}

int app::audio::AudioPlayer::GetSampleRate()
{
    return wav_file_handler_.GetSampleRate();
}

size_t app::audio::AudioPlayer::GetNumChannels()
{
    return wav_file_handler_.GetNumChannels();
}

int app::audio::AudioPlayer::GetBitsPerSample()
{
    return wav_file_handler_.GetBitsPerSample();
}

int app::audio::AudioPlayer::GetDataSize()
{
    return wav_file_handler_.GetDataSize();
}

uint32_t app::audio::AudioPlayer::GetDurationMs()
{
    const size_t bytes_per_sample = static_cast<size_t>(wav_file_handler_.GetBitsPerSample()) / 8;
    const size_t channels = wav_file_handler_.GetNumChannels();
    const int sample_rate = wav_file_handler_.GetSampleRate();
    const size_t bytes_per_second = bytes_per_sample * channels * static_cast<size_t>(sample_rate);
    if (bytes_per_second == 0)
    {
        return 0;
    }

    const uint64_t data_size = static_cast<uint64_t>(wav_file_handler_.GetDataSize());
    return static_cast<uint32_t>((data_size * 1000) / bytes_per_second);
}

const char *app::audio::AudioPlayer::GetAudioFile()
{
    return loaded_file_path_;
}

bool app::audio::AudioPlayer::IsPlaying()
{
    return is_playing_;
}

app::audio::AudioPlayer::~AudioPlayer()
{
    if (file_ != nullptr)
    {
        file_system_.Close(file_);
        file_ = nullptr;
    }
}
