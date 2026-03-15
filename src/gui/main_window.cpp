#include "main_window.h"
#include "panels/dashboard_panel.h"
#include "panels/cpu_panel.h"
#include "panels/gpu_panel.h"
#include "panels/ram_panel.h"
#include "panels/storage_panel.h"
#include "panels/monitor_panel.h"
#include "panels/results_panel.h"
#include "panels/psu_panel.h"
#include "panels/benchmark_panel.h"
#include "panels/sysinfo_panel.h"
#include "panels/schedule_panel.h"
#include "panels/certificate_panel.h"
#include "../monitor/sensor_manager.h"
#include "../safety/guardian.h"
#include "../updater/update_checker.h"
#include "../updater/update_dialog.h"
#include "../updater/update_downloader.h"
#include "../updater/update_installer.h"
#include "../updater/log_uploader.h"
#include "dialogs/token_dialog.h"
#include "../engines/cpu_engine.h"
#include "../engines/gpu_engine.h"
#include "../engines/ram_engine.h"
#include "../engines/storage_engine.h"
#include "../engines/psu_engine.h"
#include "../monitor/sensor_manager.h"
#include "config.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMessageBox>
#include <QFileDialog>
#include <QShortcut>
#include <QCloseEvent>
#include <QStyle>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHostInfo>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

namespace occt { namespace gui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("OCCT 네이티브 스트레스 테스트");
    setMinimumSize(1200, 800);
    resize(1400, 900);

    // Create status timer BEFORE setupUi() since createStatusBarWidgets() connects to it
    statusTimer_ = new QTimer(this);

    setupUi();

    connect(statusTimer_, &QTimer::timeout, this, &MainWindow::updateStatusBar);
    statusTimer_->start(1000);

    // Default panel
    setActiveTab("dashboard");

    setupTrayIcon();
    setupShortcuts();

    // Auto-update checker: check 30 seconds after startup, then every 4 hours
    updateChecker_ = new updater::UpdateChecker(this);
    connect(updateChecker_, &updater::UpdateChecker::updateAvailable,
            this, &MainWindow::onUpdateAvailable);
    connect(updateChecker_, &updater::UpdateChecker::noUpdateAvailable,
        this, [this]() {
            if (manualUpdateCheck_) {
                QMessageBox::information(this, "업데이트 확인",
                    QString("현재 최신 버전입니다. (v%1)").arg(OCCT_VERSION_STRING));
                manualUpdateCheck_ = false;
            }
        });
    connect(updateChecker_, &updater::UpdateChecker::checkFailed,
        this, [this](const QString& error) {
            if (manualUpdateCheck_) {
                QMessageBox::warning(this, "업데이트 확인 실패",
                    "업데이트를 확인할 수 없습니다.\n" + error);
                manualUpdateCheck_ = false;
            }
        });
    QTimer::singleShot(30000, updateChecker_, &updater::UpdateChecker::checkForUpdate);

    // Log uploader for manual test stop (trigger B)
    logUploader_ = new updater::LogUploader(this);
    connect(logUploader_, &updater::LogUploader::uploadComplete, this,
        [this](const QString& url) {
            statusLabel_->setText("로그 전송 완료");
            qInfo() << "Test log uploaded:" << url;
        });
    connect(logUploader_, &updater::LogUploader::uploadFailed, this,
        [this](const QString& error) {
            statusLabel_->setText("로그 전송 실패");
            qWarning() << "Log upload failed:" << error;
        });

    // Sensor history: record every 5 seconds during tests
    sensorHistoryTimer_ = new QTimer(this);
    sensorHistoryTimer_->setInterval(5000);
    connect(sensorHistoryTimer_, &QTimer::timeout, this, [this]() {
        if (!sensorMgr_ || !sensorRecording_) return;
        QJsonObject snapshot;
        snapshot["time"] = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        snapshot["cpu_temp"] = sensorMgr_->get_cpu_temperature();
        snapshot["gpu_temp"] = sensorMgr_->get_gpu_temperature();
        snapshot["cpu_power"] = sensorMgr_->get_cpu_power();
        snapshot["cpu_power_estimated"] = sensorMgr_->is_cpu_power_estimated();

        // Data source diagnostics
        auto readings = sensorMgr_->get_all_readings();
        bool has_lhm_data = false;
        bool has_wmi_data = false;
        for (const auto& r : readings) {
            if (r.category == "CPU" && r.unit == "W" && r.name.find("Package") != std::string::npos)
                has_lhm_data = true;
            if (r.category == "CPU" && r.unit == "%" && r.name.find("CPU Usage") != std::string::npos)
                has_wmi_data = true;
        }
        snapshot["source_lhm"] = has_lhm_data;
        snapshot["source_wmi"] = has_wmi_data;
        snapshot["reading_count"] = static_cast<int>(readings.size());

        sensorHistory_.append(snapshot);
    });
}

