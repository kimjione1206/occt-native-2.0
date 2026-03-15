#include "log_uploader.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "config.h"
#include "utils/app_config.h"
#include "utils/portable_paths.h"
#include "utils/secure_token_store.h"

namespace occt { namespace updater {

// ─── Construction ───────────────────────────────────────────────────────────
LogUploader::LogUploader(QObject* parent)
    : QObject(parent)
    , manager_(new QNetworkAccessManager(this))
{
    connect(manager_, &QNetworkAccessManager::finished,
            this, &LogUploader::onReplyFinished);
}

// ─── Token management ───────────────────────────────────────────────────────
void LogUploader::setToken(const QString& token)
{
    token_ = token;
}

bool LogUploader::hasToken() const
{
    return !resolveToken().isEmpty();
}

QString LogUploader::resolveToken() const
{
    // 1. Explicit token set via setToken()
    if (!token_.isEmpty())
        return token_;

    // 2. Environment variable
    const QByteArray envToken = qgetenv("OCCT_GITHUB_TOKEN");
    if (!envToken.isEmpty())
        return QString::fromUtf8(envToken);

    // 3. Secure token store (with auto-migration from plaintext)
    const QString secureToken = utils::SecureTokenStore::instance().retrieveToken();
    if (!secureToken.isEmpty())
        return secureToken;

    return {};
}

// ─── Upload ─────────────────────────────────────────────────────────────────
void LogUploader::upload(const QString& testResultsJson,
                         const QString& systemInfoJson,
                         const QString& logContent,
                         const QString& trigger)
{
    const QString pat = resolveToken();
    if (pat.isEmpty()) {
        qWarning() << "[LogUploader] No GitHub token available — skipping upload.";
        emit uploadFailed(QStringLiteral("No GitHub token configured"));
        return;
    }

    // Resolve log content: use provided or read latest file
    QString logData = logContent;
    if (logData.isEmpty())
        logData = readLatestLog();

    const QString hostname  = QHostInfo::localHostName();
    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    const QString version   = QStringLiteral(OCCT_VERSION_STRING);

    // Build Gist payload
    QJsonObject filesObj;
    filesObj[QStringLiteral("test_results.json")] =
        QJsonObject{{QStringLiteral("content"), testResultsJson}};
    filesObj[QStringLiteral("system_info.json")] =
        QJsonObject{{QStringLiteral("content"), systemInfoJson}};
    filesObj[QStringLiteral("app.log")] =
        QJsonObject{{QStringLiteral("content"),
                     logData.isEmpty() ? QStringLiteral("(no log data)") : logData}};

    QJsonObject body;
    body[QStringLiteral("description")] =
        QStringLiteral("OCCT Native v%1 - %2 - %3 - %4")
            .arg(version, trigger, hostname, timestamp);
    body[QStringLiteral("public")] = false;
    body[QStringLiteral("files")]  = filesObj;

    // Send request
    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/gists")));
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(pat).toUtf8());
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    manager_->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

// ─── Reply handler ──────────────────────────────────────────────────────────
void LogUploader::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        const QString errMsg = QStringLiteral("Gist upload failed: %1 (HTTP %2)")
            .arg(reply->errorString())
            .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
        qWarning() << "[LogUploader]" << errMsg;
        emit uploadFailed(errMsg);
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    const QString gistUrl   = doc.object().value(QStringLiteral("html_url")).toString();

    if (gistUrl.isEmpty()) {
        emit uploadFailed(QStringLiteral("Gist created but URL missing from response"));
        return;
    }

    qInfo() << "[LogUploader] Gist uploaded:" << gistUrl;
    emit uploadComplete(gistUrl);
}

// ─── Log file reader ────────────────────────────────────────────────────────
QString LogUploader::readLatestLog(qint64 maxBytes)
{
    const QString logsDir = utils::PortablePaths::logsDir();
    const QDir dir(logsDir);
    if (!dir.exists())
        return {};

    // Find today's log file (pattern: occt_YYYY-MM-DD.log)
    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    const QStringList candidates = dir.entryList(
        {QStringLiteral("occt_*.log")}, QDir::Files, QDir::Time);

    QString logPath;
    for (const QString& f : candidates) {
        if (f.contains(today)) {
            logPath = dir.absoluteFilePath(f);
            break;
        }
    }
    // Fallback: most recent log file regardless of date
    if (logPath.isEmpty() && !candidates.isEmpty())
        logPath = dir.absoluteFilePath(candidates.first());

    if (logPath.isEmpty())
        return {};

    QFile file(logPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    const qint64 fileSize = file.size();
    if (fileSize <= maxBytes)
        return QString::fromUtf8(file.readAll());

    // Read last maxBytes (tail)
    file.seek(fileSize - maxBytes);
    // Skip partial first line
    file.readLine();
    return QStringLiteral("... (truncated, last %1 KB) ...\n").arg(maxBytes / 1024)
           + QString::fromUtf8(file.readAll());
}

}} // namespace occt::updater
