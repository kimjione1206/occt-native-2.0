#include "cpu_panel.h"
#include "panel_styles.h"
#include "../widgets/realtime_chart.h"
#include "../../engines/cpu_engine.h"
#include "../../monitor/sensor_manager.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QThread>

namespace occt { namespace gui {

CpuPanel::CpuPanel(QWidget* parent)
    : QWidget(parent)
    , engine_(std::make_unique<CpuEngine>())
{
    setupUi();

    monitorTimer_ = new QTimer(this);
    connect(monitorTimer_, &QTimer::timeout, this, &CpuPanel::updateMonitoring);
}

CpuPanel::~CpuPanel()
{
    if (engine_ && engine_->is_running()) {
        engine_->stop();
    }
}

IEngine* CpuPanel::engine() const
{
    return engine_.get();
}

void CpuPanel::setupUi()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    // Left: Settings
    mainLayout->addWidget(createSettingsSection());

    // Right: Monitoring
    mainLayout->addWidget(createMonitoringSection(), 1);
}

QFrame* CpuPanel::createSettingsSection()
{
    auto* frame = new QFrame();
    frame->setFixedWidth(320);
    frame->setStyleSheet(styles::kSectionFrame);

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    // Title
    auto* title = new QLabel("CPU 스트레스 테스트", frame);
    title->setStyleSheet(styles::kPanelTitle);
    layout->addWidget(title);

    auto* subtitle = new QLabel("CPU 스트레스 테스트 설정 및 실행", frame);
    subtitle->setStyleSheet(styles::kPanelSubtitle);
    layout->addWidget(subtitle);

    layout->addSpacing(10);

    // Mode selection
    auto* modeLabel = new QLabel("테스트 모드", frame);
    modeLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(modeLabel);

    modeCombo_ = new QComboBox(frame);
    modeCombo_->setAccessibleDescription("cpu_mode_combo");
    modeCombo_->addItems({
        "Auto (Best ISA)",
        "AVX2 FMA",
        "AVX-512 FMA",
        "AVX (No FMA)",
        "SSE",
        "Linpack",
        "Prime",
        "All",
        "Cache Only",
        "Large Data Set"
    });
    layout->addWidget(modeCombo_);

    // Load Pattern selection
    auto* patternLabel = new QLabel("부하 패턴", frame);
    patternLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(patternLabel);

    loadPatternCombo_ = new QComboBox(frame);
    loadPatternCombo_->setAccessibleDescription("cpu_load_pattern_combo");
    loadPatternCombo_->addItem("일정", QStringLiteral("STEADY"));
    loadPatternCombo_->addItem("가변 (10분마다 변경)", QStringLiteral("VARIABLE"));
    loadPatternCombo_->addItem("코어 순환", QStringLiteral("CORE_CYCLING"));
    layout->addWidget(loadPatternCombo_);

    // Thread count
    auto* threadLabel = new QLabel("스레드", frame);
    threadLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(threadLabel);

    int maxThreads = QThread::idealThreadCount();

    auto* threadRow = new QHBoxLayout();
    threadSlider_ = new QSlider(Qt::Horizontal, frame);
    threadSlider_->setRange(1, maxThreads);
    threadSlider_->setValue(maxThreads);
    connect(threadSlider_, &QSlider::valueChanged, this, &CpuPanel::onThreadSliderChanged);

    threadValueLabel_ = new QLabel(QString::number(maxThreads), frame);
    threadValueLabel_->setAccessibleDescription("cpu_thread_count");
    threadValueLabel_->setFixedWidth(30);
    threadValueLabel_->setAlignment(Qt::AlignCenter);
    threadValueLabel_->setStyleSheet(styles::kStatusIdle);

    threadRow->addWidget(threadSlider_, 1);
    threadRow->addWidget(threadValueLabel_);
    layout->addLayout(threadRow);

    auto* threadInfo = new QLabel(QString("가용: %1 논리 코어").arg(maxThreads), frame);
    threadInfo->setStyleSheet(styles::kSmallInfo);
    layout->addWidget(threadInfo);

    // Duration
    auto* durationLabel = new QLabel("테스트 시간", frame);
    durationLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(durationLabel);

    durationCombo_ = new QComboBox(frame);
    durationCombo_->setAccessibleDescription("cpu_duration_combo");
    durationCombo_->addItem("1분", 60);
    durationCombo_->addItem("5분", 300);
    durationCombo_->addItem("10분", 600);
    durationCombo_->addItem("30분", 1800);
    durationCombo_->addItem("1시간", 3600);
    durationCombo_->addItem("무제한", 0);
    durationCombo_->setCurrentIndex(1); // default 5 min
    layout->addWidget(durationCombo_);

    layout->addSpacing(20);

    // Start/Stop button
    startStopBtn_ = new QPushButton("테스트 시작", frame);
    startStopBtn_->setAccessibleDescription("cpu_start_stop_btn");
    startStopBtn_->setCursor(Qt::PointingHandCursor);
    startStopBtn_->setFixedHeight(48);
    startStopBtn_->setStyleSheet(
        styles::kStartButton
    );
    connect(startStopBtn_, &QPushButton::clicked, this, &CpuPanel::onStartStopClicked);
    layout->addWidget(startStopBtn_);

    layout->addStretch();

    return frame;
}

QFrame* CpuPanel::createMonitoringSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    // Title
    auto* title = new QLabel("실시간 모니터링", frame);
    title->setStyleSheet(styles::kSectionTitle);
    layout->addWidget(title);