MainWindow::~MainWindow()
{
    // Stop guardian before destroying sensor manager (preserves destruction order)
    if (safetyGuardian_) {
        safetyGuardian_->stop();
    }
    safetyGuardian_.reset();

    if (statusTimer_) {
        statusTimer_->stop();
    }

    if (sensorMgr_) {
        sensorMgr_->stop();
    }
    sensorMgr_.reset();
}

void MainWindow::setupUi()
{
    createMenuBar();

    centralContainer_ = new QWidget(this);
    setCentralWidget(centralContainer_);

    auto* mainLayout = new QVBoxLayout(centralContainer_);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    createHeaderBar();
    mainLayout->addWidget(headerBar_);

    auto* bodyLayout = new QHBoxLayout();
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    createSidebar();
    createContentArea();
    createPanels();

    bodyLayout->addWidget(sidebar_);
    bodyLayout->addWidget(contentStack_, 1);
    mainLayout->addLayout(bodyLayout, 1);

    createStatusBarWidgets();
}

void MainWindow::createHeaderBar()
{
    headerBar_ = new QFrame(centralContainer_);
    headerBar_->setObjectName("headerBar");
    headerBar_->setFixedHeight(50);
    headerBar_->setStyleSheet(
        "QFrame#headerBar { background-color: #C0392B; border: none; }"
        "QFrame#headerBar QLabel { color: white; background: transparent; }"
    );

    auto* layout = new QHBoxLayout(headerBar_);
    layout->setContentsMargins(20, 0, 20, 0);

    // App icon placeholder (unicode gear)
    auto* iconLabel = new QLabel(QString::fromUtf8("\xe2\x9a\x99"), headerBar_);
    QFont iconFont = iconLabel->font();
    iconFont.setPixelSize(24);
    iconLabel->setFont(iconFont);
    layout->addWidget(iconLabel);

    auto* titleLabel = new QLabel("OCCT 네이티브 스트레스 테스트", headerBar_);
    QFont titleFont = titleLabel->font();
    titleFont.setPixelSize(18);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);

    layout->addStretch();

    auto* versionLabel = new QLabel("v1.0.0", headerBar_);
    versionLabel->setStyleSheet("color: rgba(255,255,255,0.7);");
    layout->addWidget(versionLabel);
}

void MainWindow::createSidebar()
{
    sidebar_ = new QFrame(centralContainer_);
    sidebar_->setObjectName("sidebar");
    sidebar_->setFixedWidth(220);
    sidebar_->setStyleSheet(
        "QFrame#sidebar { background-color: #161B22; border-right: 1px solid #30363D; }"
    );

    sidebarLayout_ = new QVBoxLayout(sidebar_);
    sidebarLayout_->setContentsMargins(0, 10, 0, 10);
    sidebarLayout_->setSpacing(2);

    // Group 1: Main stress tests
    addNavButton("dashboard", "대시보드",    "\xf0\x9f\x93\x8a");
    addNavButton("cpu",       "CPU 테스트",     "\xf0\x9f\x94\xa5");
    addNavButton("gpu",       "GPU 테스트",     "\xf0\x9f\x8e\xae");
    addNavButton("ram",       "RAM 테스트",     "\xf0\x9f\x92\xbe");
    addNavButton("storage",   "저장장치 테스트", "\xf0\x9f\x92\xbf");
    addNavButton("psu",       "PSU 테스트",     "\xf0\x9f\x94\x8c");

    addSeparator();

    // Group 2: Scheduling & benchmarks
    addNavButton("schedule",    "스케줄",     "\xf0\x9f\x93\x85");
    addNavButton("benchmark",   "벤치마크",    "\xf0\x9f\x93\x90");
    addNavButton("certificate", "인증서",  "\xf0\x9f\x8f\x86");

    addSeparator();

    // Group 3: Monitoring & results
    addNavButton("monitor",   "모니터링",   "\xf0\x9f\x93\x89");
    addNavButton("sysinfo",   "시스템 정보",  "\xf0\x9f\x96\xa5");
    addNavButton("results",   "결과",      "\xf0\x9f\x93\x8b");

    sidebarLayout_->addStretch();
}

