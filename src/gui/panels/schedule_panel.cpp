#include "schedule_panel.h"
#include "panel_styles.h"

#include "scheduler/test_scheduler.h"
#include "scheduler/preset_schedules.h"
#include "scheduler/test_step.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollArea>

namespace occt { namespace gui {

SchedulePanel::SchedulePanel(QWidget* parent)
    : QWidget(parent)
{
    scheduler_ = new TestScheduler(this);
    connect(scheduler_, &TestScheduler::stepStarted, this, &SchedulePanel::onStepStarted);
    connect(scheduler_, &TestScheduler::stepCompleted, this, &SchedulePanel::onStepCompleted);
    connect(scheduler_, &TestScheduler::scheduleCompleted, this, &SchedulePanel::onScheduleCompleted);
    connect(scheduler_, &TestScheduler::progressChanged, this, &SchedulePanel::onProgressChanged);

    setupUi();
    updateModeCombo();
}

void SchedulePanel::setupUi()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    // Left column: preset + custom builder
    auto* leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(16);
    leftLayout->addWidget(createPresetSection());
    leftLayout->addWidget(createCustomSection(), 1);

    auto* leftWidget = new QWidget();
    leftWidget->setFixedWidth(400);
    leftWidget->setLayout(leftLayout);
    mainLayout->addWidget(leftWidget);

    // Right column: progress
    mainLayout->addWidget(createProgressSection(), 1);
}

QFrame* SchedulePanel::createPresetSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto* title = new QLabel("테스트 스케줄", frame);
    title->setStyleSheet(styles::kPanelTitle);
    layout->addWidget(title);

    auto* subtitle = new QLabel("프리셋 선택 또는 사용자 정의 스케줄 구성", frame);
    subtitle->setStyleSheet(styles::kPanelSubtitle);
    layout->addWidget(subtitle);

    auto* presetLabel = new QLabel("프리셋 스케줄", frame);
    presetLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(presetLabel);

    presetCombo_ = new QComboBox(frame);
    presetCombo_->addItem("사용자 정의", -1);
    presetCombo_->addItem("빠른 검사 (~5분)", 0);
    presetCombo_->addItem("표준 (~30분)", 1);
    presetCombo_->addItem("극한 (~1시간)", 2);
    presetCombo_->addItem("OC 검증 (~2시간)", 3);
    connect(presetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SchedulePanel::onPresetChanged);
    layout->addWidget(presetCombo_);

    presetInfoLabel_ = new QLabel("", frame);
    presetInfoLabel_->setStyleSheet(styles::kSmallInfo);
    presetInfoLabel_->setWordWrap(true);
    layout->addWidget(presetInfoLabel_);

    return frame;
}

