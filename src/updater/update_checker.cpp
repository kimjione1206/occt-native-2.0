#include "update_checker.h"
#include "config.h"

#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QVersionNumber>

namespace occt { namespace updater {

static const QString kReleasesUrl =
    QStringLiteral("https://api.github.com/repos/kimjione1206/occt-native-2.0/releases/latest");

// ─── Constructor ─────────────────────────────────────────────────────────────
UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , manager_(new QNetworkAccessManager(this))
{
}

// ─── checkForUpdate ──────────────────────────────────────────────────────────
void UpdateChecker::checkForUpdate()
{
    QNetworkRequest request(kReleasesUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "occt-native-updater");

    QNetworkReply* reply = manager_->get(request);
    connect(reply, &QNetworkReply::finished, this, &UpdateChecker::onReplyFinished);
}

// ─── onReplyFinished ─────────────────────────────────────────────────────────
void UpdateChecker::onReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit checkFailed(reply->errorString());
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        emit checkFailed(QStringLiteral("JSON parse error: %1").arg(parseError.errorString()));
        return;
    }

    const QJsonObject root = doc.object();

    // ── Parse tag_name → version (strip leading 'v') ─────────────────────
    QString tagName = root.value("tag_name").toString();
    if (tagName.startsWith('v') || tagName.startsWith('V'))
        tagName = tagName.mid(1);

    UpdateInfo info;
    info.version      = tagName;
    info.releaseNotes  = root.value("body").toString();
    info.releaseDate   = root.value("published_at").toString();
    info.sha256        = parseSha256(info.releaseNotes);

    // ── Find .zip asset ──────────────────────────────────────────────────
    const QJsonArray assets = root.value("assets").toArray();
    for (const QJsonValue& assetVal : assets) {
        const QJsonObject asset = assetVal.toObject();
        const QString name = asset.value("name").toString();
        if (name.endsWith(".zip", Qt::CaseInsensitive)) {
            info.downloadUrl = asset.value("browser_download_url").toString();
            info.fileSize    = asset.value("size").toInteger();
            break;
        }
    }

    if (info.downloadUrl.isEmpty()) {
        emit checkFailed(QStringLiteral("No .zip asset found in latest release"));
        return;
    }

    // ── Compare versions ─────────────────────────────────────────────────
    const QString localVersion = QStringLiteral(OCCT_VERSION_STRING);
    if (isNewerVersion(info.version, localVersion)) {
        emit updateAvailable(info);
    } else {
        emit noUpdateAvailable();
    }
}

// ─── isNewerVersion ──────────────────────────────────────────────────────────
bool UpdateChecker::isNewerVersion(const QString& remote, const QString& local)
{
    const QVersionNumber remoteVer = QVersionNumber::fromString(remote);
    const QVersionNumber localVer  = QVersionNumber::fromString(local);
    return remoteVer > localVer;
}

// ─── parseSha256 ─────────────────────────────────────────────────────────────
QString UpdateChecker::parseSha256(const QString& body)
{
    // Look for "SHA256:" or "sha256:" followed by a 64-char hex string
    static const QRegularExpression re(
        QStringLiteral("[Ss][Hh][Aa]256:\\s*([0-9a-fA-F]{64})"));
    const QRegularExpressionMatch match = re.match(body);
    if (match.hasMatch())
        return match.captured(1).toLower();
    return {};
}

}} // namespace occt::updater
