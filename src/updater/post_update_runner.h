#pragma once

#include <QObject>
#include <QString>
#include <memory>

namespace occt { class CpuEngine; }

namespace occt { namespace updater {

class LogUploader;

// ─── Runs a smoke test after an auto-update restart ─────────────────────────
class PostUpdateRunner : public QObject {
    Q_OBJECT
public:
    explicit PostUpdateRunner(QObject* parent = nullptr);
    ~PostUpdateRunner() override;

    /// Kick off the post-update smoke test sequence.
    void run();

signals:
    void testComplete(bool passed, const QString& summary);
    void uploadComplete(const QString& gistUrl);

private:
    void pollEngine();
    void onTestFinished();

    std::unique_ptr<CpuEngine> engine_;
    LogUploader* uploader_ = nullptr;

    static constexpr int kSmokeTestDurationSecs = 30;
};

}} // namespace occt::updater
