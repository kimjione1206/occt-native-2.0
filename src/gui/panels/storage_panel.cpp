#include "storage_panel.h"
#include "panel_styles.h"
#include "../widgets/realtime_chart.h"
#include "../../engines/storage_engine.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QDir>
#include <QMessageBox>

namespace occt { namespace gui {

StoragePanel::StoragePanel(QWidget* parent)
    : QWidget(parent)
    , engine_(std::make_unique<StorageEngine>())
{
    setupUi();

    monitorTimer_ = new QTimer(this);
    connect(monitorTimer_, &QTimer::timeout, this, &StoragePanel::updateMonitoring);
}

StoragePanel::~StoragePanel()
{
    if (engine_ && engine_->is_running()) {
        engine_->stop();
    }
}

IEngine* StoragePanel::engine() const
{
    return engine_.get();
}

void StoragePanel::setupUi()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    mainLayout->addWidget(createSettingsSection());
    mainLayout->addWidget(createMonitoringSection(), 1);
}

QFrame* StoragePanel::createSettingsSection()
{
    auto* frame = new QFrame();
    frame->setFixedWidth(320);
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("저장장치 테스트", frame);
    title->setStyleSheet(styles::kPanelTitle);
    layout->addWidget(title);

    auto* subtitle = new QLabel("저장장치 I/O 스트레스 테스트 및 검증", frame);
    subtitle->setStyleSheet(styles::kPanelSubtitle);
    layout->addWidget(subtitle);

    layout->addSpacing(10);

    // Mode
    auto* modeLabel = new QLabel("테스트 모드", frame);
    modeLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(modeLabel);

    modeCombo_ = new QComboBox(frame);
    modeCombo_->setAccessibleDescription("storage_mode_combo");
    modeCombo_->addItems({
        "Sequential Read",
        "Sequential Write",
        "Random Read",
        "Random Write",
        "Mixed Read/Write",
        "Verify Sequential",
        "Verify Random",
        "Fill & Verify",
        "Butterfly Verify"
    });
    layout->addWidget(modeCombo_);

    // Block size
    auto* blockLabel = new QLabel("블록 크기", frame);
    blockLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(blockLabel);

    blockSizeCombo_ = new QComboBox(frame);
    blockSizeCombo_->addItems({"4 KB", "8 KB", "64 KB", "128 KB", "1 MB", "4 MB"});
    blockSizeCombo_->setCurrentIndex(0);
    layout->addWidget(blockSizeCombo_);

    // Direct I/O
    directIOCheck_ = new QCheckBox("직접 I/O (캐시 우회)", frame);
    directIOCheck_->setChecked(true);
    layout->addWidget(directIOCheck_);

    // Queue Depth
    auto* qdLabel = new QLabel("큐 깊이", frame);
    qdLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(qdLabel);

    queueDepthSpin_ = new QSpinBox(frame);
    queueDepthSpin_->setRange(1, 256);
    queueDepthSpin_->setValue(32);
    layout->addWidget(queueDepthSpin_);

    // Duration
    auto* durationLabel = new QLabel("테스트 시간", frame);
    durationLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(durationLabel);

    durationCombo_ = new QComboBox(frame);
    durationCombo_->addItem("1분", 60);
    durationCombo_->addItem("5분", 300);
    durationCombo_->addItem("10분", 600);
    durationCombo_->addItem("30분", 1800);
    durationCombo_->addItem("1시간", 3600);
    durationCombo_->addItem("무제한", 0);
    durationCombo_->setCurrentIndex(1); // default 5 min
    layout->addWidget(durationCombo_);

    layout->addSpacing(20);

    startStopBtn_ = new QPushButton("테스트 시작", frame);
    startStopBtn_->setAccessibleDescription("storage_start_stop_btn");
    startStopBtn_->setCursor(Qt::PointingHandCursor);
    startStopBtn_->setFixedHeight(48);
    startStopBtn_->setStyleSheet(
        styles::kStartButton
    );
    connect(startStopBtn_, &QPushButton::clicked, this, &StoragePanel::onStartStopClicked);
    layout->addWidget(startStopBtn_);

    layout->addStretch();

    return frame;
}

QFrame* StoragePanel::createMonitoringSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("저장장치 모니터링", frame);
    title->setStyleSheet(styles::kSectionTitle);
    layout->addWidget(title);

    // Status label (hidden by default, shown on error)
    statusLabel_ = new QLabel(frame);
    statusLabel_->setAccessibleDescription("storage_status");
    statusLabel_->setWordWrap(true);
    statusLabel_->setVisible(false);
    layout->addWidget(statusLabel_);

