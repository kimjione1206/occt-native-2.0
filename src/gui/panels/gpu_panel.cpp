#include "gpu_panel.h"
#include "panel_styles.h"
#include "../widgets/realtime_chart.h"
#include "../widgets/circular_gauge.h"
#include "../../engines/gpu_engine.h"
#include "../../engines/base_engine.h"
#include "../../monitor/sensor_manager.h"

#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>

namespace occt { namespace gui {

GpuPanel::GpuPanel(QWidget* parent)
    : QWidget(parent)
    , engine_(std::make_unique<GpuEngine>())
{
    bool gpuOk = engine_->initialize();
    setupUi();
    if (!gpuOk) {
        startStopBtn_->setEnabled(false);
        startStopBtn_->setText("GPU 사용 불가");
        startStopBtn_->setStyleSheet("background-color: #555; color: #999;");
        statusBanner_->setText("GPU 백엔드 사용 불가 (이 빌드에서 OpenCL/Vulkan이 활성화되지 않음)");
        statusBanner_->setVisible(true);
    }

    monitorTimer_ = new QTimer(this);
    connect(monitorTimer_, &QTimer::timeout, this, &GpuPanel::updateMonitoring);
}

GpuPanel::~GpuPanel()
{
    if (engine_ && engine_->is_running()) {
        engine_->stop();
    }
}

void GpuPanel::setupUi()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    mainLayout->addWidget(createSettingsSection());
    mainLayout->addWidget(createMonitoringSection(), 1);
}

QFrame* GpuPanel::createSettingsSection()
{
    auto* frame = new QFrame();
    frame->setFixedWidth(320);
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("GPU 스트레스 테스트", frame);
    title->setStyleSheet(styles::kPanelTitle);
    layout->addWidget(title);

    auto* subtitle = new QLabel("GPU 연산 및 3D 스트레스 테스트 설정", frame);
    subtitle->setStyleSheet(styles::kPanelSubtitle);
    layout->addWidget(subtitle);

    layout->addSpacing(10);

    // GPU selection
    auto* gpuLabel = new QLabel("GPU 장치", frame);
    gpuLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(gpuLabel);

    gpuSelectCombo_ = new QComboBox(frame);
    gpuSelectCombo_->setAccessibleDescription("gpu_device_combo");
    // Populate with detected GPUs
    auto gpus = engine_->get_available_gpus();
    if (gpus.empty()) {
        gpuSelectCombo_->addItem("자동 감지 (기본 GPU)");
    } else {
        for (const auto& gpu : gpus) {
            gpuSelectCombo_->addItem(QString::fromStdString(gpu.name));
        }
    }
    layout->addWidget(gpuSelectCombo_);

    // Mode
    auto* modeLabel = new QLabel("테스트 모드", frame);
    modeLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(modeLabel);

    modeCombo_ = new QComboBox(frame);
    modeCombo_->setAccessibleDescription("gpu_mode_combo");
    modeCombo_->addItems({
        "Matrix FP32",
        "Matrix FP64",
        "FMA Stress",
        "Trigonometric",
        "VRAM Test",
        "Mixed",
        "Vulkan 3D",
        "Vulkan Adaptive"
    });
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GpuPanel::onModeChanged);
    layout->addWidget(modeCombo_);

    // Vulkan-specific settings container
    vulkanSettingsWidget_ = new QWidget(frame);
    auto* vulkanLayout = new QVBoxLayout(vulkanSettingsWidget_);
    vulkanLayout->setContentsMargins(0, 0, 0, 0);
    vulkanLayout->setSpacing(8);

    // Shader complexity slider
    auto* shaderLabel = new QLabel("셰이더 복잡도", vulkanSettingsWidget_);
    shaderLabel->setStyleSheet(styles::kSettingsLabel);
    vulkanLayout->addWidget(shaderLabel);

    auto* sliderLayout = new QHBoxLayout();
    shaderComplexitySlider_ = new QSlider(Qt::Horizontal, vulkanSettingsWidget_);
    shaderComplexitySlider_->setRange(1, 5);
    shaderComplexitySlider_->setValue(1);
    shaderComplexitySlider_->setTickPosition(QSlider::TicksBelow);
    shaderComplexitySlider_->setTickInterval(1);

    shaderComplexityLabel_ = new QLabel("레벨 1", vulkanSettingsWidget_);
    shaderComplexityLabel_->setStyleSheet(styles::kSettingsLabel);
    shaderComplexityLabel_->setFixedWidth(60);

    connect(shaderComplexitySlider_, &QSlider::valueChanged, this, [this](int val) {
        shaderComplexityLabel_->setText(QString("레벨 %1").arg(val));
    });