void MainWindow::addNavButton(const QString& key, const QString& text, const QString& iconChar)
{
    auto* btn = new QPushButton(QString::fromUtf8(iconChar.toUtf8()) + "  " + text, sidebar_);
    btn->setObjectName("nav_" + key);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(44);
    btn->setStyleSheet(
        "QPushButton {"
        "  background-color: transparent;"
        "  color: #8B949E;"
        "  border: none;"
        "  text-align: left;"
        "  padding: 0 20px;"
        "  font-size: 14px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #21262D;"
        "  color: #C9D1D9;"
        "}"
    );

    sidebarLayout_->addWidget(btn);
    navButtons_[key] = btn;

    connect(btn, &QPushButton::clicked, this, [this, key]() {
        onNavButtonClicked(key);
    });
}

void MainWindow::addSeparator()
{
    sidebarLayout_->addSpacing(10);
    auto* sep = new QFrame(sidebar_);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background-color: #30363D; max-height: 1px;");
    sidebarLayout_->addWidget(sep);
    sidebarLayout_->addSpacing(10);
}

void MainWindow::createContentArea()
{
    contentStack_ = new QStackedWidget(centralContainer_);
    contentStack_->setStyleSheet("QStackedWidget { background-color: #0D1117; }");
}

void MainWindow::createPanels()
{
    auto* dashboardPanel = new DashboardPanel(contentStack_);
    auto* cpuPanel       = new CpuPanel(contentStack_);
    auto* gpuPanel       = new GpuPanel(contentStack_);
    auto* ramPanel       = new RamPanel(contentStack_);
    auto* storagePanel   = new StoragePanel(contentStack_);
    auto* psuPanel       = new PsuPanel(contentStack_);
    auto* benchmarkPanel = new BenchmarkPanel(contentStack_);
    auto* monitorPanel   = new MonitorPanel(contentStack_);
    auto* sysinfoPanel   = new SysInfoPanel(contentStack_);
    resultsPanel_        = new ResultsPanel(contentStack_);
    auto* resultsPanel   = resultsPanel_;
    auto* schedulePanel  = new SchedulePanel(contentStack_);
    auto* certPanel      = new CertificatePanel(contentStack_);

    panels_["dashboard"]   = dashboardPanel;
    panels_["cpu"]         = cpuPanel;
    panels_["gpu"]         = gpuPanel;
    panels_["ram"]         = ramPanel;
    panels_["storage"]     = storagePanel;
    panels_["psu"]         = psuPanel;
    panels_["benchmark"]   = benchmarkPanel;
    panels_["monitor"]     = monitorPanel;
    panels_["sysinfo"]     = sysinfoPanel;
    panels_["results"]     = resultsPanel;
    panels_["schedule"]    = schedulePanel;
    panels_["certificate"] = certPanel;

    for (auto it = panels_.begin(); it != panels_.end(); ++it) {
        contentStack_->addWidget(it.value());
    }

    // Create shared SensorManager and distribute to panels that need sensor data
    sensorMgr_ = std::make_unique<SensorManager>();
    if (sensorMgr_->initialize()) {
        sensorMgr_->start_polling(500);
    } else {
        statusLabel_->setText("센서: 초기화 실패 (관리자 권한이 필요할 수 있습니다)");
    }

    monitorPanel->setSensorManager(sensorMgr_.get());
    cpuPanel->setSensorManager(sensorMgr_.get());
    gpuPanel->setSensorManager(sensorMgr_.get());
    dashboardPanel->setSensorManager(sensorMgr_.get());
    psuPanel->setSensorManager(sensorMgr_.get());

    // Set up SafetyGuardian and register all IEngine-based engines
    safetyGuardian_ = std::make_unique<SafetyGuardian>(sensorMgr_.get());
    safetyGuardian_->register_engine(cpuPanel->engine());
    safetyGuardian_->register_engine(gpuPanel->engine());
    safetyGuardian_->register_engine(ramPanel->engine());
    safetyGuardian_->register_engine(storagePanel->engine());
    safetyGuardian_->register_engine(psuPanel->engine());
    safetyGuardian_->start();

    // Connect Dashboard quick-start buttons to navigate to the correct panels
    connect(dashboardPanel, &DashboardPanel::startCpuTest, this, [this]() {
        setActiveTab("cpu");
    });
    connect(dashboardPanel, &DashboardPanel::startGpuTest, this, [this]() {
        setActiveTab("gpu");
    });
    connect(dashboardPanel, &DashboardPanel::startFullTest, this, [this]() {
        setActiveTab("cpu");
    });

    // Connect testStartRequested → start sensor recording
    auto startRecording = [this]() {
        sensorHistory_ = QJsonArray();
        sensorRecording_ = true;
        sensorHistoryTimer_->start();
    };
    connect(cpuPanel, &CpuPanel::testStartRequested, this, startRecording);
    connect(gpuPanel, &GpuPanel::testStartRequested, this, startRecording);
    connect(ramPanel, &RamPanel::testStartRequested, this, startRecording);
    connect(storagePanel, &StoragePanel::testStartRequested, this, startRecording);
    connect(psuPanel, &PsuPanel::testStartRequested, this, startRecording);

    // Connect testStopRequested → stop recording + log upload (trigger B)
    connect(cpuPanel, &CpuPanel::testStopRequested, this, [this]() { onTestStopped("cpu"); });
    connect(gpuPanel, &GpuPanel::testStopRequested, this, [this]() { onTestStopped("gpu"); });
    connect(ramPanel, &RamPanel::testStopRequested, this, [this]() { onTestStopped("ram"); });
    connect(storagePanel, &StoragePanel::testStopRequested, this, [this]() { onTestStopped("storage"); });
    connect(psuPanel, &PsuPanel::testStopRequested, this, [this]() { onTestStopped("psu"); });
}