QFrame* SchedulePanel::createCustomSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(10);

    auto* title = new QLabel("스케줄 단계", frame);
    title->setStyleSheet(styles::kSectionTitle);
    layout->addWidget(title);

    stepList_ = new QListWidget(frame);
    stepList_->setStyleSheet(
        "QListWidget { background-color: #0D1117; border: 1px solid #30363D; border-radius: 4px; color: #C9D1D9; }"
        "QListWidget::item { padding: 6px; border-bottom: 1px solid #21262D; }"
        "QListWidget::item:selected { background-color: #1F2937; }"
    );
    layout->addWidget(stepList_, 1);

    // Add step controls
    auto* addRow = new QHBoxLayout();
    engineCombo_ = new QComboBox(frame);
    engineCombo_->addItems({"CPU", "GPU", "RAM", "Storage", "PSU"});
    addRow->addWidget(engineCombo_);

    modeCombo_ = new QComboBox(frame);
    addRow->addWidget(modeCombo_);

    connect(engineCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SchedulePanel::updateModeCombo);

    durationSpin_ = new QSpinBox(frame);
    durationSpin_->setRange(1, 720);
    durationSpin_->setValue(10);
    durationSpin_->setSuffix(" min");
    addRow->addWidget(durationSpin_);

    layout->addLayout(addRow);

    parallelCheck_ = new QCheckBox("다음 단계와 병렬 실행", frame);
    parallelCheck_->setStyleSheet("color: #8B949E; border: none; background: transparent;");
    layout->addWidget(parallelCheck_);

    // Buttons row
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);

    addBtn_ = new QPushButton("추가", frame);
    removeBtn_ = new QPushButton("삭제", frame);
    moveUpBtn_ = new QPushButton("위로", frame);
    moveDownBtn_ = new QPushButton("아래로", frame);

    auto styleMiniBtn = [](QPushButton* btn) {
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(30);
        btn->setStyleSheet(
            "QPushButton { background-color: #21262D; color: #C9D1D9; border: 1px solid #30363D; "
            "border-radius: 4px; padding: 0 12px; font-size: 12px; }"
            "QPushButton:hover { background-color: #30363D; }"
        );
    };
    styleMiniBtn(addBtn_);
    styleMiniBtn(removeBtn_);
    styleMiniBtn(moveUpBtn_);
    styleMiniBtn(moveDownBtn_);

    btnRow->addWidget(addBtn_);
    btnRow->addWidget(removeBtn_);
    btnRow->addWidget(moveUpBtn_);
    btnRow->addWidget(moveDownBtn_);
    layout->addLayout(btnRow);

    // Save/Load
    auto* ioRow = new QHBoxLayout();
    saveBtn_ = new QPushButton("JSON 저장", frame);
    loadBtn_ = new QPushButton("JSON 불러오기", frame);
    styleMiniBtn(saveBtn_);
    styleMiniBtn(loadBtn_);
    ioRow->addWidget(saveBtn_);
    ioRow->addWidget(loadBtn_);
    ioRow->addStretch();
    layout->addLayout(ioRow);

    connect(addBtn_, &QPushButton::clicked, this, &SchedulePanel::onAddStep);
    connect(removeBtn_, &QPushButton::clicked, this, &SchedulePanel::onRemoveStep);
    connect(moveUpBtn_, &QPushButton::clicked, this, &SchedulePanel::onMoveUp);
    connect(moveDownBtn_, &QPushButton::clicked, this, &SchedulePanel::onMoveDown);
    connect(saveBtn_, &QPushButton::clicked, this, &SchedulePanel::onSaveSchedule);
    connect(loadBtn_, &QPushButton::clicked, this, &SchedulePanel::onLoadSchedule);

    return frame;
}

QFrame* SchedulePanel::createProgressSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("스케줄 진행 상황", frame);
    title->setStyleSheet(styles::kSectionTitle);
    layout->addWidget(title);

    // Overall progress
    auto* progressLabel = new QLabel("전체 진행률", frame);
    progressLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(progressLabel);

    overallProgress_ = new QProgressBar(frame);
    overallProgress_->setAccessibleDescription("schedule_progress");
    overallProgress_->setRange(0, 100);
    overallProgress_->setValue(0);
    overallProgress_->setTextVisible(true);
    overallProgress_->setStyleSheet(
        "QProgressBar { background-color: #0D1117; border: 1px solid #30363D; border-radius: 6px; "
        "height: 24px; text-align: center; color: #F0F6FC; font-weight: bold; }"
        "QProgressBar::chunk { background-color: #C0392B; border-radius: 5px; }"
    );
    layout->addWidget(overallProgress_);

    // Current step info
    currentStepLabel_ = new QLabel("실행 중인 스케줄 없음", frame);
    currentStepLabel_->setAccessibleDescription("schedule_current_step");
    currentStepLabel_->setStyleSheet("color: #8B949E; font-size: 14px; border: none; background: transparent;");
    layout->addWidget(currentStepLabel_);

    // Status log
    statusLabel_ = new QLabel("준비", frame);
    statusLabel_->setAccessibleDescription("schedule_status");
    statusLabel_->setStyleSheet(
        "color: #C9D1D9; font-size: 12px; border: none; background-color: #0D1117; "
        "padding: 12px; border-radius: 6px;"
    );
    statusLabel_->setMinimumHeight(200);
    statusLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_, 1);

    // Start/Stop button
    startStopBtn_ = new QPushButton("스케줄 시작", frame);
    startStopBtn_->setCursor(Qt::PointingHandCursor);
    startStopBtn_->setFixedHeight(48);
    startStopBtn_->setStyleSheet(styles::kStartButton);
    connect(startStopBtn_, &QPushButton::clicked, this, &SchedulePanel::onStartStopClicked);
    layout->addWidget(startStopBtn_);

    return frame;
}

void SchedulePanel::onPresetChanged(int index)
{
    if (isRunning_) return;

    int presetId = presetCombo_->currentData().toInt();
    if (presetId < 0) {
        presetInfoLabel_->setText("아래에서 직접 스케줄을 구성하세요.");
        return;
    }

    loadPresetSteps();
}

