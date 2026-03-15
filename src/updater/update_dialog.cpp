#include "update_dialog.h"

#include <QHBoxLayout>

namespace occt { namespace updater {

// ─── Dark-theme palette constants ────────────────────────────────────────────
namespace {
constexpr const char* kDialogStyle =
    "QDialog { background-color: #0D1117; }";

constexpr const char* kTitleStyle =
    "QLabel { color: #C9D1D9; font-size: 20px; font-weight: bold; }";

constexpr const char* kVersionStyle =
    "QLabel { color: #8B949E; font-size: 14px; }";

constexpr const char* kStageStyle =
    "QLabel { color: #C9D1D9; font-size: 13px; }";

constexpr const char* kSpeedEtaStyle =
    "QLabel { color: #8B949E; font-size: 12px; }";

constexpr const char* kReleaseNotesStyle =
    "QTextBrowser { background-color: #161B22; color: #C9D1D9; "
    "border: 1px solid #30363D; border-radius: 6px; font-size: 13px; "
    "padding: 8px; }";

constexpr const char* kUpdateButtonStyle =
    "QPushButton { background-color: #27AE60; color: white; border: none; "
    "border-radius: 6px; font-size: 14px; font-weight: bold; padding: 8px 24px; }"
    "QPushButton:hover { background-color: #2ECC71; }";

constexpr const char* kLaterButtonStyle =
    "QPushButton { background-color: #21262D; color: #C9D1D9; "
    "border: 1px solid #30363D; border-radius: 6px; font-size: 14px; "
    "padding: 8px 24px; }"
    "QPushButton:hover { background-color: #30363D; }";

constexpr const char* kCancelButtonStyle =
    "QPushButton { background-color: #21262D; color: #C9D1D9; "
    "border: 1px solid #30363D; border-radius: 6px; font-size: 13px; "
    "padding: 6px 20px; }"
    "QPushButton:hover { background-color: #30363D; }";

constexpr const char* kProgressBarStyle =
    "QProgressBar { background-color: #161B22; border: 1px solid #30363D; "
    "border-radius: 4px; height: 22px; text-align: center; color: #C9D1D9; "
    "font-size: 12px; }"
    "QProgressBar::chunk { background-color: #C0392B; border-radius: 3px; }";
} // namespace

// ─── Constructor ─────────────────────────────────────────────────────────────
UpdateDialog::UpdateDialog(const UpdateInfo& info, const QString& currentVersion,
                           QWidget* parent)
    : QDialog(parent), info_(info), currentVersion_(currentVersion)
{
    setWindowTitle(QStringLiteral("OCCT 업데이트"));
    setFixedSize(480, 400);
    setStyleSheet(kDialogStyle);

    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(24, 20, 24, 20);
    mainLayout_->setSpacing(12);

    buildNotificationUI();
    buildProgressUI();

    // Start in notification mode
    notifyWidget_->setVisible(true);
    progressWidget_->setVisible(false);
}

// ─── Notification mode UI ────────────────────────────────────────────────────
void UpdateDialog::buildNotificationUI()
{
    notifyWidget_ = new QWidget(this);
    auto* layout = new QVBoxLayout(notifyWidget_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // Title
    titleLabel_ = new QLabel(QStringLiteral("새 버전이 있습니다"), notifyWidget_);
    titleLabel_->setStyleSheet(kTitleStyle);
    titleLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel_);

    // Version info
    versionLabel_ = new QLabel(
        QStringLiteral("%1  \xe2\x86\x92  %2").arg(currentVersion_, info_.version),
        notifyWidget_);
    versionLabel_->setStyleSheet(kVersionStyle);
    versionLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(versionLabel_);

    // Release notes
    releaseNotes_ = new QTextBrowser(notifyWidget_);
    releaseNotes_->setStyleSheet(kReleaseNotesStyle);
    releaseNotes_->setMarkdown(info_.releaseNotes);
    releaseNotes_->setOpenExternalLinks(true);
    layout->addWidget(releaseNotes_, 1);

    // Buttons
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(10);

    laterBtn_ = new QPushButton(QStringLiteral("나중에"), notifyWidget_);
    laterBtn_->setStyleSheet(kLaterButtonStyle);
    laterBtn_->setFixedHeight(36);
    btnLayout->addWidget(laterBtn_);

    updateBtn_ = new QPushButton(QStringLiteral("업데이트"), notifyWidget_);
    updateBtn_->setStyleSheet(kUpdateButtonStyle);
    updateBtn_->setFixedHeight(36);
    btnLayout->addWidget(updateBtn_);

    layout->addLayout(btnLayout);

    connect(updateBtn_, &QPushButton::clicked, this, &UpdateDialog::updateAccepted);
    connect(laterBtn_, &QPushButton::clicked, this, [this]() {
        emit updateCancelled();
        reject();
    });

    mainLayout_->addWidget(notifyWidget_);
}

// ─── Progress mode UI ───────────────────────────────────────────────────────
void UpdateDialog::buildProgressUI()
{
    progressWidget_ = new QWidget(this);
    auto* layout = new QVBoxLayout(progressWidget_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // Stage label
    stageLabel_ = new QLabel(QStringLiteral("다운로드 중..."), progressWidget_);
    stageLabel_->setStyleSheet(kStageStyle);
    stageLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(stageLabel_);

    layout->addSpacing(8);

    // Progress bar
    progressBar_ = new QProgressBar(progressWidget_);
    progressBar_->setStyleSheet(kProgressBarStyle);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(true);
    layout->addWidget(progressBar_);

    // Speed & ETA row
    auto* infoLayout = new QHBoxLayout();
    speedLabel_ = new QLabel(QStringLiteral("0 B/s"), progressWidget_);
    speedLabel_->setStyleSheet(kSpeedEtaStyle);
    infoLayout->addWidget(speedLabel_);

    infoLayout->addStretch();

    etaLabel_ = new QLabel(QString(), progressWidget_);
    etaLabel_->setStyleSheet(kSpeedEtaStyle);
    infoLayout->addWidget(etaLabel_);

    layout->addLayout(infoLayout);

    layout->addStretch();

    // Cancel button
    cancelBtn_ = new QPushButton(QStringLiteral("취소"), progressWidget_);
    cancelBtn_->setStyleSheet(kCancelButtonStyle);
    cancelBtn_->setFixedHeight(34);
    connect(cancelBtn_, &QPushButton::clicked, this, [this]() {
        emit updateCancelled();
    });
    layout->addWidget(cancelBtn_, 0, Qt::AlignCenter);

    mainLayout_->addWidget(progressWidget_);
}

// ─── Mode switch ─────────────────────────────────────────────────────────────
void UpdateDialog::showProgress()
{
    notifyWidget_->setVisible(false);
    progressWidget_->setVisible(true);
    progressBar_->setValue(0);
    stageLabel_->setText(QStringLiteral("다운로드 중..."));
    cancelBtn_->setVisible(true);
}

// ─── Slots ───────────────────────────────────────────────────────────────────
void UpdateDialog::setProgress(qint64 received, qint64 total)
{
    if (total > 0) {
        int pct = static_cast<int>(received * 100 / total);
        progressBar_->setValue(pct);
        progressBar_->setFormat(QStringLiteral("%1 / %2  (%p%)")
                                    .arg(formatBytes(received), formatBytes(total)));
    } else {
        progressBar_->setRange(0, 0); // indeterminate
    }
}

void UpdateDialog::setSpeed(double bytesPerSec)
{
    speedLabel_->setText(formatSpeed(bytesPerSec));

    // Estimate remaining time
    if (bytesPerSec > 0 && progressBar_->maximum() > 0) {
        int pct = progressBar_->value();
        if (pct > 0 && pct < 100 && info_.fileSize > 0) {
            qint64 remaining = info_.fileSize - (info_.fileSize * pct / 100);
            double eta = static_cast<double>(remaining) / bytesPerSec;
            etaLabel_->setText(formatEta(eta));
        }
    }
}

void UpdateDialog::setStage(const QString& stage)
{
    stageLabel_->setText(stage);

    // Hide cancel button when not downloading
    if (stage != QStringLiteral("다운로드 중...")) {
        cancelBtn_->setVisible(false);
    }
}

void UpdateDialog::startCountdown()
{
    countdownSec_ = 3;
    cancelBtn_->setVisible(false);
    stageLabel_->setText(QStringLiteral("%1초 후 재시작...").arg(countdownSec_));

    countdownTimer_ = new QTimer(this);
    countdownTimer_->setInterval(1000);
    connect(countdownTimer_, &QTimer::timeout, this, [this]() {
        --countdownSec_;
        if (countdownSec_ <= 0) {
            countdownTimer_->stop();
            stageLabel_->setText(QStringLiteral("재시작 중..."));
            emit countdownFinished();
        } else {
            stageLabel_->setText(QStringLiteral("%1초 후 재시작...").arg(countdownSec_));
        }
    });
    countdownTimer_->start();
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
QString UpdateDialog::formatBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024 * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString UpdateDialog::formatSpeed(double bytesPerSec)
{
    if (bytesPerSec < 1024.0)
        return QStringLiteral("%1 B/s").arg(bytesPerSec, 0, 'f', 0);
    if (bytesPerSec < 1024.0 * 1024.0)
        return QStringLiteral("%1 KB/s").arg(bytesPerSec / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB/s").arg(bytesPerSec / (1024.0 * 1024.0), 0, 'f', 1);
}

QString UpdateDialog::formatEta(double seconds)
{
    if (seconds < 60.0)
        return QStringLiteral("약 %1초").arg(static_cast<int>(seconds));
    if (seconds < 3600.0)
        return QStringLiteral("약 %1분").arg(static_cast<int>(seconds / 60.0));
    return QStringLiteral("약 %1시간").arg(seconds / 3600.0, 0, 'f', 1);
}

}} // namespace occt::updater
