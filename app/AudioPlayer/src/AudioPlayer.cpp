
#include "AudioPlayer.h"

#include <algorithm>
#include <cstring>

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
    if (file_ == nullptr || output.audio_l == nullptr || output.audio_r == nullptr)
    {
        output.n_frames = 0;
        return -1;
    }

    const size_t bytes_per_sample = static_cast<size_t>(wav_file_handler_.GetBitsPerSample()) / 8;
    const size_t channels = wav_file_handler_.GetNumChannels();
    const size_t frame_bytes = bytes_per_sample * channels;
    if (frame_bytes == 0)
    {
        output.n_frames = 0;
        return -1;
    }

    if (!is_playing_)
    {
        return 1;
    }

    if (wav_file_handler_.IsEndOfFile())
    {
        is_playing_ = false;
        // No more sample data to stream: fill the whole requested buffer with silence instead
        // of falling into the read loop below (which would just spin on zero-byte reads) and
        // leaving stale samples from a previous callback in place.
        FillWithZeros(output, n_frames);
        return static_cast<int>(n_frames);
    }

    size_t frames_remaining = n_frames;
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
        // from the raw byte buffer into these output slices, no intermediate float buffer.
        wav::audio_frame_t chunk_output{
            output.audio_l + total_frames_read,
            output.audio_r + total_frames_read,
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
            // Short read: end of data chunk reached partway through this buffer. Zero-fill the
            // remainder so playback trails into silence instead of holding over stale samples
            // from the previous callback.
            if (total_frames_read < n_frames)
            {
                wav::audio_frame_t tail{
                    output.audio_l + total_frames_read,
                    output.audio_r + total_frames_read,
                    0};
                FillWithZeros(tail, n_frames - total_frames_read);
            }
            total_frames_read = n_frames;
            break;
        }
    }

    output.n_frames = total_frames_read;
    return static_cast<int>(total_frames_read);
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

int app::audio::AudioPlayer::CountNumberOfFileInBank()
{
    // app::FsDir *dir = nullptr;
    // app::FsResult open_dir_result = file_system_.OpenDir(&dir, directory_path);
    // if (open_dir_result != app::FsResult::kOk)
    // {
    //     return -1;
    // }


    return 0;
}

int app::audio::AudioPlayer::Start()
{
    is_playing_ = true;
    return 0;
}

int app::audio::AudioPlayer::Stop()
{
    if (file_ != nullptr)
    {
        file_system_.Close(file_);
        file_ = nullptr;
        is_playing_ = false;
    }
    return 0;
}

int app::audio::AudioPlayer::SetLooping(bool looping)
{
    return 0;
}

int app::audio::AudioPlayer::SetPlaybackSpeed(float speed)
{
    return 0;
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
