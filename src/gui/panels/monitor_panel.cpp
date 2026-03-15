#include "monitor_panel.h"
#include "panel_styles.h"
#include "../widgets/realtime_chart.h"
#include "../../monitor/sensor_manager.h"
#include "../../monitor/sensor_model.h"
#include "../../monitor/whea_monitor.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QHeaderView>

namespace occt { namespace gui {

MonitorPanel::MonitorPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();

    // Create a SensorManager and initialize it for real hardware polling
    ownedSensorMgr_ = std::make_unique<SensorManager>();
    if (ownedSensorMgr_->initialize()) {
        ownedSensorMgr_->start_polling(500);
        sensorMgr_ = ownedSensorMgr_.get();
    } else {
        // Initialization failed; show placeholder structure
        ownedSensorMgr_.reset();

        addSensorCategory("CPU", {
            "Package Temperature",
            "Core 0 Temperature",
            "Core 1 Temperature",
            "Core 2 Temperature",
            "Core 3 Temperature",
            "CPU Package Power",
            "CPU Clock Speed",
            "CPU Usage"
        });
        addSensorCategory("GPU", {
            "GPU Temperature",
            "GPU Core Clock",
            "GPU Memory Clock",
            "GPU Usage",
            "GPU Power",
            "VRAM Usage"
        });
        addSensorCategory("Motherboard", {
            "System Temperature",
            "VRM Temperature",
            "Fan Speed (CPU)",
            "Fan Speed (System)"
        });
        addSensorCategory("Storage", {
            "Disk Temperature",
            "Read Speed",
            "Write Speed",
            "Disk Usage"
        });
    }
    sensorTree_->expandAll();

    updateTimer_ = new QTimer(this);
    connect(updateTimer_, &QTimer::timeout, this, &MonitorPanel::updateSensors);
    updateTimer_->start(1000);
}

MonitorPanel::~MonitorPanel() {
    if (ownedSensorMgr_) {
        ownedSensorMgr_->stop();
    }
    // ownedSensorMgr_ is automatically destroyed by unique_ptr
}

void MonitorPanel::setSensorManager(SensorManager* mgr) {
    if (!mgr) return;

    // If externally injected, stop and release our own instance
    if (ownedSensorMgr_ && mgr != ownedSensorMgr_.get()) {
        ownedSensorMgr_->stop();
        ownedSensorMgr_.reset();
    }
    sensorMgr_ = mgr;

    // Rebuild the sensor tree with live data from the new manager,
    // replacing any placeholder entries that were added during construction.
    rebuildSensorTree();
}

void MonitorPanel::setWheaMonitor(WheaMonitor* whea) {
    wheaMon_ = whea;
}

void MonitorPanel::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    // Title row
    auto* titleRow = new QHBoxLayout();

    auto* title = new QLabel("하드웨어 모니터링", this);
    title->setStyleSheet("color: #F0F6FC; font-size: 20px; font-weight: bold; background: transparent;");
    titleRow->addWidget(title);

    titleRow->addStretch();

    // WHEA error count
    wheaCountLabel_ = new QLabel("WHEA 오류: 0", this);
    wheaCountLabel_->setAccessibleDescription("monitor_whea_count");
    wheaCountLabel_->setStyleSheet(
        "color: #3FB950; font-size: 13px; font-weight: bold; background: transparent; padding: 4px 12px;");
    titleRow->addWidget(wheaCountLabel_);

    // Monitor-only mode button
    monitorOnlyBtn_ = new QPushButton("모니터링 전용", this);
    monitorOnlyBtn_->setCheckable(true);
    monitorOnlyBtn_->setCursor(Qt::PointingHandCursor);
    monitorOnlyBtn_->setStyleSheet(
        "QPushButton {"
        "  background-color: #21262D; color: #C9D1D9; border: 1px solid #30363D;"
        "  border-radius: 6px; padding: 6px 16px; font-size: 12px;"
        "}"
        "QPushButton:hover { background-color: #30363D; }"
        "QPushButton:checked { background-color: #C0392B; color: white; border-color: #C0392B; }"
    );
    connect(monitorOnlyBtn_, &QPushButton::clicked, this, &MonitorPanel::onMonitorOnlyToggled);
    titleRow->addWidget(monitorOnlyBtn_);

    mainLayout->addLayout(titleRow);

    auto* subtitle = new QLabel("CPU, GPU, 저장장치의 실시간 센서 데이터", this);
    subtitle->setStyleSheet("color: #8B949E; font-size: 13px; background: transparent;");
    mainLayout->addWidget(subtitle);

    // Splitter: tree left, chart right
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setStyleSheet(
        "QSplitter::handle { background-color: #30363D; width: 2px; }"
        "QSplitter::handle:hover { background-color: #C0392B; }"
    );

    // Left: Sensor tree
    auto* treeFrame = new QFrame(splitter);
    treeFrame->setStyleSheet(
        styles::kSectionFrame
    );
    auto* treeLayout = new QVBoxLayout(treeFrame);
    treeLayout->setContentsMargins(12, 12, 12, 12);

