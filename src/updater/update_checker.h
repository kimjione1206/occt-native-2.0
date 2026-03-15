#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>

namespace occt { namespace updater {

// ─── Update metadata parsed from GitHub Releases API ─────────────────────────
struct UpdateInfo {
    QString version;        // e.g. "1.1.0"
    QString downloadUrl;    // GitHub asset URL (.zip)
    QString sha256;         // checksum parsed from release body
    QString releaseNotes;   // markdown body
    qint64  fileSize = 0;   // bytes
    QString releaseDate;
};

// ─── Checks the latest GitHub release for a newer version ────────────────────
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);

    /// Initiate an asynchronous check against GitHub Releases API.
    void checkForUpdate();

    /// Semantic version comparison: returns true if remote > local.
    static bool isNewerVersion(const QString& remote, const QString& local);

signals:
    void updateAvailable(const UpdateInfo& info);
    void noUpdateAvailable();
    void checkFailed(const QString& error);

private slots:
    void onReplyFinished();

private:
    /// Parse SHA256 hex string from release body text.
    static QString parseSha256(const QString& body);

    QNetworkAccessManager* manager_ = nullptr;
};

}} // namespace occt::updater
