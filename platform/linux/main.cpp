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
#include "EffectController.h"
#include "FileManager.h"
#include "FatFsFileSystemAdapter.h"
#include "IBlockDevice.h"
#include "NullAudioCodec.h"
#include "PosixBlockDevice.h"
#include "ConsoleInputHandler.h"
#include "QtGui.h"
#include "Display.h"



static constexpr size_t kBufferSize = 4096;
// Number of columns to downsample the loaded file's waveform into for the Qt draw area (see
// AudioPlayer::GetAudioDataToDisplay()'s "n_frames = display width" contract).
static constexpr size_t kWaveformDisplayWidth = 400;
app::filesystem::FileManager file_manager;
app::audio::AudioPlayer audio_player;
hw_interface::NullAudioCodec audio_codec;

// ConsoleInputHandler/Display remain available (see hw_interfaces/linux/user_input and
// hw_interfaces/linux/display) but are no longer wired up here -- QtGui is the active
// IInputHandler + IDisplay for the native/Linux build (controls and waveform draw area share one
// window).
hw_interface::QtGui qt_gui;
// app::audio::EffectController effect_controller;

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
    // effect_controller.Process(frame,frame,current.buffer_len);
}

int main()
{

    // Constructs and shows the Qt window (controls + status label + waveform draw area); also
    // drives (in the polling loop below) any pending button/slider signals via
    // QCoreApplication::processEvents().
    qt_gui.Init();

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
    if (file_manager.SelectBank(0) < 0)
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
        hw_interface::InputEvent command;
        if (qt_gui.PollEvent(command))
        {
            if (command.type == hw_interface::InputEventType::kParameterChangeEvent)
            {
                if (command.parameter.id == hw_interface::ParameterChangeId::kPlayParameterId)
                {
                    const char *file_path = file_manager.GetSelectedFilePath();
                    if (file_path[0] != '\0')
                    {
                        audio_player.LoadFile(file_path);
                        const char *audio_file = audio_player.GetAudioFile();
                        if (audio_file == nullptr)
                        {
                            qt_gui.ShowText("file: unknown\n");
                        }
                        else
                        {
                            qt_gui.DisplayFileInfo(audio_file, audio_player.GetDurationMs());
                        }

                        // GetAudioDataToDisplay() may only be called while is_playing_ is false,
                        // so this must happen after LoadFile() but before Start().
                        uint16_t waveform[kWaveformDisplayWidth];
                        audio_player.GetAudioDataToDisplay(waveform, kWaveformDisplayWidth);
                        qt_gui.SetWaveformData(waveform, kWaveformDisplayWidth);
                        qt_gui.SetMarkerPositions(audio_player.GetStartMarkerMs(), audio_player.GetStopMarkerMs(),
                                                   audio_player.GetDurationMs());

                        audio_player.Start();
                    }
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kStopParameterId)
                {
                    audio_player.Stop();
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kPlaybackSpeedParameterId)
                {
                    // command.parameter.delta carries the absolute target speed (see
                    // ConsoleInputHandler's "speed" subcommand and QtGui's speed slider), so it
                    // replaces the current speed rather than being added to it.
                    audio_player.SetPlaybackSpeed(command.parameter.delta);
                    std::printf("Playback speed: %.2f\n", audio_player.GetPlaybackSpeed());
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kFreezeParameterdId)
                {
                    bool freeze_enable = command.parameter.delta == 1.0;
                    audio_player.Freeze(freeze_enable);
                    std::printf("Freeze request: %.2f\n", command.parameter.delta);
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kReverseParameterId)
                {
                    bool reverse_enable = command.parameter.delta == 1.0;
                    audio_player.SetReverse(reverse_enable);
                    std::printf("Reverse request: %.2f\n", command.parameter.delta);
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kLoopParameterId)
                {
                    bool loop_enable = command.parameter.delta == 1.0;
                    audio_player.SetLooping(loop_enable);
                    std::printf("Loop request: %.2f\n", command.parameter.delta);
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kGlitchParameterId)
                {
                    bool glitch_enable = command.parameter.delta == 1.0;
                    audio_player.SetGlitching(glitch_enable);
                    std::printf("Glitch request: %.2f\n", command.parameter.delta);
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kNoiseOutputParameterId)
                {
                    bool noise_output_enable = command.parameter.delta == 1.0;
                    audio_player.EnableNoiseOutput(noise_output_enable);
                    std::printf("Noise output request: %.2f\n", command.parameter.delta);
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kPitchModParameterId)
                {
                    bool pitch_mod_enable = command.parameter.delta == 1.0;
                    audio_player.EnablePitchMod(pitch_mod_enable);
                    std::printf("Pitch mod request: %.2f\n", command.parameter.delta);
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kBitcrushEnableParameterId)
                {
                    bool bitcrush_enable = command.parameter.delta == 1.0;
                    audio_player.SetBitcrushEnable(bitcrush_enable);
                    std::printf("Bitcrush request: %.2f\n", command.parameter.delta);
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kPitchModProbabilityParameterId)
                {
                    // command.parameter.delta is an absolute probability in [0.0, 1.0].
                    audio_player.SetPitchModProbability(command.parameter.delta);
                    std::printf("Pitch mod probability set to %.3f\n", command.parameter.delta);
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kStutterProbabilityParameterId)
                {
                    // command.parameter.delta is an absolute probability in [0.0, 1.0].
                    audio_player.SetStutterProbability(command.parameter.delta);
                    std::printf("Stutter probability set to %.3f\n", command.parameter.delta);
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kSampleRateReductionParameterId)
                {
                    // command.parameter.delta carries the absolute integer reduction factor.
                    audio_player.SetSampleRateReduction(static_cast<int>(command.parameter.delta));
                    std::printf("Sample rate reduction set to %.0f\n", command.parameter.delta);
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kReductionFactorParameterId)
                {
                    // command.parameter.delta carries the absolute integer reduction factor.
                    audio_player.SetReductionFactor(static_cast<int>(command.parameter.delta));
                    std::printf("Reduction factor set to %.0f\n", command.parameter.delta);
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kStartMarkerParameterId)
                {
                    // command.parameter.delta is a relative position in [0.0, 1.0] (fraction of
                    // the file's total length), not an absolute ms value.
                    audio_player.SetStartMarker(command.parameter.delta);
                    qt_gui.SetMarkerPositions(audio_player.GetStartMarkerMs(), audio_player.GetStopMarkerMs(),
                                               audio_player.GetDurationMs());
                    std::printf("Start marker set to %.3f (relative)\n", command.parameter.delta);
                }
                else if (command.parameter.id == hw_interface::ParameterChangeId::kStopMarkerParameterId)
                {
                    // command.parameter.delta is a relative position in [0.0, 1.0] (fraction of
                    // the file's total length), not an absolute ms value.
                    audio_player.SetStopMarker(command.parameter.delta);
                    qt_gui.SetMarkerPositions(audio_player.GetStartMarkerMs(), audio_player.GetStopMarkerMs(),
                                               audio_player.GetDurationMs());
                    std::printf("Stop marker set to %.3f (relative)\n", command.parameter.delta);
                }
                // kEffectParameterId
            }
            else if (command.type == hw_interface::InputEventType::kNavigationEvent)
            {
                int ret = 0;
                if (command.navigationDirection == hw_interface::NavigationDirection::kUp)
                {
                    ret = file_manager.SelectNextFile();
                    std::printf("Result: %d\n", ret);
                }
                else if (command.navigationDirection == hw_interface::NavigationDirection::kDown)
                {
                    ret = file_manager.SelectPreviousFile();
                    std::printf("Result: %d\n", ret);
                }
            }
        }

        // Keeps the waveform draw area's playhead line advancing in sync with playback.
        qt_gui.SetPlayheadPosition(audio_player.GetPlayheadMs(), audio_player.GetDurationMs());

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    audio_codec.Stop();
    return 0;
}