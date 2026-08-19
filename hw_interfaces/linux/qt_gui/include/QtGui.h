/**
 * @file QtGui.h
 * @brief Combined IInputHandler + IDisplay implementation for native/Linux builds: a single Qt6
 * Widgets window with buttons (Up/Down/Play/Stop/Freeze/Reverse), sliders (Speed/Start
 * Marker/Stop Marker), a status label, and a waveform draw area with a live playhead line.
 *
 * Input handling mirrors the commands ConsoleInputHandler exposes on stdin (see
 * hw_interfaces/linux/user_input/include/ConsoleInputHandler.h), translated into the same
 * InputEvent vocabulary. Display mirrors Display/PicoDisplay's IDisplay contract. Both live in
 * one class (rather than two coordinating over a shared window) because the draw area and the
 * controls must share the same QApplication/QMainWindow.
 */

#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "IDisplay.h"
#include "IInputHandler.h"

#include <QWidget>

class QApplication;
class QMainWindow;
class QLabel;
class QPaintEvent;

namespace hw_interface
{

    /// @brief Draw area: renders a static waveform snapshot (set via SetData()) as a bar chart,
    /// plus start/stop marker lines (set via SetMarkerFractions()) and a vertical playhead line
    /// (set via SetPlayheadFraction()) drawn on top of it.
    class WaveformWidget : public QWidget
    {
    public:
        explicit WaveformWidget(QWidget *parent = nullptr);

        /// @brief Replaces the waveform buffer (one bar per element) and repaints.
        void SetData(const uint16_t *data, size_t n_frames);

        /// @brief Moves the playhead line to `fraction` (0.0 = start, 1.0 = end) and repaints.
        void SetPlayheadFraction(float fraction);

        /// @brief Moves the start/stop marker lines to `start_fraction`/`stop_fraction` (each
        /// 0.0 = start of file, 1.0 = end of file) and repaints.
        void SetMarkerFractions(float start_fraction, float stop_fraction);

    protected:
        void paintEvent(QPaintEvent *event) override;

    private:
        std::vector<uint16_t> waveform_;
        float playhead_fraction_ = 0.0f;
        float start_marker_fraction_ = 0.0f;
        float stop_marker_fraction_ = 0.0f;
    };

    class QtGui : public IInputHandler, public IDisplay
    {
    public:
        QtGui();
        ~QtGui() override;

        /// @brief Constructs the QApplication + main window (controls, status label, and
        /// waveform draw area, all wired up) and shows it.
        int Init() override;

        /// @brief Non-blocking: runs a single QCoreApplication::processEvents() pass (letting Qt
        /// dispatch any pending button/slider signals into the internal queue), then pops one
        /// event from that queue into `out` if available.
        bool PollEvent(InputEvent &out) override;

        /// @brief Updates the window's status label with `text`.
        int ShowText(const char *text) override;

        /// @brief Updates the window's status label with the file name and duration.
        int DisplayFileInfo(const char *file_name, uint32_t duration_ms) override;

        /// @brief Stub: matches the existing placeholder on Display/PicoDisplay -- the live
        /// per-callback audio buffer isn't used by this draw area (which only shows the static
        /// waveform snapshot + playhead, see SetWaveformData()/SetPlayheadPosition()).
        int DisplayAudioBufferContent(float *audio_left, float *audio_right, size_t n_frames) override;

        /// @brief Sets the static waveform snapshot to display (see
        /// AudioPlayer::GetAudioDataToDisplay(), only callable while not playing).
        void SetWaveformData(const uint16_t *data, size_t n_frames);

        /// @brief Updates the playhead line position given the current/total playback ms.
        void SetPlayheadPosition(uint64_t playhead_ms, uint64_t duration_ms);

        /// @brief Updates the start/stop marker line positions given their positions and the
        /// file's total duration, all in milliseconds (see AudioPlayer::GetStartMarkerMs()/
        /// GetStopMarkerMs()/GetDurationMs()).
        void SetMarkerPositions(uint64_t start_marker_ms, uint64_t stop_marker_ms, uint64_t duration_ms);

    private:
        /// @brief Builds the QMainWindow and all its child widgets, wiring their signals to push
        /// translated InputEvent values onto event_queue_.
        void SetupWindow();

        void PushEvent(const InputEvent &event);

        // Speed slider range: matches AudioPlayer's supported playback speed range.
        static constexpr float kMinPlaybackSpeed = 0.01f;
        static constexpr float kMaxPlaybackSpeed = 4.0f;
        static constexpr int kSpeedSliderSteps = 400; // slider resolution between min/max speed

        // Start/stop marker sliders: fixed placeholder range (ms), not wired to the actual
        // loaded file's duration in this pass.
        static constexpr int kMarkerSliderMaxMs = 60000;

        // QApplication requires an int& argc/char** argv pair that outlives it; since QtGui is
        // constructed with no CLI args of its own, these are kept as static storage with a fixed
        // "no arguments" value.
        static int argc_;
        static char *argv_[1];

        std::unique_ptr<QApplication> app_;
        std::unique_ptr<QMainWindow> window_;
        QLabel *status_label_ = nullptr;
        WaveformWidget *waveform_widget_ = nullptr;

        // Filled by widget signal handlers (SetupWindow()), drained by PollEvent().
        std::deque<InputEvent> event_queue_;

        // Speed slider emits main.cpp-compatible *relative* deltas (see
        // ConsoleInputHandler::speed_command_'s handling and main.cpp's
        // kPlaybackSpeedParameterId branch), so the handler tracks the last value it sent to
        // compute each new delta.
        float last_speed_value_ = 1.0f;
    };

} // namespace hw_interface