    // Metrics rows (split 8 cards into 2 rows of 4)
    auto createMetric = [frame](const QString& label, const QString& val) -> QLabel* {
        auto* card = new QFrame(frame);
        card->setStyleSheet(styles::kCardFrame);
        card->setMinimumWidth(90);
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(12, 8, 12, 8);
        auto* lbl = new QLabel(label, card);
        lbl->setStyleSheet(styles::kSmallInfo);
        lbl->setWordWrap(true);
        auto* v = new QLabel(val, card);
        v->setStyleSheet(styles::kPanelTitle);
        cl->addWidget(lbl);
        cl->addWidget(v);
        return v;
    };

    // Row 1: IOPS, Throughput, Avg Latency, Blocks Verified
    auto* metricsRow1 = new QHBoxLayout();
    metricsRow1->setSpacing(12);
    iopsLabel_ = createMetric("IOPS", "0");
    iopsLabel_->setAccessibleDescription("storage_iops_value");
    metricsRow1->addWidget(iopsLabel_->parentWidget());
    throughputLabel_ = createMetric("처리량", "0 MB/s");
    throughputLabel_->setAccessibleDescription("storage_throughput_value");
    metricsRow1->addWidget(throughputLabel_->parentWidget());
    latencyLabel_ = createMetric("평균 지연시간", "-- ms");
    latencyLabel_->setAccessibleDescription("storage_latency_value");
    metricsRow1->addWidget(latencyLabel_->parentWidget());
    blocksVerifiedLabel_ = createMetric("검증된 블록", "0");
    blocksVerifiedLabel_->setAccessibleDescription("storage_blocks_verified");
    metricsRow1->addWidget(blocksVerifiedLabel_->parentWidget());

    // Row 2: Verify Errors, CRC Errors, Pattern Errors, Verify Speed
    auto* metricsRow2 = new QHBoxLayout();
    metricsRow2->setSpacing(12);
    verifyErrorsLabel_ = createMetric("검증 오류", "0");
    verifyErrorsLabel_->setAccessibleDescription("storage_verify_errors");
    metricsRow2->addWidget(verifyErrorsLabel_->parentWidget());
    crcErrorsLabel_ = createMetric("CRC 오류", "0");
    crcErrorsLabel_->setAccessibleDescription("storage_crc_errors");
    metricsRow2->addWidget(crcErrorsLabel_->parentWidget());
    patternErrorsLabel_ = createMetric("패턴 오류", "0");
    patternErrorsLabel_->setAccessibleDescription("storage_pattern_errors");
    metricsRow2->addWidget(patternErrorsLabel_->parentWidget());
    verifyMbsLabel_ = createMetric("검증 속도", "-- MB/s");
    verifyMbsLabel_->setAccessibleDescription("storage_verify_mbs");
    metricsRow2->addWidget(verifyMbsLabel_->parentWidget());

    layout->addLayout(metricsRow1);
    layout->addLayout(metricsRow2);

    // IOPS chart
    iopsChart_ = new RealtimeChart(frame);
    iopsChart_->setTitle("시간별 IOPS");
    iopsChart_->setUnit("IOPS");
    iopsChart_->setLineColor(QColor(230, 126, 34));
    iopsChart_->setMinimumHeight(180);
    layout->addWidget(iopsChart_, 1);

    // Throughput chart
    throughputChart_ = new RealtimeChart(frame);
    throughputChart_->setTitle("시간별 처리량");
    throughputChart_->setUnit("MB/s");
    throughputChart_->setLineColor(QColor(46, 204, 113));
    throughputChart_->setMinimumHeight(180);
    layout->addWidget(throughputChart_, 1);

    return frame;
}

void StoragePanel::onStartStopClicked()
{
    isRunning_ = !isRunning_;

    if (isRunning_) {
        startStopBtn_->setText("테스트 중지");
        startStopBtn_->setStyleSheet(
            styles::kStopButton
        );

        // Map combo index to StorageMode
        int modeIdx = modeCombo_->currentIndex();
        StorageMode mode;
        switch (modeIdx) {
            case 0: mode = StorageMode::SEQ_READ; break;
            case 1: mode = StorageMode::SEQ_WRITE; break;
            case 2: mode = StorageMode::RAND_READ; break;
            case 3: mode = StorageMode::RAND_WRITE; break;
            case 4: mode = StorageMode::MIXED; break;
            case 5: mode = StorageMode::VERIFY_SEQ; break;
            case 6: mode = StorageMode::VERIFY_RAND; break;
            case 7: mode = StorageMode::FILL_VERIFY; break;
            case 8: mode = StorageMode::BUTTERFLY; break;
            default: mode = StorageMode::SEQ_READ; break;
        }

        int queueDepth = queueDepthSpin_->value();

        // Configure block size from combo (parse "4 KB", "1 MB", etc.)
        QString blockText = blockSizeCombo_->currentText().trimmed();
        uint32_t blockSizeKb = 4;
        if (blockText.contains("MB")) {
            blockSizeKb = blockText.remove("MB").trimmed().toUInt() * 1024;
        } else {
            blockSizeKb = blockText.remove("KB").trimmed().toUInt();
        }
        engine_->set_block_size_kb(blockSizeKb);
        engine_->set_direct_io(directIOCheck_->isChecked());

        // Use temp directory for test file (engine expects a directory path)
        QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        std::string testPath = tempPath.toStdString();

        int durationSec = durationCombo_->currentData().toInt();
        if (!engine_->start(mode, testPath, 256, queueDepth, durationSec)) {
            QMessageBox::warning(this, "저장장치 테스트 오류",
                QString::fromStdString(engine_->last_error()));
            isRunning_ = false;
            startStopBtn_->setText("테스트 시작");
            return;
        }
        monitorTimer_->start(500);

        emit testStartRequested(modeCombo_->currentText(), directIOCheck_->isChecked(), queueDepthSpin_->value());
    } else {
        startStopBtn_->setText("테스트 시작");
        startStopBtn_->setStyleSheet(styles::kStartButton);

        engine_->stop();
        monitorTimer_->stop();
        emit testStopRequested();
    }
}

