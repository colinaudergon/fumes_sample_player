#include "wav_file_handler.h"

#include <cstring>

float uint8_to_float(uint8_t x)
{
    return ((float)x - 128.0f) / 128.0f;
}

float int16_to_float(int16_t x)
{
    return (float)x / 32768.0f;
}

float int24_to_float(uint8_t bytes[3])
{
    // build signed 24-bit value
    int32_t v = (bytes[0] << 8) | (bytes[1] << 16) | (bytes[2] << 24);
    v >>= 8; // sign-extend from 24 bits to 32 bits

    return (float)v / 8388608.0f; // 2^23
}

float int32_to_float(int32_t x)
{
    return (float)x / 2147483648.0f;
}

int app::audio::wav::WavFileHandler::ReadHeader(const uint8_t *buffer, size_t buffer_size)
{
    if (buffer_size != kWavHeaderSize || buffer == nullptr)
    {
        return kWavFileHandlerBufferInvalid;
    }

    size_t offset = 0;

    std::memcpy(header_.blockID, buffer + offset, sizeof(header_.blockID));
    offset += sizeof(header_.blockID);

    std::memcpy(&header_.totalSize, buffer + offset, sizeof(header_.totalSize));
    offset += sizeof(header_.totalSize);

    std::memcpy(header_.typeHeader, buffer + offset, sizeof(header_.typeHeader));
    offset += sizeof(header_.typeHeader);

    std::memcpy(header_.fmt, buffer + offset, sizeof(header_.fmt));
    offset += sizeof(header_.fmt);

    std::memcpy(&header_.headerLen, buffer + offset, sizeof(header_.headerLen));
    offset += sizeof(header_.headerLen);

    std::memcpy(&header_.typeOfFormat, buffer + offset, sizeof(header_.typeOfFormat));
    offset += sizeof(header_.typeOfFormat);

    std::memcpy(&header_.numbeOfChannel, buffer + offset, sizeof(header_.numbeOfChannel));
    offset += sizeof(header_.numbeOfChannel);

    std::memcpy(&header_.sampleRate, buffer + offset, sizeof(header_.sampleRate));
    offset += sizeof(header_.sampleRate);

    std::memcpy(&header_.byteRate, buffer + offset, sizeof(header_.byteRate));
    offset += sizeof(header_.byteRate);

    std::memcpy(&header_.blockAlign, buffer + offset, sizeof(header_.blockAlign));
    offset += sizeof(header_.blockAlign);

    std::memcpy(&header_.bitsPerSample, buffer + offset, sizeof(header_.bitsPerSample));
    offset += sizeof(header_.bitsPerSample);

    std::memcpy(header_.dataHeader, buffer + offset, sizeof(header_.dataHeader));
    offset += sizeof(header_.dataHeader);

    std::memcpy(&header_.dataSize, buffer + offset, sizeof(header_.dataSize));
    offset += sizeof(header_.dataSize);

    if (std::memcmp(header_.blockID, "RIFF", sizeof(header_.blockID)) != 0 ||
        std::memcmp(header_.typeHeader, "WAVE", sizeof(header_.typeHeader)) != 0 ||
        std::memcmp(header_.fmt, "fmt ", sizeof(header_.fmt)) != 0)
    {
        return kWavFileHandlerErr;
    }

    format_ = static_cast<AudioFormat>(header_.typeOfFormat);
    channels_ = header_.numbeOfChannel;

    bits_ = header_.bitsPerSample;

    data_size_ = header_.dataSize;
    file_size_ = header_.totalSize + 8; // totalSize excludes blockID and totalSize fields themselves

    return kWavFileHandlerSuccess;
}

int app::audio::wav::WavFileHandler::FindDataChunk(const uint8_t *buffer, size_t buffer_size)
{
    if (buffer == nullptr || buffer_size < kWavHeaderSize)
    {
        return kWavFileHandlerBufferInvalid;
    }

    // Assumes the canonical 44-byte layout parsed by ReadHeader (RIFF/WAVE, 16-byte "fmt "
    // subchunk, "data" subchunk immediately following) -- files with extra chunks in between
    // (e.g. LIST) aren't supported yet.
    if (std::memcmp(header_.dataHeader, "data", sizeof(header_.dataHeader)) != 0)
    {
        return kWavFileHandlerErr;
    }

    data_start_index_ = kWavHeaderSize;
    file_read_index_ = data_start_index_;
    return kWavFileHandlerSuccess;
}

