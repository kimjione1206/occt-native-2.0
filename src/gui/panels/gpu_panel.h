#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QFrame>
#include <QProgressBar>
#include <memory>

namespace occt { class GpuEngine; class IEngine; class SensorManager; }

namespace occt { namespace gui {

class RealtimeChart;
class CircularGauge;

class GpuPanel : public QWidget {
    Q_OBJECT

public:
    explicit GpuPanel(QWidget* parent = nullptr);
    ~GpuPanel() override;

    /// Inject the sensor manager instance (owned externally).
    void setSensorManager(SensorManager* mgr);

    /// Return the underlying engine as an IEngine pointer for SafetyGuardian registration.
    IEngine* engine() const;

signals:
    void testStartRequested(const QString& gpuDevice, const QString& mode, int durationSec);
    void testStopRequested();

private slots:
    void onStartStopClicked();
    void onModeChanged(int index);
    void updateMonitoring();

private:
    void setupUi();
    QFrame* createSettingsSection();
    QFrame* createMonitoringSection();

    // GPU Engine
    std::unique_ptr<GpuEngine> engine_;

    // Sensor manager for temperature readings
    SensorManager* sensorMgr_ = nullptr;

    // Settings
    QComboBox* gpuSelectCombo_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QComboBox* durationCombo_ = nullptr;
    QPushButton* startStopBtn_ = nullptr;

    // Vulkan-specific settings
    QSlider* shaderComplexitySlider_ = nullptr;
    QLabel* shaderComplexityLabel_ = nullptr;
    QComboBox* adaptiveModeCombo_ = nullptr;
    QSpinBox* coilFreqSpin_ = nullptr;
    QLabel* coilFreqLabel_ = nullptr;
    QCheckBox* multiGpuCheck_ = nullptr;
    QWidget* vulkanSettingsWidget_ = nullptr;

    // Monitoring
    RealtimeChart* gflopsChart_ = nullptr;
    CircularGauge* gpuUsageGauge_ = nullptr;
    QLabel* gflopsLabel_ = nullptr;
    QLabel* tempLabel_ = nullptr;
    QLabel* vramLabel_ = nullptr;
    QLabel* fpsLabel_ = nullptr;
    QLabel* artifactLabel_ = nullptr;
    QLabel* vramErrorsLabel_ = nullptr;
    QLabel* statusBanner_ = nullptr;
    QProgressBar* vramBar_ = nullptr;

    bool isRunning_ = false;
    QTimer* monitorTimer_ = nullptr;
};

}} // namespace occt::gui