    auto* treeTitle = new QLabel("센서", treeFrame);
    treeTitle->setStyleSheet("color: #C9D1D9; font-size: 14px; font-weight: bold; border: none; background: transparent;");
    treeLayout->addWidget(treeTitle);

    sensorTree_ = new QTreeWidget(treeFrame);
    sensorTree_->setAccessibleDescription("monitor_sensor_tree");
    sensorTree_->setHeaderLabels({"센서", "값", "최소", "최대"});
    sensorTree_->header()->setStretchLastSection(false);
    sensorTree_->header()->setMinimumSectionSize(50);
    sensorTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    sensorTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    sensorTree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    sensorTree_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    sensorTree_->setColumnWidth(0, 150);
    sensorTree_->setColumnWidth(1, 80);
    sensorTree_->setColumnWidth(2, 60);
    sensorTree_->setColumnWidth(3, 60);
    sensorTree_->setRootIsDecorated(true);
    sensorTree_->setAnimated(true);
    sensorTree_->setStyleSheet(
        "QTreeWidget { background-color: #0D1117; border: 1px solid #30363D; border-radius: 4px; }"
        "QTreeWidget::item { padding: 4px 0; }"
        "QTreeWidget::item:selected { background-color: #1F2937; }"
        "QHeaderView::section { background-color: #161B22; color: #8B949E; border: 1px solid #30363D; padding: 4px; }"
    );
    connect(sensorTree_, &QTreeWidget::itemClicked, this, &MonitorPanel::onSensorSelected);
    treeLayout->addWidget(sensorTree_);

    splitter->addWidget(treeFrame);

    // Right: Chart and details
    auto* chartFrame = new QFrame(splitter);
    chartFrame->setStyleSheet(
        styles::kSectionFrame
    );
    auto* chartLayout = new QVBoxLayout(chartFrame);
    chartLayout->setContentsMargins(16, 16, 16, 16);
    chartLayout->setSpacing(12);

    sensorNameLabel_ = new QLabel("센서를 선택하세요", chartFrame);
    sensorNameLabel_->setAccessibleDescription("monitor_sensor_name");
    sensorNameLabel_->setStyleSheet(styles::kSectionTitle);
    chartLayout->addWidget(sensorNameLabel_);

    // Stats row
    auto* statsLayout = new QHBoxLayout();
    statsLayout->setSpacing(16);

    auto createStat = [chartFrame](const QString& label) -> QLabel* {
        auto* card = new QFrame(chartFrame);
        card->setStyleSheet(styles::kCardFrame);
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(12, 6, 12, 6);
        auto* lbl = new QLabel(label, card);
        lbl->setStyleSheet(styles::kSmallInfo);
        auto* val = new QLabel("--", card);
        val->setStyleSheet(styles::kSectionTitle);
        cl->addWidget(lbl);
        cl->addWidget(val);
        return val;
    };

    sensorValueLabel_ = createStat("현재");
    sensorValueLabel_->setAccessibleDescription("monitor_sensor_value");
    statsLayout->addWidget(sensorValueLabel_->parentWidget());
    sensorMinLabel_ = createStat("최소");
    sensorMinLabel_->setAccessibleDescription("monitor_sensor_min");
    statsLayout->addWidget(sensorMinLabel_->parentWidget());
    sensorMaxLabel_ = createStat("최대");
    sensorMaxLabel_->setAccessibleDescription("monitor_sensor_max");
    statsLayout->addWidget(sensorMaxLabel_->parentWidget());
    sensorAvgLabel_ = createStat("평균");
    sensorAvgLabel_->setAccessibleDescription("monitor_sensor_avg");
    statsLayout->addWidget(sensorAvgLabel_->parentWidget());

    chartLayout->addLayout(statsLayout);

    // Chart
    sensorChart_ = new RealtimeChart(chartFrame);
    sensorChart_->setTitle("센서 기록");
    sensorChart_->setLineColor(QColor(192, 57, 43));
    sensorChart_->setMinimumHeight(300);
    chartLayout->addWidget(sensorChart_, 1);

    splitter->addWidget(chartFrame);
    splitter->setSizes({400, 600});

    mainLayout->addWidget(splitter, 1);
}