void MainWindow::createStatusBarWidgets()
{
    auto* sb = statusBar();
    sb->setStyleSheet(
        "QStatusBar { background-color: #161B22; color: #8B949E; border-top: 1px solid #30363D; }"
        "QStatusBar::item { border: none; }"
    );

    statusLabel_ = new QLabel("준비", sb);
    statusLabel_->setStyleSheet("color: #8B949E; padding: 2px 8px;");
    sb->addWidget(statusLabel_, 1);

    timeLabel_ = new QLabel(sb);
    timeLabel_->setStyleSheet("color: #8B949E; padding: 2px 8px;");
    sb->addPermanentWidget(timeLabel_);

    connect(statusTimer_, &QTimer::timeout, this, [this]() {
        timeLabel_->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    });
}

void MainWindow::onNavButtonClicked(const QString& panelKey)
{
    setActiveTab(panelKey);
}

void MainWindow::setActiveTab(const QString& key)
{
    if (!panels_.contains(key)) return;

    currentPanel_ = key;
    contentStack_->setCurrentWidget(panels_[key]);

    // Update button styles
    for (auto it = navButtons_.begin(); it != navButtons_.end(); ++it) {
        QPushButton* btn = it.value();
        bool active = (it.key() == key);

        QString style;
        if (active) {
            style =
                "QPushButton {"
                "  background-color: #1F2937;"
                "  color: #F0F6FC;"
                "  border: none;"
                "  border-left: 3px solid #C0392B;"
                "  text-align: left;"
                "  padding: 0 17px;"
                "  font-size: 14px;"
                "  font-weight: bold;"
                "}"
                "QPushButton:hover {"
                "  background-color: #1F2937;"
                "  color: #F0F6FC;"
                "}";
        } else {
            style =
                "QPushButton {"
                "  background-color: transparent;"
                "  color: #8B949E;"
                "  border: none;"
                "  text-align: left;"
                "  padding: 0 20px;"
                "  font-size: 14px;"
                "}"
                "QPushButton:hover {"
                "  background-color: #21262D;"
                "  color: #C9D1D9;"
                "}";
        }
        btn->setStyleSheet(style);
    }

    statusLabel_->setText("패널: " + key);
}