    sliderLayout->addWidget(shaderComplexitySlider_);
    sliderLayout->addWidget(shaderComplexityLabel_);
    vulkanLayout->addLayout(sliderLayout);

    // Adaptive mode
    auto* adaptiveLabel = new QLabel("적응형 모드", vulkanSettingsWidget_);
    adaptiveLabel->setStyleSheet(styles::kSettingsLabel);
    vulkanLayout->addWidget(adaptiveLabel);

    adaptiveModeCombo_ = new QComboBox(vulkanSettingsWidget_);
    adaptiveModeCombo_->addItems({"Variable (+5%/20s)", "Switch (20%/80%)", "Coil Whine"});
    vulkanLayout->addWidget(adaptiveModeCombo_);

    // Coil Whine Frequency (for COIL_WHINE adaptive mode)
    coilFreqLabel_ = new QLabel("코일 주파수 (Hz)", vulkanSettingsWidget_);
    coilFreqLabel_->setStyleSheet(styles::kSettingsLabel);
    vulkanLayout->addWidget(coilFreqLabel_);

    auto* coilFreqRow = new QHBoxLayout();
    coilFreqSpin_ = new QSpinBox(vulkanSettingsWidget_);
    coilFreqSpin_->setRange(0, 15000);
    coilFreqSpin_->setValue(100);
    coilFreqSpin_->setSuffix(" Hz");
    coilFreqSpin_->setToolTip("0 = sweep mode");
    coilFreqRow->addWidget(coilFreqSpin_);
    vulkanLayout->addLayout(coilFreqRow);

    // Initially hidden -- only shown when adaptive mode is "Coil Whine"
    coilFreqLabel_->setVisible(false);
    coilFreqSpin_->setVisible(false);

    connect(adaptiveModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        bool isCoilWhine = (index == 2);
        coilFreqLabel_->setVisible(isCoilWhine);
        coilFreqSpin_->setVisible(isCoilWhine);
    });

    // Multi-GPU checkbox
    multiGpuCheck_ = new QCheckBox("멀티 GPU (감지된 모든 GPU)", vulkanSettingsWidget_);
    multiGpuCheck_->setStyleSheet("color: #C9D1D9; border: none; background: transparent;");
    vulkanLayout->addWidget(multiGpuCheck_);

    vulkanSettingsWidget_->setVisible(false);
    layout->addWidget(vulkanSettingsWidget_);

    // Duration
    auto* durationLabel = new QLabel("테스트 시간", frame);
    durationLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(durationLabel);

    durationCombo_ = new QComboBox(frame);
    durationCombo_->setAccessibleDescription("gpu_duration_combo");
    durationCombo_->addItem("1분", 60);
    durationCombo_->addItem("5분", 300);
    durationCombo_->addItem("10분", 600);
    durationCombo_->addItem("30분", 1800);
    durationCombo_->addItem("1시간", 3600);
    durationCombo_->addItem("무제한", 0);
    durationCombo_->setCurrentIndex(1);
    layout->addWidget(durationCombo_);

    layout->addSpacing(20);

    startStopBtn_ = new QPushButton("테스트 시작", frame);
    startStopBtn_->setAccessibleDescription("gpu_start_stop_btn");
    startStopBtn_->setCursor(Qt::PointingHandCursor);
    startStopBtn_->setFixedHeight(48);
    startStopBtn_->setStyleSheet(
        styles::kStartButton
    );
    connect(startStopBtn_, &QPushButton::clicked, this, &GpuPanel::onStartStopClicked);
    layout->addWidget(startStopBtn_);

    layout->addStretch();

    return frame;
}