void MonitorPanel::rebuildSensorTree()
{
    if (!sensorMgr_) return;

    // Save selection
    QString prev_selected = selectedSensor_;

    sensorTree_->clear();

    auto tree = sensorMgr_->get_hardware_tree();
    QTreeWidgetItem* reselect = nullptr;

    for (const auto& hw : tree) {
        auto* hwItem = new QTreeWidgetItem(sensorTree_);
        hwItem->setText(0, hw.name);
        hwItem->setFlags(hwItem->flags() & ~Qt::ItemIsSelectable);
        QFont boldFont = hwItem->font(0);
        boldFont.setBold(true);
        hwItem->setFont(0, boldFont);

        for (const auto& grp : hw.groups) {
            auto* grpItem = new QTreeWidgetItem(hwItem);
            grpItem->setText(0, grp.name);
            grpItem->setFlags(grpItem->flags() & ~Qt::ItemIsSelectable);
            QFont italicFont = grpItem->font(0);
            italicFont.setItalic(true);
            grpItem->setFont(0, italicFont);

            for (const auto& r : grp.readings) {
                auto* sensorItem = new QTreeWidgetItem(grpItem);
                QString name = QString::fromStdString(r.name);
                sensorItem->setText(0, name);
                sensorItem->setText(1, QString::number(r.value, 'f', 1) + " " + QString::fromStdString(r.unit));
                sensorItem->setText(2, QString::number(r.min_value, 'f', 1));
                sensorItem->setText(3, QString::number(r.max_value, 'f', 1));

                if (name == prev_selected) reselect = sensorItem;
            }
        }
    }

    sensorTree_->expandAll();

    if (reselect) {
        sensorTree_->setCurrentItem(reselect);
    }
}

void MonitorPanel::addSensorCategory(const QString& category, const QStringList& sensors)
{
    auto* categoryItem = new QTreeWidgetItem(sensorTree_);
    categoryItem->setText(0, category);
    categoryItem->setFlags(categoryItem->flags() & ~Qt::ItemIsSelectable);

    QFont boldFont = categoryItem->font(0);
    boldFont.setBold(true);
    categoryItem->setFont(0, boldFont);

    for (const auto& sensor : sensors) {
        auto* item = new QTreeWidgetItem(categoryItem);
        item->setText(0, sensor);
        item->setText(1, "--");
        item->setText(2, "--");
        item->setText(3, "--");
    }
}

void MonitorPanel::onSensorSelected(QTreeWidgetItem* item, int /*column*/)
{
    if (!item || !item->parent()) return; // skip category items
    // Also skip group items (items whose parent has no parent are categories,
    // items whose parent has a parent might be groups or sensors)
    // Sensors are the leaf items
    if (item->childCount() > 0) return; // this is a group, not a leaf sensor

    selectedSensor_ = item->text(0);
    sensorNameLabel_->setText(selectedSensor_);
    sensorChart_->clear();
    sensorChart_->setTitle(selectedSensor_);

    // Set unit based on sensor type
    if (selectedSensor_.contains("Temperature") || selectedSensor_.contains("Temp"))
        sensorChart_->setUnit("C");
    else if (selectedSensor_.contains("Power"))
        sensorChart_->setUnit("W");
    else if (selectedSensor_.contains("Clock") || selectedSensor_.contains("Speed"))
        sensorChart_->setUnit("MHz");
    else if (selectedSensor_.contains("Usage") || selectedSensor_.contains("Load"))
        sensorChart_->setUnit("%");
    else if (selectedSensor_.contains("Fan"))
        sensorChart_->setUnit("RPM");
    else if (selectedSensor_.contains("Voltage") || selectedSensor_.contains("Vcore"))
        sensorChart_->setUnit("V");
}

void MonitorPanel::updateSensors()
{
    if (!sensorMgr_) return;

    // Rebuild tree from live data
    rebuildSensorTree();

    // Update chart for selected sensor
    if (!selectedSensor_.isEmpty()) {
        auto readings = sensorMgr_->get_all_readings();
        for (const auto& r : readings) {
            if (QString::fromStdString(r.name) == selectedSensor_) {
                sensorChart_->addPoint(r.value);
                sensorValueLabel_->setText(QString::number(r.value, 'f', 1));
                sensorMinLabel_->setText(QString::number(r.min_value, 'f', 1));
                sensorMaxLabel_->setText(QString::number(r.max_value, 'f', 1));
                double avg = (r.min_value + r.max_value) / 2.0;
                sensorAvgLabel_->setText(QString::number(avg, 'f', 1));
                break;
            }
        }
    }

    // Update WHEA count
    if (wheaMon_) {
        int count = wheaMon_->error_count();
        wheaCountLabel_->setText(QString("WHEA 오류: %1").arg(count));
        if (count > 0) {
            wheaCountLabel_->setStyleSheet(
                "color: #F85149; font-size: 13px; font-weight: bold; background: transparent; padding: 4px 12px;");
        } else {
            wheaCountLabel_->setStyleSheet(
                "color: #3FB950; font-size: 13px; font-weight: bold; background: transparent; padding: 4px 12px;");
        }
    }
}

void MonitorPanel::onMonitorOnlyToggled()
{
    monitorOnlyMode_ = monitorOnlyBtn_->isChecked();
    // In monitor-only mode, the timer runs at a higher frequency
    if (monitorOnlyMode_) {
        updateTimer_->setInterval(500);
    } else {
        updateTimer_->setInterval(1000);
    }
}

}} // namespace occt::gui