void SchedulePanel::loadPresetSteps()
{
    int presetId = presetCombo_->currentData().toInt();
    QVector<TestStep> steps;
    QString info;

    switch (presetId) {
        case 0:
            steps = preset_quick_check();
            info = "CPU AVX2 3min + RAM March 2min";
            break;
        case 1:
            steps = preset_standard();
            info = "CPU 10min -> GPU 10min -> RAM 10min";
            break;
        case 2:
            steps = preset_extreme();
            info = "CPU+RAM parallel 20min -> GPU 20min -> Storage 20min";
            break;
        case 3:
            steps = preset_oc_validation();
            info = "CPU Linpack 60min -> RAM March 60min";
            break;
        default:
            return;
    }

    presetInfoLabel_->setText(info);
    scheduler_->load_schedule(steps);
    updateStepList();
}

void SchedulePanel::updateStepList()
{
    stepList_->clear();
    const auto& steps = scheduler_->steps();
    for (int i = 0; i < steps.size(); ++i) {
        const auto& s = steps[i];
        int mins = s.duration_secs / 60;
        QString mode = s.settings.value("mode", "default").toString();
        QString text = QString("%1. %2 [%3] - %4 min")
            .arg(i + 1)
            .arg(s.engine.toUpper())
            .arg(mode)
            .arg(mins);
        if (s.parallel_with_next)
            text += " (parallel)";
        stepList_->addItem(text);
    }
}

void SchedulePanel::onAddStep()
{
    if (isRunning_) return;

    TestStep step;
    step.engine = engineCombo_->currentText().toLower();

    // Map display name to internal mode string
    QString displayMode = modeCombo_->currentText();
    QString mode;

    // Use the data stored in the combo item (set by updateModeCombo)
    QString data = modeCombo_->currentData().toString();
    if (!data.isEmpty()) {
        mode = data;
    } else {
        mode = displayMode.toLower();
    }

    step.settings["mode"] = mode;
    step.duration_secs = durationSpin_->value() * 60;
    step.parallel_with_next = parallelCheck_->isChecked();

    auto steps = scheduler_->steps();
    steps.append(step);
    scheduler_->load_schedule(steps);
    presetCombo_->setCurrentIndex(0); // switch to Custom
    updateStepList();
}

void SchedulePanel::onRemoveStep()
{
    if (isRunning_) return;
    int row = stepList_->currentRow();
    if (row < 0) return;

    auto steps = scheduler_->steps();
    steps.removeAt(row);
    scheduler_->load_schedule(steps);
    updateStepList();
}

void SchedulePanel::onMoveUp()
{
    if (isRunning_) return;
    int row = stepList_->currentRow();
    if (row <= 0) return;

    auto steps = scheduler_->steps();
    steps.swapItemsAt(row, row - 1);
    scheduler_->load_schedule(steps);
    updateStepList();
    stepList_->setCurrentRow(row - 1);
}

void SchedulePanel::onMoveDown()
{
    if (isRunning_) return;
    int row = stepList_->currentRow();
    if (row < 0 || row >= stepList_->count() - 1) return;

    auto steps = scheduler_->steps();
    steps.swapItemsAt(row, row + 1);
    scheduler_->load_schedule(steps);
    updateStepList();
    stepList_->setCurrentRow(row + 1);
}

void SchedulePanel::onStartStopClicked()
{
    if (isRunning_) {
        scheduler_->stop();
        isRunning_ = false;
        startStopBtn_->setText("스케줄 시작");
        startStopBtn_->setStyleSheet(
            styles::kStartButton
        );
        currentStepLabel_->setText("스케줄 중지됨");
    } else {
        if (scheduler_->steps().isEmpty()) {
            statusLabel_->setText("스케줄에 단계가 없습니다. 단계를 추가하거나 프리셋을 선택하세요.");
            return;
        }

        isRunning_ = true;
        statusLabel_->setText("스케줄 시작 중...\n");
        overallProgress_->setValue(0);
        startStopBtn_->setText("스케줄 중지");
        startStopBtn_->setStyleSheet(
            styles::kStopButton
        );
        scheduler_->start();
    }
}

void SchedulePanel::onStepStarted(int index, const QString& engine)
{
    currentStepLabel_->setText(
        QString("단계 %1/%2: %3")
            .arg(index + 1)
            .arg(scheduler_->steps().size())
            .arg(engine.toUpper()));

    statusLabel_->setText(statusLabel_->text() +
        QString("단계 %1 시작: %2\n").arg(index + 1).arg(engine.toUpper()));
}