    // Metrics row
    auto* metricsLayout = new QHBoxLayout();
    metricsLayout->setSpacing(16);

    auto createMetric = [frame](const QString& label, const QString& initVal) -> QLabel* {
        auto* card = new QFrame(frame);
        card->setStyleSheet(styles::kCardFrame);
        card->setMinimumWidth(100);
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(12, 8, 12, 8);
        auto* lbl = new QLabel(label, card);
        lbl->setStyleSheet(styles::kSmallInfo);
        auto* val = new QLabel(initVal, card);
        val->setStyleSheet(styles::kMetricValue);
        cl->addWidget(lbl);
        cl->addWidget(val);
        return val;
    };

    gflopsValueLabel_ = createMetric("GFLOPS", "0.00");
    gflopsValueLabel_->setAccessibleDescription("cpu_gflops_value");
    metricsLayout->addWidget(gflopsValueLabel_->parentWidget());
    tempLabel_ = createMetric("온도", "-- \u00B0C");
    tempLabel_->setAccessibleDescription("cpu_temp_value");
    metricsLayout->addWidget(tempLabel_->parentWidget());
    powerLabel_ = createMetric("전력", "-- W");
    powerLabel_->setAccessibleDescription("cpu_power_value");
    metricsLayout->addWidget(powerLabel_->parentWidget());
    freqLabel_ = createMetric("주파수", "-- MHz");
    freqLabel_->setAccessibleDescription("cpu_freq_value");
    metricsLayout->addWidget(freqLabel_->parentWidget());

    // Error count metric (in red)
    auto* errorCard = new QFrame(frame);
    errorCard->setStyleSheet(styles::kCardFrame);
    auto* ecl = new QVBoxLayout(errorCard);
    ecl->setContentsMargins(12, 8, 12, 8);
    auto* errLabel = new QLabel("오류", errorCard);
    errLabel->setStyleSheet(styles::kSmallInfo);
    errorCountLabel_ = new QLabel("0", errorCard);
    errorCountLabel_->setAccessibleDescription("cpu_error_count");
    errorCountLabel_->setStyleSheet("color: #27AE60; font-size: 18px; font-weight: bold; border: none; background: transparent;");
    ecl->addWidget(errLabel);
    ecl->addWidget(errorCountLabel_);
    metricsLayout->addWidget(errorCard);

    layout->addLayout(metricsLayout);

    // GFLOPS chart
    gflopsChart_ = new RealtimeChart(frame);
    gflopsChart_->setAccessibleDescription("cpu_gflops_chart");
    gflopsChart_->setTitle("시간별 GFLOPS");
    gflopsChart_->setUnit("GFLOPS");
    gflopsChart_->setLineColor(QColor(192, 57, 43));
    gflopsChart_->setMinimumHeight(200);
    layout->addWidget(gflopsChart_, 1);

