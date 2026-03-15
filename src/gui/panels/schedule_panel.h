#pragma once

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QListWidget>
#include <QProgressBar>
#include <QLabel>
#include <QFrame>
#include <QSpinBox>
#include <QCheckBox>

namespace occt {
class TestScheduler;
}

namespace occt { namespace gui {

class SchedulePanel : public QWidget {
    Q_OBJECT

public:
    explicit SchedulePanel(QWidget* parent = nullptr);

private slots:
    void onPresetChanged(int index);
    void onAddStep();
    void onRemoveStep();
    void onMoveUp();
    void onMoveDown();
    void onStartStopClicked();
    void onStepStarted(int index, const QString& engine);
    void onStepCompleted(int index, bool passed, int errors);
    void onScheduleCompleted(bool all_passed, int total_errors);
    void onProgressChanged(double pct);
    void onSaveSchedule();
    void onLoadSchedule();
    void updateModeCombo();

private:
    void setupUi();
    QFrame* createPresetSection();
    QFrame* createCustomSection();
    QFrame* createProgressSection();
    void loadPresetSteps();
    void updateStepList();

    // Preset
    QComboBox* presetCombo_ = nullptr;
    QLabel* presetInfoLabel_ = nullptr;

    // Custom builder
    QListWidget* stepList_ = nullptr;
    QComboBox* engineCombo_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QSpinBox* durationSpin_ = nullptr;
    QCheckBox* parallelCheck_ = nullptr;
    QPushButton* addBtn_ = nullptr;
    QPushButton* removeBtn_ = nullptr;
    QPushButton* moveUpBtn_ = nullptr;
    QPushButton* moveDownBtn_ = nullptr;
    QPushButton* saveBtn_ = nullptr;
    QPushButton* loadBtn_ = nullptr;

    // Progress
    QProgressBar* overallProgress_ = nullptr;
    QLabel* currentStepLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* startStopBtn_ = nullptr;

    // Scheduler
    TestScheduler* scheduler_ = nullptr;
    bool isRunning_ = false;
};

}} // namespace occt::gui
