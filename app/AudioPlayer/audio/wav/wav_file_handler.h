/*
 * @file wav_file_handler.h
 * @brief Contains the class handling wav format data parsing
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "wav_format.h"

namespace app::audio::wav
{
    enum AudioFormat
    {
        kPcm = 1,
        kCompressed
    };

    typedef struct
    {
        float *audio_l;
        float *audio_r;
        size_t n_frames;
    } audio_frame_t;

    class WavFileHandler
    {
    public:
        WavFileHandler() {};
        ~WavFileHandler() {};
        int ReadHeader(const uint8_t *buffer, size_t buffer_size);
        int FindDataChunk(const uint8_t *buffer, size_t buffer_size);
        int ReadData(const uint8_t *buffer, size_t buffer_size, audio_frame_t &frame, size_t n_frames);
        int GetDataSize();
        bool IsEndOfFile();
        int GetSampleRate();
        size_t GetNumChannels();
        int GetBitsPerSample();
        AudioFormat GetFormat();

    private:
        wav_file_header_t header_;

        size_t file_size_;
        size_t data_size_;
        AudioFormat format_;
        size_t channels_;
        int bits_;
        size_t data_start_index_;
        size_t file_read_index_;
        static constexpr size_t kHeightBits = 8;
        static constexpr size_t kSixteenBits = 16;
        static constexpr size_t kTwentyfourBits = 24;
        static constexpr size_t kThirtytwoBits = 32;

        static constexpr size_t kWavHeaderSize = 44;
        static constexpr int kWavFileHandlerSuccess = 0;
        static constexpr int kWavFileHandlerErr = -1;
        static constexpr int kWavFileHandlerBufferInvalid = -2;
    };
} // namespace app::audio::wav