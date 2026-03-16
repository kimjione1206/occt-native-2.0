#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QTimer>
#include <QFrame>
#include <QPushButton>
#include <QSplitter>

namespace occt {
class SensorManager;
class WheaMonitor;
}

namespace occt { namespace gui {

class RealtimeChart;

class MonitorPanel : public QWidget {
    Q_OBJECT

public:
    explicit MonitorPanel(QWidget* parent = nullptr);
    ~MonitorPanel() override = default;

    /// Inject the sensor manager instance (owned externally).
    void setSensorManager(SensorManager* mgr);

    /// Inject the WHEA monitor instance (owned externally).
    void setWheaMonitor(WheaMonitor* whea);

private slots:
    void onSensorSelected(QTreeWidgetItem* item, int column);
    void updateSensors();
    void onMonitorOnlyToggled();

private:
    void setupUi();
    void rebuildSensorTree();
    void addSensorCategory(const QString& category, const QStringList& sensors);

    QTreeWidget* sensorTree_ = nullptr;
    RealtimeChart* sensorChart_ = nullptr;
    QLabel* sensorNameLabel_ = nullptr;
    QLabel* sensorValueLabel_ = nullptr;
    QLabel* sensorMinLabel_ = nullptr;
    QLabel* sensorMaxLabel_ = nullptr;
    QLabel* sensorAvgLabel_ = nullptr;
    QLabel* wheaCountLabel_ = nullptr;
    QPushButton* monitorOnlyBtn_ = nullptr;

    QTimer* updateTimer_ = nullptr;
    QString selectedSensor_;
    bool monitorOnlyMode_ = false;

    SensorManager* sensorMgr_ = nullptr;                  // non-owning; injected by MainWindow
    WheaMonitor*   wheaMon_   = nullptr;
};

}} // namespace occt::gui
