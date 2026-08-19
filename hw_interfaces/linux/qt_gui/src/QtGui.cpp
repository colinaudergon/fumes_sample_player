#include "QtGui.h"

#include <algorithm>
#include <limits>

#include <QApplication>
#include <QGridLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QString>

// -- WaveformWidget --------------------------------------------------------------------------

hw_interface::WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(120);
    setMinimumWidth(400);
}

void hw_interface::WaveformWidget::SetData(const uint16_t *data, size_t n_frames)
{
    if (data == nullptr || n_frames == 0)
    {
        waveform_.clear();
    }
    else
    {
        waveform_.assign(data, data + n_frames);
    }
    update();
}

void hw_interface::WaveformWidget::SetPlayheadFraction(float fraction)
{
    playhead_fraction_ = std::clamp(fraction, 0.0f, 1.0f);
    update();
}

void hw_interface::WaveformWidget::SetMarkerFractions(float start_fraction, float stop_fraction)
{
    start_marker_fraction_ = std::clamp(start_fraction, 0.0f, 1.0f);
    stop_marker_fraction_ = std::clamp(stop_fraction, 0.0f, 1.0f);
    update();
}

void hw_interface::WaveformWidget::paintEvent(QPaintEvent *event)
{
    (void)event;
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    const int width = this->width();
    const int height = this->height();

    if (!waveform_.empty() && width > 0)
    {
        const int center_y = height / 2;
        const size_t n = waveform_.size();
        const int bar_width = std::max(1, width / static_cast<int>(n));

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 200, 0));

        // Each waveform_[i] is a peak amplitude (0..UINT16_MAX, see
        // AudioPlayer::GetAudioDataToDisplay()); drawn as a bar centered on the widget's vertical
        // midline, symmetric above/below like a typical waveform overview display.
        for (size_t i = 0; i < n; ++i)
        {
            const float amplitude = static_cast<float>(waveform_[i]) /
                                     static_cast<float>(std::numeric_limits<uint16_t>::max());
            const int bar_half_height = static_cast<int>(amplitude * (static_cast<float>(height) / 2.0f));
            const int x = static_cast<int>((static_cast<float>(i) / static_cast<float>(n)) * static_cast<float>(width));
            painter.drawRect(x, center_y - bar_half_height, bar_width, bar_half_height * 2);
        }
    }

    // Marker lines drawn on top of the waveform, playhead drawn last (topmost) so it's always
    // visible even when it coincides with a marker.
    const int start_marker_x = static_cast<int>(start_marker_fraction_ * static_cast<float>(width));
    painter.setPen(QPen(Qt::yellow, 2));
    painter.drawLine(start_marker_x, 0, start_marker_x, height);

    const int stop_marker_x = static_cast<int>(stop_marker_fraction_ * static_cast<float>(width));
    painter.setPen(QPen(Qt::cyan, 2));
    painter.drawLine(stop_marker_x, 0, stop_marker_x, height);

    const int playhead_x = static_cast<int>(playhead_fraction_ * static_cast<float>(width));
    painter.setPen(QPen(Qt::red, 2));
    painter.drawLine(playhead_x, 0, playhead_x, height);
}

// -- QtGui -------------------------------------------------------------------------------------

int hw_interface::QtGui::argc_ = 0;
char *hw_interface::QtGui::argv_[1] = {nullptr};

hw_interface::QtGui::QtGui() = default;

hw_interface::QtGui::~QtGui() = default;

void hw_interface::QtGui::PushEvent(const InputEvent &event)
{
    event_queue_.push_back(event);
}

