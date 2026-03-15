#pragma once

#include <QWidget>
#include <QLabel>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QTimer>
#include <QFrame>

namespace occt {
class SensorManager;
}

namespace occt { namespace gui {

class CircularGauge;

class DashboardPanel : public QWidget {
    Q_OBJECT

public:
    explicit DashboardPanel(QWidget* parent = nullptr);

    /// Inject the sensor manager instance (owned externally).
    void setSensorManager(SensorManager* mgr);

signals:
    void startCpuTest();
    void startGpuTest();
    void startFullTest();

private slots:
    void updateGauges();

private:
    void setupUi();
    QFrame* createInfoCard(const QString& title, const QString& value, const QString& detail);
    QFrame* createQuickStartSection();

    // Gauges
    CircularGauge* cpuGauge_ = nullptr;
    CircularGauge* ramGauge_ = nullptr;
    CircularGauge* gpuGauge_ = nullptr;

    QTimer* updateTimer_ = nullptr;

    SensorManager* sensorMgr_ = nullptr;

    // For CPU usage delta calculation
    uint64_t prevIdleTicks_ = 0;
    uint64_t prevTotalTicks_ = 0;
};

}} // namespace occt::gui
