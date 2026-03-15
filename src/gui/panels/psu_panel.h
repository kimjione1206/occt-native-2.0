#pragma once

#include <QWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QFrame>
#include <memory>

namespace occt {
class IEngine;
class PsuEngine;
class SensorManager;
}

namespace occt { namespace gui {

class RealtimeChart;

class PsuPanel : public QWidget {
    Q_OBJECT

public:
    explicit PsuPanel(QWidget* parent = nullptr);
    ~PsuPanel() override;

    /// Return a raw pointer to the underlying engine (for SafetyGuardian registration).
    IEngine* engine() const;

    /// Pass SensorManager to the internal PsuEngine for power/temperature readings.
    void setSensorManager(SensorManager* mgr);

signals:
    void testStartRequested(const QString& pattern, int durationSec, bool useAllGpus);
    void testStopRequested();

private slots:
    void onStartStopClicked();
    void updateMonitoring();

private:
    void setupUi();
    QFrame* createSettingsSection();
    QFrame* createMonitoringSection();

    // PSU Engine
    std::unique_ptr<PsuEngine> engine_;

    // Settings
    QComboBox* patternCombo_ = nullptr;
    QComboBox* durationCombo_ = nullptr;
    QCheckBox* useAllGpusCheck_ = nullptr;
    QPushButton* startStopBtn_ = nullptr;

    // Monitoring
    RealtimeChart* powerChart_ = nullptr;
    QLabel* totalPowerLabel_ = nullptr;
    QLabel* cpuPowerLabel_ = nullptr;
    QLabel* gpuPowerLabel_ = nullptr;
    QLabel* cpuStatusLabel_ = nullptr;
    QLabel* gpuStatusLabel_ = nullptr;
    QLabel* elapsedLabel_ = nullptr;
    QLabel* cpuErrorsLabel_ = nullptr;
    QLabel* gpuErrorsLabel_ = nullptr;

    bool isRunning_ = false;
    QTimer* monitorTimer_ = nullptr;
};

}} // namespace occt::gui