QFrame* GpuPanel::createMonitoringSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("GPU 모니터링", frame);
    title->setStyleSheet(styles::kSectionTitle);
    layout->addWidget(title);

    // Status banner (hidden by default, shown when GPU backend unavailable or on error)
    statusBanner_ = new QLabel(frame);
    statusBanner_->setAccessibleDescription("gpu_status");
    statusBanner_->setStyleSheet(styles::kWarningBanner);
    statusBanner_->setWordWrap(true);
    statusBanner_->setVisible(false);
    layout->addWidget(statusBanner_);

    // Top metrics row
    auto* metricsLayout = new QHBoxLayout();
    metricsLayout->setSpacing(16);

    // GPU usage gauge
    gpuUsageGauge_ = new CircularGauge(frame);
    gpuUsageGauge_->setAccessibleDescription("gpu_usage_gauge");
    gpuUsageGauge_->setLabel("GPU 사용률");
    gpuUsageGauge_->setFixedSize(140, 140);
    metricsLayout->addWidget(gpuUsageGauge_);

    // Metrics cards
    auto* metricsGrid = new QVBoxLayout();
    metricsGrid->setSpacing(8);

    auto createMetricCard = [frame](const QString& label, const QString& val) -> QLabel* {
        auto* card = new QFrame(frame);
        card->setStyleSheet(styles::kCardFrame);
        auto* cl = new QHBoxLayout(card);
        cl->setContentsMargins(12, 8, 12, 8);
        auto* lbl = new QLabel(label, card);
        lbl->setStyleSheet(styles::kPanelSubtitle);
        auto* v = new QLabel(val, card);
        v->setStyleSheet(styles::kSectionTitle);
        v->setAlignment(Qt::AlignRight);
        cl->addWidget(lbl);
        cl->addStretch();
        cl->addWidget(v);
        return v;
    };

    gflopsLabel_ = createMetricCard("GFLOPS", "0.00");
    gflopsLabel_->setAccessibleDescription("gpu_gflops_value");
    metricsGrid->addWidget(gflopsLabel_->parentWidget());
    tempLabel_ = createMetricCard("온도", "-- C");
    tempLabel_->setAccessibleDescription("gpu_temp_value");
    metricsGrid->addWidget(tempLabel_->parentWidget());
    vramLabel_ = createMetricCard("VRAM 사용", "-- %");
    vramLabel_->setAccessibleDescription("gpu_vram_usage");
    metricsGrid->addWidget(vramLabel_->parentWidget());
    fpsLabel_ = createMetricCard("FPS", "--");
    fpsLabel_->setAccessibleDescription("gpu_fps_value");
    metricsGrid->addWidget(fpsLabel_->parentWidget());
    artifactLabel_ = createMetricCard("아티팩트", "0");
    artifactLabel_->setAccessibleDescription("gpu_artifact_count");
    metricsGrid->addWidget(artifactLabel_->parentWidget());
    vramErrorsLabel_ = createMetricCard("VRAM 오류", "0");
    vramErrorsLabel_->setAccessibleDescription("gpu_vram_errors");
    metricsGrid->addWidget(vramErrorsLabel_->parentWidget());

    metricsLayout->addLayout(metricsGrid, 1);
    layout->addLayout(metricsLayout);

    // VRAM progress bar
    auto* vramTitle = new QLabel("VRAM 사용률", frame);
    vramTitle->setStyleSheet(styles::kPanelSubtitle);
    layout->addWidget(vramTitle);

    vramBar_ = new QProgressBar(frame);
    vramBar_->setAccessibleDescription("gpu_vram_bar");
    vramBar_->setRange(0, 100);
    vramBar_->setValue(0);
    vramBar_->setTextVisible(true);
    vramBar_->setFixedHeight(20);
    layout->addWidget(vramBar_);

    // GFLOPS chart
    gflopsChart_ = new RealtimeChart(frame);
    gflopsChart_->setAccessibleDescription("gpu_gflops_chart");
    gflopsChart_->setTitle("시간별 GPU 성능");
    gflopsChart_->setUnit("GFLOPS");
    gflopsChart_->setLineColor(QColor(41, 128, 185));
    gflopsChart_->setMinimumHeight(200);
    layout->addWidget(gflopsChart_, 1);

    return frame;
}

IEngine* GpuPanel::engine() const {
    return engine_.get();
}

void GpuPanel::setSensorManager(SensorManager* mgr)
{
    sensorMgr_ = mgr;
}

void GpuPanel::onModeChanged(int index)
{
    // Show Vulkan settings when Vulkan modes are selected (indices 6 and 7)
    bool isVulkanMode = (index >= 6);
    vulkanSettingsWidget_->setVisible(isVulkanMode);

    // FPS and Artifact labels are only relevant for Vulkan 3D modes
    if (isVulkanMode) {
        fpsLabel_->setText("--");
        artifactLabel_->setText("0");
    } else {
        fpsLabel_->setText("N/A");
        artifactLabel_->setText("N/A");
    }
}