void MainWindow::updateStatusBar()
{
    // Could be extended with real system info
}

void MainWindow::createMenuBar()
{
    auto* mb = menuBar();
    mb->setStyleSheet(
        "QMenuBar { background-color: #161B22; color: #C9D1D9; border-bottom: 1px solid #30363D; }"
        "QMenuBar::item { padding: 6px 12px; }"
        "QMenuBar::item:selected { background-color: #21262D; }"
        "QMenu { background-color: #161B22; color: #C9D1D9; border: 1px solid #30363D; }"
        "QMenu::item { padding: 6px 24px; }"
        "QMenu::item:selected { background-color: #1F2937; }"
        "QMenu::separator { height: 1px; background: #30363D; margin: 4px 8px; }"
    );

    // File menu
    auto* fileMenu = mb->addMenu("&파일");
    auto* exportAction = fileMenu->addAction("보고서 내보내기...");
    connect(exportAction, &QAction::triggered, this, &MainWindow::onExportReport);
    auto* tokenAction = fileMenu->addAction("GitHub 토큰 설정...");
    connect(tokenAction, &QAction::triggered, this, &MainWindow::onTokenSettings);
    fileMenu->addSeparator();
    auto* exitAction = fileMenu->addAction("종료");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &MainWindow::onExit);

    // Test menu
    auto* testMenu = mb->addMenu("&테스트");
    auto* startScheduleAction = testMenu->addAction("스케줄 시작");
    connect(startScheduleAction, &QAction::triggered, this, [this]() {
        setActiveTab("schedule");
    });
    auto* stopAllAction = testMenu->addAction("모든 테스트 중지");
    connect(stopAllAction, &QAction::triggered, this, &MainWindow::stopAllTests);

    // Help menu
    auto* helpMenu = mb->addMenu("&도움말");
    auto* checkUpdateAction = helpMenu->addAction("업데이트 확인...");
    connect(checkUpdateAction, &QAction::triggered, this, &MainWindow::onCheckForUpdate);
    helpMenu->addSeparator();
    auto* aboutAction = helpMenu->addAction("정보");
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::onExportReport()
{
    setActiveTab("results");
    if (resultsPanel_) {
        resultsPanel_->onExportHtmlClicked();
    }
}

void MainWindow::onExit()
{
    QApplication::quit();
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, "OCCT 네이티브 스트레스 테스트 정보",
        "<h2>OCCT 네이티브 스트레스 테스트</h2>"
        "<p>버전 1.0.0</p>"
        "<p>CPU, GPU, RAM, 저장장치, PSU 안정성 검증을 위한 "
        "전문 하드웨어 스트레스 테스트 도구입니다.</p>"
        "<p>기능:</p>"
        "<ul>"
        "<li>CPU: AVX2/AVX-512/SSE/Linpack/Prime 스트레스 테스트</li>"
        "<li>GPU: OpenCL/Vulkan 연산 스트레스</li>"
        "<li>RAM: March C/Walking Ones/Random 패턴 테스트</li>"
        "<li>저장장치: 순차/랜덤 I/O 테스트</li>"
        "<li>PSU: CPU+GPU 결합 부하 패턴</li>"
        "<li>실시간 하드웨어 모니터링</li>"
        "<li>테스트 스케줄링 및 자동화</li>"
        "<li>안정성 인증서 생성</li>"
        "<li>보고서 내보내기: HTML, PNG, CSV, JSON</li>"
        "</ul>"
        "<p>Qt6와 C++17로 개발되었습니다.</p>"
    );
}

// ─── System Tray Icon ────────────────────────────────────────────────────────

