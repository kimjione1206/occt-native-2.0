#include "post_update_runner.h"
#include "log_uploader.h"

#include <QDateTime>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QThread>

#include "config.h"
#include "engines/cpu_engine.h"
#include "report/png_report.h"
#include "utils/portable_paths.h"

namespace occt { namespace updater {

// ─── Construction / Destruction ─────────────────────────────────────────────
PostUpdateRunner::PostUpdateRunner(QObject* parent)
    : QObject(parent)
    , uploader_(new LogUploader(this))
{
    connect(uploader_, &LogUploader::uploadComplete,
            this, &PostUpdateRunner::uploadComplete);
}

PostUpdateRunner::~PostUpdateRunner() = default;

// ─── Run ────────────────────────────────────────────────────────────────────
void PostUpdateRunner::run()
{
    qInfo() << "[PostUpdateRunner] Starting post-update smoke test"
            << "(threads:" << (QThread::idealThreadCount() / 2)
            << ", duration:" << kSmokeTestDurationSecs << "s)";

    engine_ = std::make_unique<CpuEngine>();

    // Use half the available threads for a gentle smoke test
    const int threads = std::max(1, QThread::idealThreadCount() / 2);

    engine_->start(CpuStressMode::AUTO, threads, kSmokeTestDurationSecs);

    // Poll engine status every 500 ms
    QTimer::singleShot(500, this, &PostUpdateRunner::pollEngine);
}

// ─── Poll engine completion ─────────────────────────────────────────────────
void PostUpdateRunner::pollEngine()
{
    if (!engine_ || !engine_->is_running()) {
        onTestFinished();
        return;
    }
    QTimer::singleShot(500, this, &PostUpdateRunner::pollEngine);
}

// ─── Test finished — collect results and upload ─────────────────────────────
void PostUpdateRunner::onTestFinished()
{
    if (!engine_) return;

    const CpuMetrics metrics = engine_->get_metrics();
    const bool passed = (metrics.error_count == 0);

    // ── Build TestResultData ────────────────────────────────────────────────
    TestResultData result;
    result.timestamp   = QDateTime::currentDateTime().toString(Qt::ISODate);
    result.test_type   = QStringLiteral("CPU");
    result.mode        = QStringLiteral("AUTO (smoke)");
    result.duration    = QStringLiteral("%1s").arg(
        static_cast<int>(metrics.elapsed_secs));
    result.score       = QStringLiteral("%1 GFLOPS").arg(metrics.gflops, 0, 'f', 2);
    result.passed      = passed;
    result.error_count = metrics.error_count;
    result.details     = passed
        ? QStringLiteral("Post-update smoke test passed")
        : QStringLiteral("Errors detected: %1").arg(metrics.error_count);

    // ── Test results JSON ───────────────────────────────────────────────────
    QJsonObject resultObj;
    resultObj[QStringLiteral("timestamp")]   = result.timestamp;
    resultObj[QStringLiteral("test_type")]   = result.test_type;
    resultObj[QStringLiteral("mode")]        = result.mode;
    resultObj[QStringLiteral("duration")]    = result.duration;
    resultObj[QStringLiteral("score")]       = result.score;
    resultObj[QStringLiteral("passed")]      = result.passed;
    resultObj[QStringLiteral("error_count")] = result.error_count;
    resultObj[QStringLiteral("details")]     = result.details;
    resultObj[QStringLiteral("verdict")]     = passed
        ? QStringLiteral("PASS") : QStringLiteral("FAIL");

    QJsonObject testResultsRoot;
    testResultsRoot[QStringLiteral("version")]  = QStringLiteral(OCCT_VERSION_STRING);
    testResultsRoot[QStringLiteral("hostname")] = QHostInfo::localHostName();
    testResultsRoot[QStringLiteral("results")]  = QJsonArray{resultObj};

    const QString testResultsJson =
        QJsonDocument(testResultsRoot).toJson(QJsonDocument::Indented);

    // ── System info JSON ────────────────────────────────────────────────────
    QJsonObject sysObj;
    sysObj[QStringLiteral("hostname")]    = QHostInfo::localHostName();
    sysObj[QStringLiteral("threads")]     = metrics.active_threads;
    sysObj[QStringLiteral("peak_gflops")] = metrics.peak_gflops;
#ifdef Q_OS_WIN
    sysObj[QStringLiteral("platform")] = QStringLiteral("Windows");
#elif defined(Q_OS_MACOS)
    sysObj[QStringLiteral("platform")] = QStringLiteral("macOS");
#else
    sysObj[QStringLiteral("platform")] = QStringLiteral("Linux");
#endif

    const QString systemInfoJson =
        QJsonDocument(sysObj).toJson(QJsonDocument::Indented);

    // ── Release engine ──────────────────────────────────────────────────────
    engine_.reset();

    // ── Emit test result ────────────────────────────────────────────────────
    const QString summary = QStringLiteral("CPU AUTO smoke %1 — %2 GFLOPS, %3 error(s)")
        .arg(passed ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
        .arg(metrics.gflops, 0, 'f', 2)
        .arg(metrics.error_count);

    emit testComplete(passed, summary);

    // ── Upload via LogUploader ──────────────────────────────────────────────
    if (uploader_->hasToken()) {
        uploader_->upload(testResultsJson, systemInfoJson,
                          QString() /* auto-read log */, QStringLiteral("post_update"));
    } else {
        qWarning() << "[PostUpdateRunner] No token — skipping log upload.";
    }
}

}} // namespace occt::updater