void GpuPanel::onStartStopClicked()
{
    isRunning_ = !isRunning_;

    if (isRunning_) {
        startStopBtn_->setText("테스트 중지");
        startStopBtn_->setStyleSheet(
            styles::kStopButton
        );

        // Map combo index to GpuStressMode
        int modeIdx = modeCombo_->currentIndex();
        GpuStressMode mode;
        switch (modeIdx) {
            case 0: mode = GpuStressMode::MATRIX_MUL; break;
            case 1: mode = GpuStressMode::MATRIX_MUL_FP64; break;
            case 2: mode = GpuStressMode::FMA_STRESS; break;
            case 3: mode = GpuStressMode::TRIG_STRESS; break;
            case 4: mode = GpuStressMode::VRAM_TEST; break;
            case 5: mode = GpuStressMode::MIXED; break;
            case 6: mode = GpuStressMode::VULKAN_3D; break;
            case 7: mode = GpuStressMode::VULKAN_ADAPTIVE; break;
            default: mode = GpuStressMode::MATRIX_MUL; break;
        }

        // Configure Vulkan-specific settings if applicable
        if (mode == GpuStressMode::VULKAN_3D || mode == GpuStressMode::VULKAN_ADAPTIVE) {
            engine_->set_shader_complexity(shaderComplexitySlider_->value());
            if (mode == GpuStressMode::VULKAN_ADAPTIVE) {
                AdaptiveMode am;
                switch (adaptiveModeCombo_->currentIndex()) {
                    case 1:  am = AdaptiveMode::SWITCH;     break;
                    case 2:  am = AdaptiveMode::COIL_WHINE; break;
                    default: am = AdaptiveMode::VARIABLE;    break;
                }
                engine_->set_adaptive_mode(am);
                if (am == AdaptiveMode::COIL_WHINE) {
                    engine_->set_coil_whine_freq(static_cast<float>(coilFreqSpin_->value()));
                }
            }
        }

        int durationSec = durationCombo_->currentData().toInt();

        // Clear chart for the new test
        gflopsChart_->clear();

        // Start engine
        if (!engine_->start(mode, durationSec)) {
            QString errMsg = QString::fromStdString(engine_->last_error());
            QMessageBox::warning(this, "GPU 테스트 오류", errMsg);
            statusBanner_->setText(errMsg.isEmpty()
                ? "GPU 백엔드 사용 불가 (이 빌드에서 OpenCL/Vulkan이 활성화되지 않음)"
                : errMsg);
            statusBanner_->setVisible(true);
            isRunning_ = false;
            startStopBtn_->setText("테스트 시작");
            startStopBtn_->setStyleSheet(styles::kStartButton);
            return;
        }

        // Clear any previous error banner
        statusBanner_->setVisible(false);

        // Start monitoring timer
        monitorTimer_->start(500);

        emit testStartRequested(gpuSelectCombo_->currentText(), modeCombo_->currentText(), durationSec);
    } else {
        startStopBtn_->setText("테스트 시작");
        startStopBtn_->setStyleSheet(styles::kStartButton);

        // Stop engine
        engine_->stop();
        monitorTimer_->stop();
        emit testStopRequested();
    }
}

void GpuPanel::updateMonitoring()
{
    if (!engine_ || !engine_->is_running()) {
        // Engine stopped on its own (duration reached)
        if (isRunning_) {
            isRunning_ = false;
            startStopBtn_->setText("테스트 시작");
            startStopBtn_->setStyleSheet(styles::kStartButton);
            monitorTimer_->stop();
            emit testStopRequested();
        }
        return;
    }

    auto m = engine_->get_metrics();

    // Update GFLOPS
    gflopsLabel_->setText(QString::number(m.gflops, 'f', 2));
    gflopsChart_->addPoint(m.gflops);

    // Update GPU usage gauge
    gpuUsageGauge_->setValue(m.gpu_usage_pct);

    // Update temperature (with SensorManager fallback)
    double gpuTemp = m.temperature;
    if (sensorMgr_ && gpuTemp <= 0) {
        gpuTemp = sensorMgr_->get_gpu_temperature();
    }
    if (gpuTemp > 0)
        tempLabel_->setText(QString::number(gpuTemp, 'f', 1) + " °C");

    // Update VRAM
    vramLabel_->setText(QString::number(m.vram_usage_pct, 'f', 1) + "%");
    vramBar_->setValue(static_cast<int>(m.vram_usage_pct));

    // Update FPS
    if (m.fps > 0)
        fpsLabel_->setText(QString::number(m.fps, 'f', 1));

    // Update artifact count with error styling
    artifactLabel_->setText(QString::number(m.artifact_count));
    if (m.artifact_count > 0) {
        artifactLabel_->setStyleSheet(styles::kErrorText);
    } else {
        artifactLabel_->setStyleSheet(styles::kSectionTitle);
    }

    // Update VRAM errors with error styling
    vramErrorsLabel_->setText(QString::number(m.vram_errors));
    if (m.vram_errors > 0) {
        vramErrorsLabel_->setStyleSheet(styles::kErrorText);
        if (!statusBanner_->isVisible()) {
            statusBanner_->setText(QString("GPU 오류 감지: VRAM 오류 %1건, 아티팩트 %2건")
                .arg(m.vram_errors).arg(m.artifact_count));
            statusBanner_->setStyleSheet(
                "color: #E74C3C; font-size: 13px; font-weight: bold; border: 1px solid #E74C3C; "
                "border-radius: 6px; padding: 8px; background-color: rgba(231,76,60,0.1);");
            statusBanner_->setVisible(true);
        }
    } else {
        vramErrorsLabel_->setStyleSheet(styles::kSectionTitle);
    }
}

}} // namespace occt::gui