void MainWindow::setupTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    trayIcon_ = new QSystemTrayIcon(this);
    trayIcon_->setIcon(windowIcon().isNull() ? QApplication::style()->standardIcon(QStyle::SP_ComputerIcon)
                                              : windowIcon());
    trayIcon_->setToolTip("OCCT 네이티브 스트레스 테스트");

    trayMenu_ = new QMenu(this);

    auto* showHideAction = trayMenu_->addAction(QString::fromUtf8("\xeb\xb3\xb4\xec\x9d\xb4\xea\xb8\xb0/\xec\x88\xa8\xea\xb8\xb0\xea\xb8\xb0"));
    connect(showHideAction, &QAction::triggered, this, [this]() {
        if (isVisible()) {
            hide();
        } else {
            show();
            raise();
            activateWindow();
        }
    });

    trayMenu_->addSeparator();

    auto* stopAllAction = trayMenu_->addAction(QString::fromUtf8("\xeb\xaa\xa8\xeb\x93\xa0 \xed\x85\x8c\xec\x8a\xa4\xed\x8a\xb8 \xec\xa4\x91\xec\xa7\x80"));
    connect(stopAllAction, &QAction::triggered, this, &MainWindow::stopAllTests);

    trayMenu_->addSeparator();

    auto* quitAction = trayMenu_->addAction(QString::fromUtf8("\xec\xa2\x85\xeb\xa3\x8c"));
    connect(quitAction, &QAction::triggered, this, &MainWindow::onExit);

    trayIcon_->setContextMenu(trayMenu_);

    connect(trayIcon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            if (isVisible()) {
                hide();
            } else {
                show();
                raise();
                activateWindow();
            }
        }
    });

    trayIcon_->show();
}

// ─── Keyboard Shortcuts ─────────────────────────────────────────────────────

void MainWindow::setupShortcuts()
{
    // Panel navigation
    new QShortcut(QKeySequence("Ctrl+1"), this, [this]() { setActiveTab("dashboard"); });
    new QShortcut(QKeySequence("Ctrl+2"), this, [this]() { setActiveTab("cpu"); });
    new QShortcut(QKeySequence("Ctrl+3"), this, [this]() { setActiveTab("gpu"); });
    new QShortcut(QKeySequence("Ctrl+4"), this, [this]() { setActiveTab("ram"); });
    new QShortcut(QKeySequence("Ctrl+5"), this, [this]() { setActiveTab("storage"); });
    new QShortcut(QKeySequence("Ctrl+6"), this, [this]() { setActiveTab("monitor"); });

    // Escape = emergency stop all
    new QShortcut(QKeySequence("Escape"), this, [this]() { stopAllTests(); });
}

void MainWindow::stopAllTests()
{
    // Stop all engines via their panels
    for (auto it = panels_.begin(); it != panels_.end(); ++it) {
        const QString& key = it.key();
        QWidget* panel = it.value();

        if (key == "cpu") {
            if (auto* p = qobject_cast<CpuPanel*>(panel)) {
                if (p->engine() && p->engine()->is_running()) {
                    p->engine()->stop();
                }
            }
        } else if (key == "gpu") {
            if (auto* p = qobject_cast<GpuPanel*>(panel)) {
                if (p->engine() && p->engine()->is_running()) {
                    p->engine()->stop();
                }
            }
        } else if (key == "ram") {
            if (auto* p = qobject_cast<RamPanel*>(panel)) {
                if (p->engine() && p->engine()->is_running()) {
                    p->engine()->stop();
                }
            }
        } else if (key == "storage") {
            if (auto* p = qobject_cast<StoragePanel*>(panel)) {
                if (p->engine() && p->engine()->is_running()) {
                    p->engine()->stop();
                }
            }
        } else if (key == "psu") {
            if (auto* p = qobject_cast<PsuPanel*>(panel)) {
                if (p->engine() && p->engine()->is_running()) {
                    p->engine()->stop();
                }
            }
        }
    }

    statusLabel_->setText("모든 테스트 중지됨 (긴급 중지)");
    playTestErrorSound();
    onTestStopped("all");
}

// ─── Update & Log Upload ─────────────────────────────────────────────────────

void MainWindow::onCheckForUpdate() {
    manualUpdateCheck_ = true;
    statusLabel_->setText("업데이트 확인 중...");
    updateChecker_->checkForUpdate();
}

void MainWindow::onTokenSettings() {
    auto* dialog = new TokenDialog(this);
    dialog->exec();
    dialog->deleteLater();
}

