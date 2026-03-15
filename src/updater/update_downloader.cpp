#include "update_downloader.h"
#include "utils/portable_paths.h"

#include <QCryptographicHash>
#include <QDir>

namespace occt { namespace updater {

// ─── Constructor ─────────────────────────────────────────────────────────────
UpdateDownloader::UpdateDownloader(QObject* parent)
    : QObject(parent)
    , manager_(new QNetworkAccessManager(this))
{
}

// ─── download ────────────────────────────────────────────────────────────────
void UpdateDownloader::download(const UpdateInfo& info)
{
    info_ = info;

    // Build destination path: <tempDir>/occt_update_v<version>.zip
    const QString tempDir = occt::utils::PortablePaths::tempDir();
    QDir().mkpath(tempDir);
    destPath_ = tempDir + QStringLiteral("/occt_update_v%1.zip").arg(info.version);

    // Open output file
    file_ = new QFile(destPath_, this);
    if (!file_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit downloadFailed(
            QStringLiteral("Cannot open file for writing: %1").arg(file_->errorString()));
        delete file_;
        file_ = nullptr;
        return;
    }

    // Start HTTP GET
    QNetworkRequest request(info.downloadUrl);
    request.setRawHeader("User-Agent", "occt-native-updater");
    // Follow redirects (GitHub assets redirect to S3)
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    reply_ = manager_->get(request);

    connect(reply_, &QNetworkReply::readyRead,
            this,   &UpdateDownloader::onReadyRead);
    connect(reply_, &QNetworkReply::downloadProgress,
            this,   &UpdateDownloader::onDownloadProgress);
    connect(reply_, &QNetworkReply::finished,
            this,   &UpdateDownloader::onFinished);

    // Reset speed tracking
    receivedBytes_ = 0;
    lastBytes_     = 0;
    speedTimer_.start();
}

// ─── cancel ──────────────────────────────────────────────────────────────────
void UpdateDownloader::cancel()
{
    if (reply_) {
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }
    if (file_) {
        file_->close();
        QFile::remove(destPath_);
        file_->deleteLater();
        file_ = nullptr;
    }
}

// ─── onReadyRead ─────────────────────────────────────────────────────────────
void UpdateDownloader::onReadyRead()
{
    if (file_ && reply_)
        file_->write(reply_->readAll());
}

// ─── onDownloadProgress ──────────────────────────────────────────────────────
void UpdateDownloader::onDownloadProgress(qint64 received, qint64 total)
{
    receivedBytes_ = received;
    emit progressChanged(received, total);

    // Calculate speed every ~500 ms
    const qint64 elapsed = speedTimer_.elapsed();
    if (elapsed >= 500) {
        const qint64 deltaBytes = received - lastBytes_;
        const double secs       = elapsed / 1000.0;
        const double speed      = (secs > 0.0) ? deltaBytes / secs : 0.0;

        emit speedChanged(speed);

        lastBytes_ = received;
        speedTimer_.restart();
    }
}

// ─── onFinished ──────────────────────────────────────────────────────────────
void UpdateDownloader::onFinished()
{
    if (!reply_) return;

    const bool hadError = (reply_->error() != QNetworkReply::NoError);
    const QString errorStr = reply_->errorString();

    // Flush remaining data
    if (file_ && reply_)
        file_->write(reply_->readAll());

    reply_->deleteLater();
    reply_ = nullptr;

    if (file_) {
        file_->close();
        file_->deleteLater();
        file_ = nullptr;
    }

    if (hadError) {
        QFile::remove(destPath_);
        emit downloadFailed(errorStr);
        return;
    }

    // SHA256 verification (skip if no checksum provided)
    if (!info_.sha256.isEmpty()) {
        if (!verifySha256(destPath_, info_.sha256)) {
            QFile::remove(destPath_);
            emit verificationFailed();
            return;
        }
    }

    emit downloadComplete(destPath_);
}

// ─── verifySha256 ────────────────────────────────────────────────────────────
bool UpdateDownloader::verifySha256(const QString& filePath, const QString& expected)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QCryptographicHash hash(QCryptographicHash::Sha256);

    // Read in 64 KB chunks to avoid loading the entire file into memory
    constexpr qint64 kChunkSize = 64 * 1024;
    while (!file.atEnd()) {
        hash.addData(file.read(kChunkSize));
    }

    const QString actual = hash.result().toHex().toLower();
    return (actual == expected.toLower());
}

}} // namespace occt::updater
