/**
 * @file main.cpp
 * @brief Native/Linux entry point.
 *
 * Demonstrates that the app layer (AudioPlayer, FileSystem) is entirely hardware-agnostic:
 * it is wired up here against host implementations of IBlockDevice/IAudioCodec
 * (hw_interfaces/linux) instead of the RP2040 ones used by platform/rp2040/wav_file_reader.cpp.
 */

#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include "AudioPlayer.h"
#include "FileManager.h"
#include "FatFsFileSystemAdapter.h"
#include "IBlockDevice.h"
#include "NullAudioCodec.h"
#include "PosixBlockDevice.h"
#include "ConsoleInputHandler.h"
#include "UserInterface.h"
#include "Display.h"

static constexpr size_t kBufferSize = 4096;
app::filesystem::FileManager file_manager;
app::audio::AudioPlayer audio_player;
hw_interface::NullAudioCodec audio_codec;

hw_interface::ConsoleInputHandler console_input;
hw_interface::Display display;
app::ui::UserInterface ui(console_input, display);

void buffer_callback(hw_interface::audio_buffer_t *buffer_0, hw_interface::audio_buffer_t *buffer_1)
{
    // buffer_0 is always the buffer to be consumed for this callback ("current"); buffer_1 is
    // read-ahead scratch space that NullAudioCodec manages internally and isn't touched here.
    (void)buffer_1;

    hw_interface::audio_buffer_t &current = *buffer_0;

    // AudioPlayer streams samples into its own wav::audio_frame_t representation, which mirrors
    // audio_buffer_t's fields but belongs to the app layer.
    app::audio::wav::audio_frame_t frame{current.buffer_left, current.buffer_right, current.buffer_len};
    audio_player.Read(frame, current.buffer_len);
}

int main()
{

    // Puts stdin in non-blocking mode so ui.ProcessUi()'s polling loop below never stalls
    // waiting on a line of console input.
    console_input.Init();

    // Physical drive 0 (see app/FileSystem/include/IBlockDevice.h): a FAT volume backed by a
    // plain file on the host filesystem instead of a real SD/USB device.
    hw_interface::PosixBlockDevice block_device("disk.img");
    filesystem::RegisterBlockDevice(0, &block_device);

    // Own the app-wide singleton IFileSystem instance (see app::GetFileSystem() in
    // IFileSystem.h) instead of constructing a local FatFsFileSystemAdapter: AudioPlayer now
    // obtains this same instance internally, so Init()/Mount() here must happen on it too.
    app::IFileSystem &file_system = app::GetFileSystem();
    file_system.Init();

    app::audio::AudioPlayerConfiguration configuration = {
        .playback_speed = 1.0f,
        .n_channels = 2};

    const char *disk_path = "0:";
    app::FsResult mount_result = file_system.Mount(disk_path);
    if (mount_result != app::FsResult::kOk)
    {
        std::printf("Mount result: %d\n", static_cast<int>(mount_result));
        return -1;
    }

    if (file_manager.Init(disk_path, static_cast<uint8_t>(app::filesystem::SupportedFileExtensions::KWav)) < 0)
    {
        std::printf("Failed to initialize file system manager\n");
        return -1;
    }

    size_t number_of_banks = file_manager.GetNumberOfBanks();
    std::printf("Number of banks: %ld\n", number_of_banks);
    if(file_manager.SelectBank(0) <0)
    {
        std::printf("Failed to select bank\n");
        return -1;
    }

    audio_player.Init(configuration);

    // Native/Linux playback device (see hw_interfaces/linux/audio_codec): backed by miniaudio
    // instead of the RP2040 I2S codec used by platform/rp2040/wav_file_reader.cpp.

    int codec_init_result = audio_codec.Init();
    if (codec_init_result != 0)
    {
        std::printf("Audio codec Init result: %d\n", codec_init_result);
        return -1;
    }
    
    audio_codec.RegisterFillCallback(buffer_callback);
    int codec_start_result = audio_codec.Start();
    if (codec_start_result != 0)
    {
        std::printf("Audio codec Start result: %d\n", codec_start_result);
        return -1;
    }

    std::printf("wav_file_reader (native/Linux build) ready.\n");
    
    while (1)
    {
        ui.ProcessUi();

        hw_interface::InputEvent command;
        while (ui.PopCommand(command))
        {
            if (command.type == hw_interface::InputEventType::kParameterChangeEvent)
            {
                if (command.parameter.id == hw_interface::ParameterChangeId::kPlayParameterId)
                {
                    const char* file_path = file_manager.GetSelectedFilePath();
                    if(file_path[0] != '\0')
                    {
                        audio_player.LoadFile(file_path);
                        ui.DisplayFileInformation(audio_player.GetAudioFile(), audio_player.GetDurationMs());
                        audio_player.Start();
                    }
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kStopParameterId)
                {
                    audio_player.Stop();
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kPlaybackSpeedParameterId)
                {
                    // command.parameter.delta is a relative adjustment (see ConsoleInputHandler's
                    // "speed" subcommand help text: "Playback speed delta"), not an absolute
                    // speed, so it's added to the current speed rather than replacing it.
                    audio_player.SetPlaybackSpeed(audio_player.GetPlaybackSpeed() + command.parameter.delta);
                    std::printf("Playback speed: %.2f\n", audio_player.GetPlaybackSpeed());
                }
                else if(command.parameter.id == hw_interface::ParameterChangeId::kFreezeParameterdId)
                {   
                    bool freeze_enable = command.parameter.delta == 1.0;
                    audio_player.Freeze(freeze_enable);
                    std::printf("Freeze request: %.2f\n",command.parameter.delta);
                }
            }
            else if (command.type == hw_interface::InputEventType::kNavigationEvent)
            {
                int ret = 0;
                if (command.navigationDirection == hw_interface::NavigationDirection::kUp)
                {
                    ret = file_manager.SelectNextFile();
                    std::printf("Result: %d\n",ret);
                }
                else if (command.navigationDirection == hw_interface::NavigationDirection::kDown)
                {
                    ret = file_manager.SelectPreviousFile();
                    std::printf("Result: %d\n",ret);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    audio_codec.Stop();
    return 0;
}