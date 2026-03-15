#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QElapsedTimer>

#include "update_checker.h"

namespace occt { namespace updater {

// ─── Downloads a release ZIP and verifies its SHA256 checksum ────────────────
class UpdateDownloader : public QObject {
    Q_OBJECT
public:
    explicit UpdateDownloader(QObject* parent = nullptr);

    /// Start downloading the update described by |info|.
    void download(const UpdateInfo& info);

    /// Cancel an in-progress download.
    void cancel();

signals:
    void progressChanged(qint64 received, qint64 total);
    void speedChanged(double bytesPerSec);
    void downloadComplete(const QString& filePath);
    void downloadFailed(const QString& error);
    void verificationFailed();

private slots:
    void onReadyRead();
    void onDownloadProgress(qint64 received, qint64 total);
    void onFinished();

private:
    bool verifySha256(const QString& filePath, const QString& expected);

    QNetworkAccessManager* manager_ = nullptr;
    QNetworkReply*         reply_   = nullptr;
    QFile*                 file_    = nullptr;

    UpdateInfo             info_;
    QString                destPath_;

    // Speed measurement
    QElapsedTimer          speedTimer_;
    qint64                 lastBytes_    = 0;
    qint64                 receivedBytes_ = 0;
};

}} // namespace occt::updater