void MainWindow::onUpdateAvailable(const updater::UpdateInfo& info)
{
    manualUpdateCheck_ = false;
    auto* dialog = new updater::UpdateDialog(info, OCCT_VERSION_STRING, this);

    connect(dialog, &updater::UpdateDialog::updateAccepted, this, [this, info, dialog]() {
        dialog->showProgress();

        auto* downloader = new updater::UpdateDownloader(this);

        connect(downloader, &updater::UpdateDownloader::progressChanged, dialog,
            &updater::UpdateDialog::setProgress);
        connect(downloader, &updater::UpdateDownloader::speedChanged, dialog,
            &updater::UpdateDialog::setSpeed);

        connect(downloader, &updater::UpdateDownloader::downloadComplete, this,
            [this, dialog](const QString& zipPath) {
                dialog->setStage("설치 준비 중...");
                dialog->startCountdown();

                connect(dialog, &updater::UpdateDialog::countdownFinished, this,
                    [zipPath]() {
                        updater::UpdateInstaller installer;
                        installer.install(zipPath);
                    });
            });

        connect(downloader, &updater::UpdateDownloader::downloadFailed, this,
            [dialog](const QString& error) {
                dialog->setStage("다운로드 실패: " + error);
            });

        connect(downloader, &updater::UpdateDownloader::verificationFailed, this,
            [dialog]() {
                dialog->setStage("파일 검증 실패 - 다시 시도해주세요");
            });

        downloader->download(info);
    });

    dialog->show();
}