void StoragePanel::updateMonitoring()
{
    if (!engine_ || !engine_->is_running()) {
        // Engine stopped on its own (test completed)
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

    // Check for error state
    std::string lastErr = engine_->last_error();
    if (m.state == "error" || !lastErr.empty()) {
        QString errMsg = lastErr.empty()
            ? "저장장치 엔진에 오류가 발생했습니다"
            : QString::fromStdString(lastErr);
        statusLabel_->setText(errMsg);
        statusLabel_->setStyleSheet(
            "color: #E74C3C; font-size: 13px; font-weight: bold; border: 1px solid #E74C3C; "
            "border-radius: 6px; padding: 8px; background-color: rgba(231,76,60,0.1);");
        statusLabel_->setVisible(true);
    } else if (statusLabel_->isVisible() && m.state != "error") {
        statusLabel_->setVisible(false);
    }

    if (m.state == "preparing") {
        // Show file preparation progress
        iopsLabel_->setText("준비 중...");
        double prep_pct = m.progress_pct;
        throughputLabel_->setText(QString("테스트 파일 생성 중: %1%").arg(prep_pct, 0, 'f', 0));
        return;
    }

    // Update IOPS
    iopsLabel_->setText(QString::number(m.iops, 'f', 0));
    iopsChart_->addPoint(m.iops);

    // Update throughput (use whichever is higher: read or write)
    double throughput = std::max(m.read_mbs, m.write_mbs);
    if (m.read_mbs > 0 && m.write_mbs > 0)
        throughput = m.read_mbs + m.write_mbs;
    throughputLabel_->setText(QString::number(throughput, 'f', 1) + " MB/s");
    throughputChart_->addPoint(throughput);

    // Update latency (convert from microseconds to milliseconds)
    double latency_ms = m.latency_us / 1000.0;
    latencyLabel_->setText(QString::number(latency_ms, 'f', 2) + " ms");

    // Update verification metrics
    blocksVerifiedLabel_->setText(QString::number(m.blocks_verified));

    if (m.verify_errors > 0) {
        verifyErrorsLabel_->setText(QString::number(m.verify_errors));
        verifyErrorsLabel_->setStyleSheet(
            "color: #E74C3C; font-size: 18px; font-weight: bold; border: none; background: transparent;");
    } else {
        verifyErrorsLabel_->setText("0");
        verifyErrorsLabel_->setStyleSheet(
            styles::kPanelTitle);
    }

    // Update CRC errors
    if (m.crc_errors > 0) {
        crcErrorsLabel_->setText(QString::number(m.crc_errors));
        crcErrorsLabel_->setStyleSheet(
            "color: #E74C3C; font-size: 18px; font-weight: bold; border: none; background: transparent;");
    } else {
        crcErrorsLabel_->setText("0");
        crcErrorsLabel_->setStyleSheet(
            styles::kPanelTitle);
    }

    // Update pattern errors
    if (m.pattern_errors > 0) {
        patternErrorsLabel_->setText(QString::number(m.pattern_errors));
        patternErrorsLabel_->setStyleSheet(
            "color: #E74C3C; font-size: 18px; font-weight: bold; border: none; background: transparent;");
    } else {
        patternErrorsLabel_->setText("0");
        patternErrorsLabel_->setStyleSheet(
            styles::kPanelTitle);
    }

    // Update verify speed
    if (m.verify_mbs > 0) {
        verifyMbsLabel_->setText(QString::number(m.verify_mbs, 'f', 1) + " MB/s");
    } else {
        verifyMbsLabel_->setText("-- MB/s");
    }
}

}} // namespace occt::gui