void SchedulePanel::onStepCompleted(int index, bool passed, int errors)
{
    QString result = passed ? "통과" : "실패";
    statusLabel_->setText(statusLabel_->text() +
        QString("단계 %1 완료: %2 (오류: %3)\n").arg(index + 1).arg(result).arg(errors));
}

void SchedulePanel::onScheduleCompleted(bool all_passed, int total_errors)
{
    isRunning_ = false;
    startStopBtn_->setText("스케줄 시작");
    startStopBtn_->setStyleSheet(styles::kStartButton);
    overallProgress_->setValue(100);

    QString summary = all_passed ? "모두 통과" : "실패";
    currentStepLabel_->setText("스케줄 완료: " + summary);
    statusLabel_->setText(statusLabel_->text() +
        QString("\n--- 스케줄 완료 ---\n결과: %1\n총 오류: %2\n")
            .arg(summary).arg(total_errors));
}

void SchedulePanel::onProgressChanged(double pct)
{
    overallProgress_->setValue(static_cast<int>(pct));
}

void SchedulePanel::onSaveSchedule()
{
    QString path = QFileDialog::getSaveFileName(this, "스케줄 저장", "", "JSON (*.json)");
    if (!path.isEmpty()) {
        scheduler_->save_to_json(path);
    }
}

void SchedulePanel::onLoadSchedule()
{
    if (isRunning_) return;
    QString path = QFileDialog::getOpenFileName(this, "스케줄 불러오기", "", "JSON (*.json)");
    if (!path.isEmpty()) {
        scheduler_->load_from_json(path);
        presetCombo_->setCurrentIndex(0); // Custom
        updateStepList();
    }
}

void SchedulePanel::updateModeCombo()
{
    modeCombo_->clear();

    QString engine = engineCombo_->currentText().toLower();

    // Each entry: display name -> internal mode string
    if (engine == "cpu") {
        modeCombo_->addItem("Default (AVX2)", "avx2");
        modeCombo_->addItem("AVX2", "avx2");
        modeCombo_->addItem("AVX-512", "avx512");
        modeCombo_->addItem("SSE", "sse");
        modeCombo_->addItem("Linpack", "linpack");
        modeCombo_->addItem("Prime", "prime");
        modeCombo_->addItem("Cache Only", "cache_only");
        modeCombo_->addItem("Large Data Set", "large_data_set");
        modeCombo_->addItem("All", "all");
    } else if (engine == "gpu") {
        modeCombo_->addItem("Default (Matrix Multiply)", "matrix_mul");
        modeCombo_->addItem("Matrix Multiply", "matrix_mul");
        modeCombo_->addItem("FP64 Matrix", "fp64_matrix");
        modeCombo_->addItem("FMA", "fma");
        modeCombo_->addItem("Trigonometric", "trigonometric");
        modeCombo_->addItem("VRAM", "vram");
        modeCombo_->addItem("Mixed", "mixed");
        modeCombo_->addItem("Vulkan 3D", "vulkan_3d");
        modeCombo_->addItem("Vulkan Adaptive", "vulkan_adaptive");
    } else if (engine == "ram") {
        modeCombo_->addItem("Default (March C-)", "march_c");
        modeCombo_->addItem("March C-", "march_c");
        modeCombo_->addItem("Walking Ones", "walking_ones");
        modeCombo_->addItem("Walking Zeros", "walking_zeros");
        modeCombo_->addItem("Checkerboard", "checkerboard");
        modeCombo_->addItem("Random", "random");
        modeCombo_->addItem("Bandwidth", "bandwidth");
    } else if (engine == "storage") {
        modeCombo_->addItem("Default (Write)", "write");
        modeCombo_->addItem("Sequential Write", "sequential_write");
        modeCombo_->addItem("Sequential Read", "sequential_read");
        modeCombo_->addItem("Random Write", "random_write");
        modeCombo_->addItem("Random Read", "random_read");
        modeCombo_->addItem("Mixed", "mixed");
        modeCombo_->addItem("Verify Sequential", "verify_sequential");
        modeCombo_->addItem("Verify Random", "verify_random");
        modeCombo_->addItem("Fill & Verify", "fill_verify");
        modeCombo_->addItem("Butterfly", "butterfly");
    } else if (engine == "psu") {
        modeCombo_->addItem("Default (Steady)", "steady");
        modeCombo_->addItem("Steady", "steady");
        modeCombo_->addItem("Spike", "spike");
        modeCombo_->addItem("Ramp", "ramp");
    }
}

}} // namespace occt::gui