void MainWindow::onTestStopped(const QString& engineName)
{
    // Stop sensor recording
    sensorRecording_ = false;
    sensorHistoryTimer_->stop();

    if (!logUploader_ || !logUploader_->hasToken()) return;

    QJsonObject testResults;
    testResults["engine"] = engineName;
    testResults["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    testResults["app_version"] = OCCT_VERSION_STRING;

    // ── Collect metrics from all relevant engines ──
    for (auto it = panels_.begin(); it != panels_.end(); ++it) {
        const QString& key = it.key();
        if (engineName != "all" && key != engineName) continue;

        if (key == "cpu") {
            if (auto* p = qobject_cast<CpuPanel*>(it.value())) {
                auto* eng = dynamic_cast<CpuEngine*>(p->engine());
                if (eng) {
                    auto m = eng->get_metrics();
                    QJsonObject o;
                    o["gflops"] = m.gflops;
                    o["peak_gflops"] = m.peak_gflops;
                    o["temperature"] = m.temperature;
                    o["power_watts"] = m.power_watts;
                    o["power_estimated"] = m.power_estimated;
                    o["active_threads"] = m.active_threads;
                    o["error_count"] = m.error_count;
                    o["elapsed_secs"] = m.elapsed_secs;
                    testResults["cpu"] = o;
                }
            }
        } else if (key == "gpu") {
            if (auto* p = qobject_cast<GpuPanel*>(it.value())) {
                auto* eng = dynamic_cast<GpuEngine*>(p->engine());
                if (eng) {
                    auto m = eng->get_metrics();
                    QJsonObject o;
                    o["gflops"] = m.gflops;
                    o["temperature"] = m.temperature;
                    o["power_watts"] = m.power_watts;
                    o["gpu_usage_pct"] = m.gpu_usage_pct;
                    o["vram_usage_pct"] = m.vram_usage_pct;
                    o["vram_errors"] = static_cast<qint64>(m.vram_errors);
                    o["artifact_count"] = static_cast<qint64>(m.artifact_count);
                    o["fps"] = m.fps;
                    o["elapsed_secs"] = m.elapsed_secs;
                    testResults["gpu"] = o;
                }
            }
        } else if (key == "ram") {
            if (auto* p = qobject_cast<RamPanel*>(it.value())) {
                auto* eng = dynamic_cast<RamEngine*>(p->engine());
                if (eng) {
                    auto m = eng->get_metrics();
                    QJsonObject o;
                    o["bandwidth_mbs"] = m.bandwidth_mbs;
                    o["errors_found"] = static_cast<qint64>(m.errors_found);
                    o["memory_used_mb"] = m.memory_used_mb;
                    o["pages_locked"] = m.pages_locked;
                    o["progress_pct"] = m.progress_pct;
                    o["elapsed_secs"] = m.elapsed_secs;
                    testResults["ram"] = o;
                }
            }
        } else if (key == "storage") {
            if (auto* p = qobject_cast<StoragePanel*>(it.value())) {
                auto* eng = dynamic_cast<StorageEngine*>(p->engine());
                if (eng) {
                    auto m = eng->get_metrics();
                    QJsonObject o;
                    o["write_mbs"] = m.write_mbs;
                    o["read_mbs"] = m.read_mbs;
                    o["iops"] = m.iops;
                    o["latency_us"] = m.latency_us;
                    o["error_count"] = m.error_count;
                    o["verify_errors"] = static_cast<qint64>(m.verify_errors);
                    o["crc_errors"] = static_cast<qint64>(m.crc_errors);
                    o["progress_pct"] = m.progress_pct;
                    o["elapsed_secs"] = m.elapsed_secs;
                    testResults["storage"] = o;
                }
            }
        } else if (key == "psu") {
            if (auto* p = qobject_cast<PsuPanel*>(it.value())) {
                auto* eng = dynamic_cast<PsuEngine*>(p->engine());
                if (eng) {
                    auto m = eng->get_metrics();
                    QJsonObject o;
                    o["total_power_watts"] = m.total_power_watts;
                    o["cpu_power_watts"] = m.cpu_power_watts;
                    o["gpu_power_watts"] = m.gpu_power_watts;
                    o["power_stability_pct"] = m.power_stability_pct;
                    o["max_power_drop_watts"] = m.max_power_drop_watts;
                    o["power_drop_events"] = m.power_drop_events;
                    o["errors_cpu"] = m.errors_cpu;
                    o["errors_gpu"] = m.errors_gpu;
                    o["elapsed_secs"] = m.elapsed_secs;
                    testResults["psu"] = o;
                }
            }
        }
    }

    // ── Sensor snapshot (temperature/power from SensorManager) ──
    if (sensorMgr_) {
        QJsonObject sensors;
        sensors["cpu_temperature"] = sensorMgr_->get_cpu_temperature();
        sensors["gpu_temperature"] = sensorMgr_->get_gpu_temperature();
        sensors["cpu_power"] = sensorMgr_->get_cpu_power();
        sensors["cpu_power_estimated"] = sensorMgr_->is_cpu_power_estimated();

        QJsonArray allReadings;
        for (const auto& r : sensorMgr_->get_all_readings()) {
            QJsonObject reading;
            reading["name"] = QString::fromStdString(r.name);
            reading["category"] = QString::fromStdString(r.category);
            reading["value"] = r.value;
            reading["unit"] = QString::fromStdString(r.unit);
            allReadings.append(reading);
        }
        sensors["all_readings"] = allReadings;

        // Sensor history timeline (5s intervals during test)
        sensors["history"] = sensorHistory_;
        sensors["history_count"] = sensorHistory_.size();

        testResults["sensors"] = sensors;
    }

    QString resultsJson = QJsonDocument(testResults).toJson(QJsonDocument::Indented);

    // ── System info ──
    QJsonObject sysInfo;
    sysInfo["hostname"] = QHostInfo::localHostName();
    sysInfo["app_version"] = OCCT_VERSION_STRING;
    sysInfo["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    QString sysInfoJson = QJsonDocument(sysInfo).toJson(QJsonDocument::Indented);

    logUploader_->upload(resultsJson, sysInfoJson, QString(), "manual_stop");
    statusLabel_->setText("테스트 로그 전송 중...");
}

// ─── Sound Alerts ────────────────────────────────────────────────────────────

void MainWindow::playTestCompleteSound()
{
    if (!soundAlertsEnabled_) return;

#if defined(_WIN32)
    MessageBeep(MB_OK);
#else
    QApplication::beep();
#endif
}

void MainWindow::playTestErrorSound()
{
    if (!soundAlertsEnabled_) return;

#if defined(_WIN32)
    MessageBeep(MB_ICONEXCLAMATION);
#else
    QApplication::beep();
#endif
}

}} // namespace occt::gui