void hw_interface::QtGui::SetupWindow()
{
    window_ = std::make_unique<QMainWindow>();
    window_->setWindowTitle("Fumes Sample Player");

    QWidget *central = new QWidget(window_.get());
    QGridLayout *layout = new QGridLayout(central);

    // -- Navigation / transport buttons -------------------------------------------------------
    QPushButton *up_button = new QPushButton("Up", central);
    QPushButton *down_button = new QPushButton("Down", central);
    QPushButton *play_button = new QPushButton("Play", central);
    QPushButton *stop_button = new QPushButton("Stop", central);
    QPushButton *freeze_button = new QPushButton("Freeze", central);
    QPushButton *reverse_button = new QPushButton("Reverse", central);
    freeze_button->setCheckable(true);
    reverse_button->setCheckable(true);

    layout->addWidget(up_button, 0, 0);
    layout->addWidget(down_button, 0, 1);
    layout->addWidget(play_button, 1, 0);
    layout->addWidget(stop_button, 1, 1);
    layout->addWidget(freeze_button, 2, 0);
    layout->addWidget(reverse_button, 2, 1);

    QObject::connect(up_button, &QPushButton::clicked, [this]()
                      {
        InputEvent event;
        event.type = InputEventType::kNavigationEvent;
        event.navigationDirection = NavigationDirection::kUp;
        PushEvent(event); });

    QObject::connect(down_button, &QPushButton::clicked, [this]()
                      {
        InputEvent event;
        event.type = InputEventType::kNavigationEvent;
        event.navigationDirection = NavigationDirection::kDown;
        PushEvent(event); });

    QObject::connect(play_button, &QPushButton::clicked, [this]()
                      {
        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kPlayParameterId;
        event.parameter.delta = 1.0f;
        PushEvent(event); });

    QObject::connect(stop_button, &QPushButton::clicked, [this]()
                      {
        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kStopParameterId;
        event.parameter.delta = 0.0f;
        PushEvent(event); });

    QObject::connect(freeze_button, &QPushButton::toggled, [this](bool checked)
                      {
        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kFreezeParameterdId;
        event.parameter.delta = checked ? 1.0f : 0.0f;
        PushEvent(event); });

    QObject::connect(reverse_button, &QPushButton::toggled, [this](bool checked)
                      {
        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kReverseParameterId;
        event.parameter.delta = checked ? 1.0f : 0.0f;
        PushEvent(event); });

    // -- Sliders --------------------------------------------------------------------------------
    QLabel *speed_label = new QLabel("Speed: 1.00x", central);
    QSlider *speed_slider = new QSlider(Qt::Horizontal, central);
    speed_slider->setRange(0, kSpeedSliderSteps);
    // Initial slider position corresponds to last_speed_value_ (1.0x default speed).
    const int initial_speed_step = static_cast<int>(
        (last_speed_value_ - kMinPlaybackSpeed) / (kMaxPlaybackSpeed - kMinPlaybackSpeed) * kSpeedSliderSteps);
    speed_slider->setValue(initial_speed_step);

    QLabel *start_marker_label = new QLabel("Start Marker: 0 ms", central);
    QSlider *start_marker_slider = new QSlider(Qt::Horizontal, central);
    start_marker_slider->setRange(0, kMarkerSliderMaxMs);

    QLabel *stop_marker_label = new QLabel("Stop Marker: 0 ms", central);
    QSlider *stop_marker_slider = new QSlider(Qt::Horizontal, central);
    stop_marker_slider->setRange(0, kMarkerSliderMaxMs);

    layout->addWidget(speed_label, 3, 0, 1, 2);
    layout->addWidget(speed_slider, 4, 0, 1, 2);
    layout->addWidget(start_marker_label, 5, 0, 1, 2);
    layout->addWidget(start_marker_slider, 6, 0, 1, 2);
    layout->addWidget(stop_marker_label, 7, 0, 1, 2);
    layout->addWidget(stop_marker_slider, 8, 0, 1, 2);

    QObject::connect(speed_slider, &QSlider::valueChanged, [this, speed_label](int value)
                      {
        const float new_speed = kMinPlaybackSpeed +
            (static_cast<float>(value) / kSpeedSliderSteps) * (kMaxPlaybackSpeed - kMinPlaybackSpeed);

        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kPlaybackSpeedParameterId;
        // main.cpp treats this delta as relative (added to AudioPlayer's current speed), so emit
        // the change since the last slider position rather than the absolute value.
        event.parameter.delta = new_speed - last_speed_value_;
        last_speed_value_ = new_speed;
        PushEvent(event);

        speed_label->setText(QString("Speed: %1x").arg(new_speed, 0, 'f', 2)); });

    QObject::connect(start_marker_slider, &QSlider::valueChanged, [this, start_marker_label](int value)
                      {
        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kStartMarkerParameterId;
        // main.cpp treats this delta as an absolute ms value, unlike the speed slider above.
        event.parameter.delta = static_cast<float>(value);
        PushEvent(event);

        start_marker_label->setText(QString("Start Marker: %1 ms").arg(value)); });

    QObject::connect(stop_marker_slider, &QSlider::valueChanged, [this, stop_marker_label](int value)
                      {
        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kStopMarkerParameterId;
        event.parameter.delta = static_cast<float>(value);
        PushEvent(event);

        stop_marker_label->setText(QString("Stop Marker: %1 ms").arg(value)); });

    // -- Status label + waveform draw area -----------------------------------------------------
    status_label_ = new QLabel("No file loaded", central);
    layout->addWidget(status_label_, 9, 0, 1, 2);

    waveform_widget_ = new WaveformWidget(central);
    layout->addWidget(waveform_widget_, 10, 0, 1, 2);

    central->setLayout(layout);
    window_->setCentralWidget(central);
}

