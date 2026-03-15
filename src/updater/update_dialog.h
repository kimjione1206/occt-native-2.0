#pragma once

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include "update_checker.h"

namespace occt { namespace updater {

// ─── Dialog that shows update notification and download progress ─────────────
class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    explicit UpdateDialog(const UpdateInfo& info, const QString& currentVersion,
                          QWidget* parent = nullptr);

    /// Switch from notification mode to progress mode.
    void showProgress();

public slots:
    /// Update progress bar based on bytes received / total.
    void setProgress(qint64 received, qint64 total);

    /// Display download speed (e.g. "2.3 MB/s").
    void setSpeed(double bytesPerSec);

    /// Update stage label text.
    void setStage(const QString& stage);

    /// Start "3초 후 재시작..." countdown.
    void startCountdown();

signals:
    void updateAccepted();
    void updateCancelled();
    void countdownFinished();

private:
    void buildNotificationUI();
    void buildProgressUI();
    static QString formatBytes(qint64 bytes);
    static QString formatSpeed(double bytesPerSec);
    static QString formatEta(double seconds);

    UpdateInfo info_;
    QString currentVersion_;

    // ── Notification mode widgets ──
    QWidget*      notifyWidget_  = nullptr;
    QLabel*       titleLabel_    = nullptr;
    QLabel*       versionLabel_  = nullptr;
    QTextBrowser* releaseNotes_  = nullptr;
    QPushButton*  updateBtn_     = nullptr;
    QPushButton*  laterBtn_      = nullptr;

    // ── Progress mode widgets ──
    QWidget*      progressWidget_ = nullptr;
    QProgressBar* progressBar_    = nullptr;
    QLabel*       speedLabel_     = nullptr;
    QLabel*       etaLabel_       = nullptr;
    QLabel*       stageLabel_     = nullptr;
    QPushButton*  cancelBtn_      = nullptr;

    // ── Countdown ──
    QTimer*       countdownTimer_ = nullptr;
    int           countdownSec_   = 3;

    QVBoxLayout*  mainLayout_    = nullptr;
};

}} // namespace occt::updater