int app::audio::wav::WavFileHandler::ReadData(const uint8_t *buffer, size_t buffer_size, audio_frame_t &frame, size_t n_frames)
{
    if (buffer == nullptr || frame.audio_l == nullptr || frame.audio_r == nullptr)
    {
        return kWavFileHandlerBufferInvalid;
    }

    if (channels_ != 1 && channels_ != 2)
    {
        // audio_frame_t only carries left/right buffers: mono is duplicated to both, stereo is
        // split as-is. Anything else (>2 channels) isn't supported.
        return kWavFileHandlerErr;
    }

    const size_t bytes_per_sample = bits_ / kHeightBits;
    const size_t frame_bytes = channels_ * bytes_per_sample;

    if (bytes_per_sample == 0)
    {
        return kWavFileHandlerErr;
    }

    const size_t available_frames = buffer_size / frame_bytes;
    const size_t frames_read = (n_frames < available_frames) ? n_frames : available_frames;

    if (frames_read == 0)
    {
        frame.n_frames = 0;
        return 0;
    }

    switch (bits_)
    {
    case kHeightBits:
    {
        const uint8_t *src = buffer;
        for (size_t i = 0; i < frames_read; i++)
        {
            frame.audio_l[i] = uint8_to_float(src[0]);
            frame.audio_r[i] = (channels_ == 2) ? uint8_to_float(src[1]) : frame.audio_l[i];
            src += channels_;
        }
        break;
    }
    case kSixteenBits:
    {
        const int16_t *src = reinterpret_cast<const int16_t *>(buffer);
        for (size_t i = 0; i < frames_read; i++)
        {
            frame.audio_l[i] = int16_to_float(src[0]);
            frame.audio_r[i] = (channels_ == 2) ? int16_to_float(src[1]) : frame.audio_l[i];
            src += channels_;
        }
        break;
    }
    case kTwentyfourBits:
    {
        const uint8_t *src = buffer;
        for (size_t i = 0; i < frames_read; i++)
        {
            uint8_t left_bytes[3] = {src[0], src[1], src[2]};
            frame.audio_l[i] = int24_to_float(left_bytes);
            if (channels_ == 2)
            {
                uint8_t right_bytes[3] = {src[3], src[4], src[5]};
                frame.audio_r[i] = int24_to_float(right_bytes);
            }
            else
            {
                frame.audio_r[i] = frame.audio_l[i];
            }
            src += frame_bytes;
        }
        break;
    }
    case kThirtytwoBits:
    {
        const int32_t *src = reinterpret_cast<const int32_t *>(buffer);
        for (size_t i = 0; i < frames_read; i++)
        {
            frame.audio_l[i] = int32_to_float(src[0]);
            frame.audio_r[i] = (channels_ == 2) ? int32_to_float(src[1]) : frame.audio_l[i];
            src += channels_;
        }
        break;
    }
    default:
        return kWavFileHandlerErr;
    }

    frame.n_frames = frames_read;

    file_read_index_ += buffer_size;

    return static_cast<int>(frames_read);
}

int app::audio::wav::WavFileHandler::GetDataSize()
{
    return static_cast<int>(data_size_);
}

bool app::audio::wav::WavFileHandler::IsEndOfFile(size_t position)
{

    return position >= file_size_;
}

int app::audio::wav::WavFileHandler::GetSampleRate()
{
    return static_cast<int>(header_.sampleRate);
}

size_t app::audio::wav::WavFileHandler::GetNumChannels()
{
    return channels_;
}

int app::audio::wav::WavFileHandler::GetBitsPerSample()
{
    return bits_;
}

app::audio::wav::AudioFormat app::audio::wav::WavFileHandler::GetFormat()
{
    return format_;
}
