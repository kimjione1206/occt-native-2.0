#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace occt { namespace updater {

// ─── Handles ZIP extraction, file replacement, and app restart ───────────────
class UpdateInstaller : public QObject {
    Q_OBJECT
public:
    explicit UpdateInstaller(QObject* parent = nullptr);

    /// Extract ZIP, generate replacement script, and execute.
    /// Returns true if the installation process was successfully started.
    bool install(const QString& zipPath);

    /// Restart the application with optional extra arguments.
    static void restartApp(const QStringList& extraArgs = {});

signals:
    void installStarted();
    void installFailed(const QString& error);
    void stageChanged(const QString& stage);

private:
    /// Extract ZIP to target directory using platform-appropriate method.
    bool extractZip(const QString& zipPath, const QString& destDir);

    /// (Windows) Create and run a batch script for file replacement.
    bool createAndRunBatchScript(const QString& updateDir, const QString& appDir);

    /// (macOS/Linux) Direct file copy replacement.
    bool directReplace(const QString& updateDir, const QString& appDir);

    /// Recursively copy directory contents.
    static bool copyDirectory(const QString& srcDir, const QString& dstDir);
};

}} // namespace occt::updater
