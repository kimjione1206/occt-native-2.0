#pragma once

#include <QWidget>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QFrame>
#include <QGridLayout>
#include <memory>
#include <vector>

namespace occt {
class IEngine;
class CpuEngine;
class SensorManager;
}

namespace occt { namespace gui {

class RealtimeChart;

class CpuPanel : public QWidget {
    Q_OBJECT

public:
    explicit CpuPanel(QWidget* parent = nullptr);
    ~CpuPanel() override;

    // Update per-core error status from engine metrics
    void updateErrorStatus(int errorCount, const std::vector<bool>& coreErrors);

    // Set SensorManager for temperature/power fallback
    void setSensorManager(SensorManager* mgr);

    /// Return a raw pointer to the underlying engine (for SafetyGuardian registration).
    IEngine* engine() const;

signals:
    void testStartRequested(const QString& mode, const QString& loadPattern,
                            int threads, int durationSec);
    void testStopRequested();

private slots:
    void onStartStopClicked();
    void onThreadSliderChanged(int value);
    void updateMonitoring();

private:
    void setupUi();
    QFrame* createSettingsSection();
    QFrame* createMonitoringSection();
    void rebuildCoreGrid(int coreCount);

    // CPU Engine
    std::unique_ptr<CpuEngine> engine_;

    // Sensor manager for temperature/power readings
    SensorManager* sensorMgr_ = nullptr;

    // Settings
    QComboBox* modeCombo_ = nullptr;
    QComboBox* loadPatternCombo_ = nullptr;
    QSlider* threadSlider_ = nullptr;
    QLabel* threadValueLabel_ = nullptr;
    QComboBox* durationCombo_ = nullptr;
    QPushButton* startStopBtn_ = nullptr;

    // Monitoring
    RealtimeChart* gflopsChart_ = nullptr;
    QLabel* gflopsValueLabel_ = nullptr;
    QLabel* tempLabel_ = nullptr;
    QLabel* powerLabel_ = nullptr;
    QLabel* freqLabel_ = nullptr;
    QLabel* errorCountLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    // Per-core error grid
    QFrame* coreGridFrame_ = nullptr;
    QGridLayout* coreGridLayout_ = nullptr;
    std::vector<QLabel*> coreStatusLabels_;

    // State
    bool isRunning_ = false;
    QTimer* monitorTimer_ = nullptr;
};

}} // namespace occt::gui
