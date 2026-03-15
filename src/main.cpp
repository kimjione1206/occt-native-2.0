#include <cstring>

#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QStyleFactory>
#include <QSettings>
#include <QTimer>

#include "config.h"
#include "cli/cli_args.h"
#include "cli/cli_runner.h"
#include "gui/main_window.h"
#include "utils/portable_paths.h"
#include "utils/file_logger.h"
#include "utils/app_config.h"
#include "updater/post_update_runner.h"
#include "updater/update_checker.h"

// Check for a specific flag early before creating any Qt application object.
static bool has_flag(int argc, char* argv[], const char* flag)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0) return true;
    }
    return false;
}

static int run_cli(int argc, char* argv[])
{
    // Use QCoreApplication for CLI mode - no GUI modules needed at runtime.
    QCoreApplication app(argc, argv);
    app.setApplicationName("OCCT Native Stress Test");
    app.setApplicationVersion(OCCT_VERSION_STRING);
    app.setOrganizationName("OCCTNative");

    QSettings::setDefaultFormat(QSettings::IniFormat);
    occt::utils::PortablePaths::init();
    occt::utils::FileLogger::init();

    auto opts = occt::parse_args(argc, argv);

    occt::CliRunner runner;
    int result = runner.run(opts);

    occt::utils::FileLogger::shutdown();
    return result;
}

static int run_gui(int argc, char* argv[])
{
    // High DPI support
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    app.setApplicationName("OCCT Native Stress Test");
    app.setApplicationVersion(OCCT_VERSION_STRING);
    app.setOrganizationName("OCCTNative");
    app.setOrganizationDomain("occt-native.local");

    QSettings::setDefaultFormat(QSettings::IniFormat);
    occt::utils::PortablePaths::init();
    occt::utils::FileLogger::init();

    qInfo() << "OCCT Native Stress Test v" OCCT_VERSION_STRING;
    qInfo() << "Portable mode:" << occt::utils::PortablePaths::isPortable();
    qInfo() << "Config dir:" << occt::utils::PortablePaths::configDir();
    qInfo() << "Logs dir:" << occt::utils::PortablePaths::logsDir();

    // Load dark theme stylesheet
    QFile styleFile(":/styles/dark_theme.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream stream(&styleFile);
        QString styleSheet = stream.readAll();
        app.setStyleSheet(styleSheet);
        styleFile.close();
    }

    app.setStyle(QStyleFactory::create("Fusion"));

    // Create main window and restore geometry
    occt::gui::MainWindow mainWindow;
    auto& config = occt::utils::AppConfig::instance();
    QByteArray savedGeometry = config.windowGeometry();
    if (!savedGeometry.isEmpty()) {
        mainWindow.restoreGeometry(savedGeometry);
    }
    QByteArray savedState = config.windowState();
    if (!savedState.isEmpty()) {
        mainWindow.restoreState(savedState);
    }
    mainWindow.show();

    // Post-update: run smoke test automatically after update
    if (has_flag(argc, argv, "--post-update")) {
        qInfo() << "Post-update mode: running smoke test...";
        QTimer::singleShot(2000, [&mainWindow]() {
            auto* runner = new occt::updater::PostUpdateRunner(&mainWindow);
            QObject::connect(runner, &occt::updater::PostUpdateRunner::testComplete,
                [](bool passed, const QString& summary) {
                    qInfo() << "Post-update smoke test:" << (passed ? "PASS" : "FAIL") << summary;
                });
            QObject::connect(runner, &occt::updater::PostUpdateRunner::uploadComplete,
                [](const QString& gistUrl) {
                    qInfo() << "Test results uploaded:" << gistUrl;
                });
            runner->run();
        });
    }

    QObject::connect(&app, &QApplication::aboutToQuit, [&]() {
        config.setWindowGeometry(mainWindow.saveGeometry());
        config.setWindowState(mainWindow.saveState());
        config.sync();
        occt::utils::FileLogger::shutdown();
    });

    return app.exec();
}

int main(int argc, char* argv[])
{
    // Handle --help and --version before creating any Qt objects.
    if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h")) {
        occt::print_usage();
        return 0;
    }
    if (has_flag(argc, argv, "--version") || has_flag(argc, argv, "-v")) {
        std::fprintf(stdout, "OCCT Native Stress Test v%s\n", OCCT_VERSION_STRING);
        return 0;
    }

    // Detect --cli flag EARLY, before any Qt objects are created.
    if (has_flag(argc, argv, "--cli")) {
        return run_cli(argc, argv);
    }
    return run_gui(argc, argv);
}