    // Per-core error status grid
    auto* coreTitle = new QLabel("코어별 상태", frame);
    coreTitle->setStyleSheet(styles::kSectionTitle);
    layout->addWidget(coreTitle);

    coreGridFrame_ = new QFrame(frame);
    coreGridFrame_->setStyleSheet(styles::kCardFrame);
    coreGridLayout_ = new QGridLayout(coreGridFrame_);
    coreGridLayout_->setContentsMargins(12, 12, 12, 12);
    coreGridLayout_->setSpacing(6);

    // Initialize with default core count
    rebuildCoreGrid(QThread::idealThreadCount());

    layout->addWidget(coreGridFrame_);

    // Status bar
    auto* statusFrame = new QFrame(frame);
    statusFrame->setStyleSheet(styles::kCardFrame);
    auto* statusLayout = new QHBoxLayout(statusFrame);
    statusLayout->setContentsMargins(12, 8, 12, 8);

    statusLabel_ = new QLabel("대기", statusFrame);
    statusLabel_->setAccessibleDescription("cpu_status");
    statusLabel_->setStyleSheet(styles::kStatusIdle);
    statusLayout->addWidget(statusLabel_);
    statusLayout->addStretch();

    layout->addWidget(statusFrame);

    return frame;
}

void CpuPanel::rebuildCoreGrid(int coreCount)
{
    // Clear existing labels
    for (auto* lbl : coreStatusLabels_) {
        coreGridLayout_->removeWidget(lbl);
        delete lbl;
    }
    coreStatusLabels_.clear();

    int cols = 8; // 8 cores per row
    for (int i = 0; i < coreCount; ++i) {
        auto* lbl = new QLabel(QString("C%1").arg(i), coreGridFrame_);
        lbl->setAccessibleDescription(QString("cpu_core_%1").arg(i));
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setFixedSize(40, 28);
        lbl->setStyleSheet(
            "color: white; background-color: #27AE60; border-radius: 4px; "
            "font-size: 10px; font-weight: bold; border: none;"
        );
        coreGridLayout_->addWidget(lbl, i / cols, i % cols);
        coreStatusLabels_.push_back(lbl);
    }
}

void CpuPanel::updateErrorStatus(int errorCount, const std::vector<bool>& coreErrors)
{
    // Update error count label
    if (errorCount > 0) {
        errorCountLabel_->setText(QString::number(errorCount));
        errorCountLabel_->setStyleSheet(
            "color: #E74C3C; font-size: 18px; font-weight: bold; border: none; background: transparent;"
        );
    } else {
        errorCountLabel_->setText("0");
        errorCountLabel_->setStyleSheet(
            "color: #27AE60; font-size: 18px; font-weight: bold; border: none; background: transparent;"
        );
    }

    // Update per-core status
    int count = static_cast<int>(std::min(coreErrors.size(),
                                          static_cast<size_t>(coreStatusLabels_.size())));
    for (int i = 0; i < count; ++i) {
        if (coreErrors[i]) {
            coreStatusLabels_[i]->setStyleSheet(
                "color: white; background-color: #E74C3C; border-radius: 4px; "
                "font-size: 10px; font-weight: bold; border: none;"
            );
        } else {
            coreStatusLabels_[i]->setStyleSheet(
                "color: white; background-color: #27AE60; border-radius: 4px; "
                "font-size: 10px; font-weight: bold; border: none;"
            );
        }
    }
}

void CpuPanel::setSensorManager(SensorManager* mgr)
{
    sensorMgr_ = mgr;
    if (engine_) {
        engine_->set_sensor_manager(mgr);
    }
}

