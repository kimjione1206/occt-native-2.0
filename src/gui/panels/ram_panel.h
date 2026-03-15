#pragma once

#include <QWidget>
#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QTimer>
#include <QFrame>
#include <QCheckBox>
#include <memory>

namespace occt {
class IEngine;
class RamEngine;
}

namespace occt { namespace gui {

class RealtimeChart;

class RamPanel : public QWidget {
    Q_OBJECT

public:
    explicit RamPanel(QWidget* parent = nullptr);
    ~RamPanel() override;

    /// Return a raw pointer to the underlying engine (for SafetyGuardian registration).
    IEngine* engine() const;

signals:
    void testStartRequested(const QString& pattern, int memPercent, int passes);
    void testStopRequested();

private slots:
    void onStartStopClicked();
    void onMemSliderChanged(int value);
    void updateMonitoring();

private:
    void setupUi();
    QFrame* createSettingsSection();
    QFrame* createMonitoringSection();

    // RAM Engine
    std::unique_ptr<RamEngine> engine_;

    // Settings
    QComboBox* patternCombo_ = nullptr;
    QSlider* memSlider_ = nullptr;
    QLabel* memValueLabel_ = nullptr;
    QSpinBox* passesSpinBox_ = nullptr;
    QPushButton* startStopBtn_ = nullptr;

    // Direct MB mode
    QCheckBox* directMbCheck_ = nullptr;
    QSpinBox* memMbSpin_ = nullptr;

    // Monitoring
    RealtimeChart* bandwidthChart_ = nullptr;
    QLabel* bandwidthLabel_ = nullptr;
    QLabel* errorsLabel_ = nullptr;
    QLabel* progressLabel_ = nullptr;
    QProgressBar* testProgress_ = nullptr;

    bool isRunning_ = false;
    QTimer* monitorTimer_ = nullptr;
};

}} // namespace occt::gui
