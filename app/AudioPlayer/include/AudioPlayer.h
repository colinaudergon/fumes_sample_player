/**
 * @file AudioPlayer.h
 * @brief
 */

#pragma once

#include "../../../hw_interfaces/include/IAudioCodec.h"
#include "../../include/IFileSystem.h"
#include "wav_file_handler.h"

namespace app::audio
{

    struct AudioPlayerConfiguration
    {
        float playback_speed;
        size_t n_channels;
    };

    class AudioPlayer
    {
    public:
        // No IFileSystem is injected: the instance is obtained internally from GetFileSystem()
        // (see IFileSystem.h). A dedicated factory will replace this direct call later.
        AudioPlayer() : file_system_(GetFileSystem()) {}
        ~AudioPlayer();

        int Init(AudioPlayerConfiguration &configuration);
        int LoadFile(const char *path);
        int Read(wav::audio_frame_t &output, size_t n_frames);

        int Start();
        int Stop();
        int SetLooping(bool looping);
        int SetPlaybackSpeed(float speed);
        
        int GetSampleRate();
        size_t GetNumChannels();
        int GetBitsPerSample();
        int GetDataSize();

        bool IsPlaying();
    private:
        void FillWithZeros(wav::audio_frame_t &output, size_t n_frames);

        bool is_playing_{false};
        bool is_looping_{false};
        float playback_speed_{0.0f};
        size_t n_channels{0};
        IFileSystem &file_system_;
        wav::WavFileHandler wav_file_handler_;
        FsFile *file_{nullptr};

        static constexpr size_t kWavHeaderSize = 44;

        // Bounded scratch buffer used to stream+convert raw file bytes into Read()'s output in
        // chunks, avoiding both unbounded stack/heap use and any extra copy: WavFileHandler::
        // ReadData() converts straight from this buffer into the caller's output arrays.
        static constexpr size_t kMaxReadFrames = 1024;
        static constexpr size_t kMaxBytesPerFrame = 8; // 2 channels * 4 bytes (32-bit) max
        uint8_t read_scratch_buffer_[kMaxReadFrames * kMaxBytesPerFrame];
    };

} // namespace app::audio