#include "QtGui.h"

#include <algorithm>
#include <limits>

#include <QApplication>
#include <QGridLayout>
#include <QGroupBox>
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
    QPushButton *loop_button = new QPushButton("Loop", central);
    QPushButton *glitch_button = new QPushButton("Glitch", central);
    freeze_button->setCheckable(true);
    reverse_button->setCheckable(true);
    loop_button->setCheckable(true);
    glitch_button->setCheckable(true);

    layout->addWidget(up_button, 0, 0);
    layout->addWidget(down_button, 0, 1);
    layout->addWidget(play_button, 1, 0);
    layout->addWidget(stop_button, 1, 1);
    layout->addWidget(freeze_button, 2, 0);
    layout->addWidget(reverse_button, 2, 1);
    layout->addWidget(loop_button, 3, 0);
    // glitch_button is added inside glitch_group below instead of here, alongside the rest of
    // the glitch-related controls.

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

    QObject::connect(loop_button, &QPushButton::toggled, [this](bool checked)
                      {
        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kLoopParameterId;
        event.parameter.delta = checked ? 1.0f : 0.0f;
        PushEvent(event); });

    QObject::connect(glitch_button, &QPushButton::toggled, [this](bool checked)
                      {
        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kGlitchParameterId;
        event.parameter.delta = checked ? 1.0f : 0.0f;
        PushEvent(event); });

    // -- Sliders --------------------------------------------------------------------------------
    QLabel *speed_label = new QLabel("Speed: 1.00x", central);
    QSlider *speed_slider = new QSlider(Qt::Horizontal, central);
    speed_slider->setRange(0, kSpeedSliderSteps);
    // Initial slider position corresponds to kInitialSpeedValue (1.0x default speed).
    const int initial_speed_step = static_cast<int>(
        (kInitialSpeedValue - kMinPlaybackSpeed) / (kMaxPlaybackSpeed - kMinPlaybackSpeed) * kSpeedSliderSteps);
    speed_slider->setValue(initial_speed_step);

    QLabel *start_marker_label = new QLabel("Start Marker: 0%", central);
    QSlider *start_marker_slider = new QSlider(Qt::Horizontal, central);
    start_marker_slider->setRange(0, kMarkerSliderSteps);

    QLabel *stop_marker_label = new QLabel("Stop Marker: 0%", central);
    QSlider *stop_marker_slider = new QSlider(Qt::Horizontal, central);
    stop_marker_slider->setRange(0, kMarkerSliderSteps);

    layout->addWidget(speed_label, 4, 0, 1, 2);
    layout->addWidget(speed_slider, 5, 0, 1, 2);
    layout->addWidget(start_marker_label, 6, 0, 1, 2);
    layout->addWidget(start_marker_slider, 7, 0, 1, 2);
    layout->addWidget(stop_marker_label, 8, 0, 1, 2);
    layout->addWidget(stop_marker_slider, 9, 0, 1, 2);

    QObject::connect(speed_slider, &QSlider::valueChanged, [this, speed_label](int value)
                      {
        const float new_speed = kMinPlaybackSpeed +
            (static_cast<float>(value) / kSpeedSliderSteps) * (kMaxPlaybackSpeed - kMinPlaybackSpeed);

        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kPlaybackSpeedParameterId;
        // main.cpp sets AudioPlayer's speed directly from this value (see
        // kPlaybackSpeedParameterId's branch), so the presented slider value and the
        // effective playback speed always match.
        event.parameter.delta = new_speed;
        PushEvent(event);

        speed_label->setText(QString("Speed: %1x").arg(new_speed, 0, 'f', 2)); });

    QObject::connect(start_marker_slider, &QSlider::valueChanged, [this, start_marker_label](int value)
                      {
        const float relative_position = static_cast<float>(value) / static_cast<float>(kMarkerSliderSteps);

        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kStartMarkerParameterId;
        // main.cpp treats this delta as a relative position in [0.0, 1.0] (fraction of the
        // file's total length), unlike the speed slider above.
        event.parameter.delta = relative_position;
        PushEvent(event);

        start_marker_label->setText(QString("Start Marker: %1%").arg(relative_position * 100.0f, 0, 'f', 1)); });

    QObject::connect(stop_marker_slider, &QSlider::valueChanged, [this, stop_marker_label](int value)
                      {
        const float relative_position = static_cast<float>(value) / static_cast<float>(kMarkerSliderSteps);

        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kStopMarkerParameterId;
        event.parameter.delta = relative_position;
        PushEvent(event);

        stop_marker_label->setText(QString("Stop Marker: %1%").arg(relative_position * 100.0f, 0, 'f', 1)); });

    // -- Glitch controls (grouped in a light grey box) -------------------------------------
    // All glitch-related controls -- the overall on/off toggle plus the individual
    // GlitchEngine parameters below (see GlitchEngine.h), each mapped 1:1 to one of its
    // setters via AudioPlayer's pass-throughs -- live together in one visually distinct box.
    QGroupBox *glitch_group = new QGroupBox("Glitch", central);
    glitch_group->setStyleSheet(
        "QGroupBox { background-color: #d3d3d3; border-radius: 6px; margin-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }");
    QGridLayout *glitch_layout = new QGridLayout(glitch_group);

    glitch_layout->addWidget(glitch_button, 0, 0, 1, 3);

    QPushButton *noise_output_button = new QPushButton("Noise Output", central);
    QPushButton *pitch_mod_button = new QPushButton("Pitch Mod", central);
    QPushButton *bitcrush_button = new QPushButton("Bitcrush", central);
    noise_output_button->setCheckable(true);
    pitch_mod_button->setCheckable(true);
    bitcrush_button->setCheckable(true);

    glitch_layout->addWidget(noise_output_button, 1, 0);
    glitch_layout->addWidget(pitch_mod_button, 1, 1);
    glitch_layout->addWidget(bitcrush_button, 1, 2);

    QObject::connect(noise_output_button, &QPushButton::toggled, [this](bool checked)
                      {
        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kNoiseOutputParameterId;
        event.parameter.delta = checked ? 1.0f : 0.0f;
        PushEvent(event); });

    QObject::connect(pitch_mod_button, &QPushButton::toggled, [this](bool checked)
                      {
        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kPitchModParameterId;
        event.parameter.delta = checked ? 1.0f : 0.0f;
        PushEvent(event); });

    QObject::connect(bitcrush_button, &QPushButton::toggled, [this](bool checked)
                      {
        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kBitcrushEnableParameterId;
        event.parameter.delta = checked ? 1.0f : 0.0f;
        PushEvent(event); });

    QLabel *pitch_mod_probability_label = new QLabel("Pitch Mod Probability: 0%", central);
    QSlider *pitch_mod_probability_slider = new QSlider(Qt::Horizontal, central);
    pitch_mod_probability_slider->setRange(0, kMarkerSliderSteps);

    QLabel *stutter_probability_label = new QLabel("Stutter Probability: 0%", central);
    QSlider *stutter_probability_slider = new QSlider(Qt::Horizontal, central);
    stutter_probability_slider->setRange(0, kMarkerSliderSteps);

    QLabel *sample_rate_reduction_label = new QLabel(
        QString("Sample Rate Reduction: %1").arg(kMinSampleRateReduction), central);
    QSlider *sample_rate_reduction_slider = new QSlider(Qt::Horizontal, central);
    sample_rate_reduction_slider->setRange(kMinSampleRateReduction, kMaxSampleRateReduction);
    sample_rate_reduction_slider->setValue(kMinSampleRateReduction);

    QLabel *reduction_factor_label = new QLabel(
        QString("Reduction Factor: %1").arg(kMinReductionFactor), central);
    QSlider *reduction_factor_slider = new QSlider(Qt::Horizontal, central);
    reduction_factor_slider->setRange(kMinReductionFactor, kMaxReductionFactor);
    reduction_factor_slider->setValue(kMinReductionFactor);

    glitch_layout->addWidget(pitch_mod_probability_label, 2, 0, 1, 3);
    glitch_layout->addWidget(pitch_mod_probability_slider, 3, 0, 1, 3);
    glitch_layout->addWidget(stutter_probability_label, 4, 0, 1, 3);
    glitch_layout->addWidget(stutter_probability_slider, 5, 0, 1, 3);
    glitch_layout->addWidget(sample_rate_reduction_label, 6, 0, 1, 3);
    glitch_layout->addWidget(sample_rate_reduction_slider, 7, 0, 1, 3);
    glitch_layout->addWidget(reduction_factor_label, 8, 0, 1, 3);
    glitch_layout->addWidget(reduction_factor_slider, 9, 0, 1, 3);

    QObject::connect(pitch_mod_probability_slider, &QSlider::valueChanged, [this, pitch_mod_probability_label](int value)
                      {
        const float probability = static_cast<float>(value) / static_cast<float>(kMarkerSliderSteps);

        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kPitchModProbabilityParameterId;
        event.parameter.delta = probability;
        PushEvent(event);

        pitch_mod_probability_label->setText(QString("Pitch Mod Probability: %1%").arg(probability * 100.0f, 0, 'f', 1)); });

    QObject::connect(stutter_probability_slider, &QSlider::valueChanged, [this, stutter_probability_label](int value)
                      {
        const float probability = static_cast<float>(value) / static_cast<float>(kMarkerSliderSteps);

        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kStutterProbabilityParameterId;
        event.parameter.delta = probability;
        PushEvent(event);

        stutter_probability_label->setText(QString("Stutter Probability: %1%").arg(probability * 100.0f, 0, 'f', 1)); });

    QObject::connect(sample_rate_reduction_slider, &QSlider::valueChanged, [this, sample_rate_reduction_label](int value)
                      {
        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kSampleRateReductionParameterId;
        event.parameter.delta = static_cast<float>(value);
        PushEvent(event);

        sample_rate_reduction_label->setText(QString("Sample Rate Reduction: %1").arg(value)); });

    QObject::connect(reduction_factor_slider, &QSlider::valueChanged, [this, reduction_factor_label](int value)
                      {
        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kReductionFactorParameterId;
        event.parameter.delta = static_cast<float>(value);
        PushEvent(event);

        reduction_factor_label->setText(QString("Reduction Factor: %1").arg(value)); });

    QPushButton *click_output_button = new QPushButton("Click Output", central);
    click_output_button->setCheckable(true);

    glitch_layout->addWidget(click_output_button, 10, 0);

    QObject::connect(click_output_button, &QPushButton::toggled, [this](bool checked)
                      {
        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kClickOutputParameterId;
        event.parameter.delta = checked ? 1.0f : 0.0f;
        PushEvent(event); });

    QLabel *click_density_label = new QLabel("Click Density: 0%", central);
    QSlider *click_density_slider = new QSlider(Qt::Horizontal, central);
    click_density_slider->setRange(0, kMarkerSliderSteps);

    glitch_layout->addWidget(click_density_label, 11, 0, 1, 3);
    glitch_layout->addWidget(click_density_slider, 12, 0, 1, 3);

    QObject::connect(click_density_slider, &QSlider::valueChanged, [this, click_density_label](int value)
                      {
        const float density = static_cast<float>(value) / static_cast<float>(kMarkerSliderSteps);

        InputEvent event;
        event.type = InputEventType::kParameterChangeEvent;
        event.parameter.id = ParameterChangeId::kClickDensityParameterId;
        event.parameter.delta = density;
        PushEvent(event);

        click_density_label->setText(QString("Click Density: %1%").arg(density * 100.0f, 0, 'f', 1)); });

    layout->addWidget(glitch_group, 10, 0, 1, 2);

    // -- Status label + waveform draw area -----------------------------------------------------
    status_label_ = new QLabel("No file loaded", central);
    layout->addWidget(status_label_, 11, 0, 1, 2);

    waveform_widget_ = new WaveformWidget(central);
    layout->addWidget(waveform_widget_, 12, 0, 1, 2);

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
