#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QPushButton>
#include <QStatusBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QMap>
#include <QTimer>
#include <QMenuBar>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QAction>
#include <QSystemTrayIcon>
#include <QShortcut>

#include <memory>

namespace occt { class SensorManager; class SafetyGuardian; }
namespace occt { namespace updater { class UpdateChecker; class LogUploader; struct UpdateInfo; } }

namespace occt { namespace gui {

class ResultsPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onNavButtonClicked(const QString& panelKey);
    void updateStatusBar();
    void onExportReport();
    void onExit();
    void onAbout();
    void onUpdateAvailable(const updater::UpdateInfo& info);
    void onTestStopped(const QString& engineName);
    void onCheckForUpdate();
    void onTokenSettings();

private:
    void setupUi();
    void createMenuBar();
    void createHeaderBar();
    void createSidebar();
    void createContentArea();
    void createStatusBarWidgets();
    void createPanels();
    void setActiveTab(const QString& key);
    void addNavButton(const QString& key, const QString& text, const QString& iconChar);
    void addSeparator();
    void setupTrayIcon();
    void setupShortcuts();
    void stopAllTests();
    void playTestCompleteSound();
    void playTestErrorSound();

    QWidget* centralContainer_ = nullptr;
    QFrame* headerBar_ = nullptr;
    QFrame* sidebar_ = nullptr;
    QVBoxLayout* sidebarLayout_ = nullptr;
    QStackedWidget* contentStack_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QTimer* statusTimer_ = nullptr;

    QMap<QString, QPushButton*> navButtons_;
    QMap<QString, QWidget*> panels_;
    QString currentPanel_;

    ResultsPanel* resultsPanel_ = nullptr;
    std::unique_ptr<SensorManager> sensorMgr_;
    std::unique_ptr<SafetyGuardian> safetyGuardian_;

    // System tray
    QSystemTrayIcon* trayIcon_ = nullptr;
    QMenu* trayMenu_ = nullptr;

    // Sound alerts
    bool soundAlertsEnabled_ = true;

    // Update checker
    updater::UpdateChecker* updateChecker_ = nullptr;
    updater::LogUploader* logUploader_ = nullptr;
    bool manualUpdateCheck_ = false;

    // Sensor history (5s interval, recorded during tests)
    QTimer* sensorHistoryTimer_ = nullptr;
    QJsonArray sensorHistory_;
    bool sensorRecording_ = false;
};

}} // namespace occt::gui