void CpuPanel::onStartStopClicked()
{
    isRunning_ = !isRunning_;

    if (isRunning_) {
        startStopBtn_->setText("테스트 중지");
        startStopBtn_->setStyleSheet(
            styles::kStopButton
        );

        // Update status label and reset chart for new test
        statusLabel_->setText("실행 중");
        statusLabel_->setStyleSheet(styles::kStatusRunning);
        gflopsChart_->clear();

        // Rebuild core grid for selected thread count
        int threads = threadSlider_->value();
        rebuildCoreGrid(threads);

        // Map combo index to CpuStressMode
        int modeIdx = modeCombo_->currentIndex();
        CpuStressMode mode;
        switch (modeIdx) {
            case 0: mode = CpuStressMode::AUTO; break;
            case 1: mode = CpuStressMode::AVX2_FMA; break;
            case 2: mode = CpuStressMode::AVX512_FMA; break;
            case 3: mode = CpuStressMode::AVX_FLOAT; break;
            case 4: mode = CpuStressMode::SSE_FLOAT; break;
            case 5: mode = CpuStressMode::LINPACK; break;
            case 6: mode = CpuStressMode::PRIME; break;
            case 7: mode = CpuStressMode::ALL; break;
            case 8: mode = CpuStressMode::CACHE_ONLY; break;
            case 9: mode = CpuStressMode::LARGE_DATA_SET; break;
            default: mode = CpuStressMode::AUTO; break;
        }

        LoadPattern pattern;
        switch (loadPatternCombo_->currentIndex()) {
            case 1: pattern = LoadPattern::VARIABLE; break;
            case 2: pattern = LoadPattern::CORE_CYCLING; break;
            default: pattern = LoadPattern::STEADY; break;
        }

        int durationSec = durationCombo_->currentData().toInt();

        // Start engine
        engine_->start(mode, threads, durationSec, pattern);

        // Start monitoring timer
        monitorTimer_->start(500);

        emit testStartRequested(modeCombo_->currentText(),
                                loadPatternCombo_->currentData().toString(),
                                threads, durationSec);
    } else {
        startStopBtn_->setText("테스트 시작");
        startStopBtn_->setStyleSheet(styles::kStartButton);

        // Update status label
        statusLabel_->setText("대기");
        statusLabel_->setStyleSheet(styles::kStatusIdle);

        // Stop engine
        engine_->stop();
        monitorTimer_->stop();
        emit testStopRequested();
    }
}

void CpuPanel::onThreadSliderChanged(int value)
{
    threadValueLabel_->setText(QString::number(value));
}

void CpuPanel::updateMonitoring()
{
    if (!engine_ || !engine_->is_running()) {
        // Engine stopped on its own (duration reached)
        if (isRunning_) {
            isRunning_ = false;
            startStopBtn_->setText("테스트 시작");
            startStopBtn_->setStyleSheet(styles::kStartButton);
            statusLabel_->setText("대기");
            statusLabel_->setStyleSheet(styles::kStatusIdle);
            monitorTimer_->stop();
            emit testStopRequested();
        }
        return;
    }

    auto m = engine_->get_metrics();

    // Update GFLOPS
    gflopsValueLabel_->setText(QString::number(m.gflops, 'f', 2));
    gflopsChart_->addPoint(m.gflops);

    // Use SensorManager for temperature/power if engine doesn't provide them
    double cpuTemp = m.temperature;
    double cpuPower = m.power_watts;

    bool powerEstimated = m.power_estimated;
    if (sensorMgr_) {
        if (cpuTemp <= 0) cpuTemp = sensorMgr_->get_cpu_temperature();
        if (cpuPower <= 0) cpuPower = sensorMgr_->get_cpu_power();
        if (cpuPower > 0 && m.power_watts <= 0)
            powerEstimated = sensorMgr_->is_cpu_power_estimated();
    }

    if (cpuTemp > 0)
        tempLabel_->setText(QString::number(cpuTemp, 'f', 1) + " \u00B0C");
    else
        tempLabel_->setText("N/A");

    if (cpuPower > 0) {
        QString powerText = QString::number(cpuPower, 'f', 1) + " W";
        if (powerEstimated)
            powerText += " (추정)";
        powerLabel_->setText(powerText);
    } else {
        powerLabel_->setText("N/A");
    }

    // Update CPU frequency from SensorManager if available
    bool freqUpdated = false;
    if (sensorMgr_) {
        auto readings = sensorMgr_->get_all_readings();
        for (const auto& r : readings) {
            if (r.category == "CPU" && r.unit == "MHz" && r.value > 0) {
                freqLabel_->setText(QString::number(r.value, 'f', 0) + " MHz");
                freqUpdated = true;
                break;
            }
        }
    }
    if (!freqUpdated)
        freqLabel_->setText("N/A");

    // Update error status
    updateErrorStatus(m.error_count, m.core_has_error);
}

}} // namespace occt::gui