int hw_interface::QtGui::Init()
{
    if (!app_)
    {
        app_ = std::make_unique<QApplication>(argc_, argv_);
    }

    SetupWindow();
    window_->show();
    return 0;
}

bool hw_interface::QtGui::PollEvent(InputEvent &out)
{
    // Single non-blocking pass: dispatches any pending Qt signals (button clicks, slider moves)
    // synchronously, which in turn push translated InputEvents onto event_queue_ via the
    // lambdas connected in SetupWindow(). Mirrors ConsoleInputHandler::PollEvent()'s non-blocking
    // contract relied on by main.cpp's polling loop.
    QCoreApplication::processEvents();

    if (event_queue_.empty())
    {
        return false;
    }

    out = event_queue_.front();
    event_queue_.pop_front();
    return true;
}

int hw_interface::QtGui::ShowText(const char *text)
{
    if (text == nullptr || status_label_ == nullptr)
    {
        return -1;
    }
    status_label_->setText(QString(text));
    return 0;
}

int hw_interface::QtGui::DisplayFileInfo(const char *file_name, uint32_t duration_ms)
{
    if (file_name == nullptr || status_label_ == nullptr)
    {
        return -1;
    }
    status_label_->setText(QString("File: %1  Duration: %2 ms").arg(file_name).arg(duration_ms));
    return 0;
}

// Matches Display/PicoDisplay's DisplayAudioBufferContent -- placeholder stub, not used by this
// draw area (which only shows the static waveform snapshot + playhead; see
// SetWaveformData()/SetPlayheadPosition()).
int hw_interface::QtGui::DisplayAudioBufferContent(float *audio_left, float *audio_right, size_t n_frames)
{
    (void)audio_left;
    (void)audio_right;
    (void)n_frames;
    return 0;
}

void hw_interface::QtGui::SetWaveformData(const uint16_t *data, size_t n_frames)
{
    if (waveform_widget_ != nullptr)
    {
        waveform_widget_->SetData(data, n_frames);
    }
}

void hw_interface::QtGui::SetPlayheadPosition(uint64_t playhead_ms, uint64_t duration_ms)
{
    if (waveform_widget_ == nullptr)
    {
        return;
    }
    const float fraction = duration_ms > 0
                                ? static_cast<float>(playhead_ms) / static_cast<float>(duration_ms)
                                : 0.0f;
    waveform_widget_->SetPlayheadFraction(fraction);
}

void hw_interface::QtGui::SetMarkerPositions(uint64_t start_marker_ms, uint64_t stop_marker_ms, uint64_t duration_ms)
{
    if (waveform_widget_ == nullptr)
    {
        return;
    }
    const float start_fraction = duration_ms > 0
                                      ? static_cast<float>(start_marker_ms) / static_cast<float>(duration_ms)
                                      : 0.0f;
    const float stop_fraction = duration_ms > 0
                                     ? static_cast<float>(stop_marker_ms) / static_cast<float>(duration_ms)
                                     : 0.0f;
    waveform_widget_->SetMarkerFractions(start_fraction, stop_fraction);
}
