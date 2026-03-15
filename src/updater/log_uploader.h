#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>

namespace occt { namespace updater {

// ─── Uploads test logs to a GitHub Secret Gist ──────────────────────────────
class LogUploader : public QObject {
    Q_OBJECT
public:
    explicit LogUploader(QObject* parent = nullptr);

    /// Upload test results, system info, and app log to a secret Gist.
    /// @param trigger  "post_update" or "manual_stop"
    void upload(const QString& testResultsJson,
                const QString& systemInfoJson,
                const QString& logContent,
                const QString& trigger);

    /// Set the GitHub Personal Access Token.
    void setToken(const QString& token);

    /// Returns true if a PAT is available (env, config, or explicit).
    bool hasToken() const;

signals:
    void uploadComplete(const QString& gistUrl);
    void uploadFailed(const QString& error);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    /// Resolve token from env -> AppConfig -> empty.
    QString resolveToken() const;

    /// Read the latest log file (today), tail-truncated to maxBytes.
    static QString readLatestLog(qint64 maxBytes = 100 * 1024);

    QNetworkAccessManager* manager_ = nullptr;
    QString token_;
};

}} // namespace occt::updater
