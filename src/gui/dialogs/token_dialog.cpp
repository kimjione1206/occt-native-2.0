#include "token_dialog.h"

#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVBoxLayout>

#include "utils/secure_token_store.h"

namespace occt { namespace gui {

// ─── Construction ───────────────────────────────────────────────────────────
TokenDialog::TokenDialog(QWidget* parent)
    : QDialog(parent)
    , nam_(new QNetworkAccessManager(this))
{
    setWindowTitle(QStringLiteral("GitHub 토큰 설정"));
    setFixedSize(500, 350);

    // ── 다크 테마 스타일시트 ──
    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #0D1117; }"
        "QLabel { color: #C9D1D9; font-size: 13px; }"
        "QLineEdit {"
        "  background-color: #161B22; color: #C9D1D9; border: 1px solid #30363D;"
        "  border-radius: 6px; padding: 8px; font-size: 14px;"
        "}"
        "QLineEdit:focus { border-color: #58A6FF; }"
        "QPushButton {"
        "  color: #FFFFFF; border: none; border-radius: 6px;"
        "  padding: 8px 16px; font-size: 13px; font-weight: bold;"
        "}"
        "QPushButton:hover { opacity: 0.9; }"
    ));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(24, 20, 24, 20);

    // ── 제목 ──
    auto* titleLabel = new QLabel(QStringLiteral("GitHub 토큰 설정"), this);
    titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 18px; font-weight: bold; color: #F0F6FC;"));
    mainLayout->addWidget(titleLabel);

    // ── 설명 ──
    descLabel_ = new QLabel(
        QStringLiteral("테스트 로그를 GitHub Gist로 전송하기 위한\n"
                       "Personal Access Token을 입력하세요.\n"
                       "(gist 권한만 필요합니다)"),
        this);
    descLabel_->setStyleSheet(QStringLiteral("color: #8B949E; font-size: 12px;"));
    mainLayout->addWidget(descLabel_);

    // ── 토큰 입력 ──
    tokenEdit_ = new QLineEdit(this);
    tokenEdit_->setEchoMode(QLineEdit::Password);
    tokenEdit_->setPlaceholderText(QStringLiteral("ghp_..."));
    mainLayout->addWidget(tokenEdit_);

    // ── 상태 라벨 ──
    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet(QStringLiteral("font-size: 12px;"));
    mainLayout->addWidget(statusLabel_);

    mainLayout->addStretch();

    // ── 버튼 영역 ──
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(10);

    validateBtn_ = new QPushButton(QStringLiteral("검증"), this);
    validateBtn_->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #58A6FF; }"
        "QPushButton:hover { background-color: #79B8FF; }"));

    saveBtn_ = new QPushButton(QStringLiteral("저장"), this);
    saveBtn_->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #238636; }"
        "QPushButton:hover { background-color: #2EA043; }"));

    deleteBtn_ = new QPushButton(QStringLiteral("삭제"), this);
    deleteBtn_->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #DA3633; }"
        "QPushButton:hover { background-color: #F85149; }"));

    btnLayout->addStretch();
    btnLayout->addWidget(validateBtn_);
    btnLayout->addWidget(saveBtn_);
    btnLayout->addWidget(deleteBtn_);
    mainLayout->addLayout(btnLayout);

    // ── 시그널 연결 ──
    connect(validateBtn_, &QPushButton::clicked, this, &TokenDialog::onValidateClicked);
    connect(saveBtn_, &QPushButton::clicked, this, &TokenDialog::onSaveClicked);
    connect(deleteBtn_, &QPushButton::clicked, this, &TokenDialog::onDeleteClicked);

    updateStatus();
}

// ─── Status update ──────────────────────────────────────────────────────────
void TokenDialog::updateStatus()
{
    auto& store = utils::SecureTokenStore::instance();
    if (store.hasToken()) {
        statusLabel_->setText(
            QStringLiteral("현재 토큰: %1 (저장됨)").arg(store.maskedToken()));
        statusLabel_->setStyleSheet(QStringLiteral("color: #3FB950; font-size: 12px;"));
    } else {
        statusLabel_->setText(QStringLiteral("토큰 없음"));
        statusLabel_->setStyleSheet(QStringLiteral("color: #8B949E; font-size: 12px;"));
    }
}

// ─── Save ───────────────────────────────────────────────────────────────────
void TokenDialog::onSaveClicked()
{
    const QString token = tokenEdit_->text().trimmed();
    if (token.isEmpty()) {
        statusLabel_->setText(QStringLiteral("토큰을 입력하세요."));
        statusLabel_->setStyleSheet(QStringLiteral("color: #F0883E; font-size: 12px;"));
        return;
    }

    if (utils::SecureTokenStore::instance().storeToken(token)) {
        statusLabel_->setText(QStringLiteral("토큰이 안전하게 저장되었습니다."));
        statusLabel_->setStyleSheet(QStringLiteral("color: #3FB950; font-size: 12px;"));
        tokenEdit_->clear();
        updateStatus();
    } else {
        statusLabel_->setText(QStringLiteral("토큰 저장에 실패했습니다."));
        statusLabel_->setStyleSheet(QStringLiteral("color: #DA3633; font-size: 12px;"));
    }
}

// ─── Delete ─────────────────────────────────────────────────────────────────
void TokenDialog::onDeleteClicked()
{
    auto reply = QMessageBox::question(
        this,
        QStringLiteral("토큰 삭제"),
        QStringLiteral("저장된 토큰을 삭제하시겠습니까?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        utils::SecureTokenStore::instance().deleteToken();
        tokenEdit_->clear();
        updateStatus();
    }
}

// ─── Validate ───────────────────────────────────────────────────────────────
void TokenDialog::onValidateClicked()
{
    // 입력 필드에 토큰이 있으면 그것을 검증, 없으면 저장된 토큰 검증
    QString token = tokenEdit_->text().trimmed();
    if (token.isEmpty())
        token = utils::SecureTokenStore::instance().retrieveToken();

    if (token.isEmpty()) {
        statusLabel_->setText(QStringLiteral("검증할 토큰이 없습니다."));
        statusLabel_->setStyleSheet(QStringLiteral("color: #F0883E; font-size: 12px;"));
        return;
    }

    statusLabel_->setText(QStringLiteral("검증 중..."));
    statusLabel_->setStyleSheet(QStringLiteral("color: #58A6FF; font-size: 12px;"));
    validateBtn_->setEnabled(false);

    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/user")));
    req.setRawHeader("Authorization",
                     QStringLiteral("Bearer %1").arg(token).toUtf8());
    req.setRawHeader("Accept", "application/vnd.github+json");

    QNetworkReply* reply = nam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        validateBtn_->setEnabled(true);

        if (reply->error() == QNetworkReply::NoError) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            const QString login = doc.object().value(QStringLiteral("login")).toString();
            statusLabel_->setText(
                QStringLiteral("유효한 토큰 (user: %1)").arg(login));
            statusLabel_->setStyleSheet(
                QStringLiteral("color: #3FB950; font-size: 12px;"));
        } else {
            const int httpStatus = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (httpStatus == 401) {
                statusLabel_->setText(QStringLiteral("유효하지 않은 토큰"));
                statusLabel_->setStyleSheet(
                    QStringLiteral("color: #DA3633; font-size: 12px;"));
            } else {
                statusLabel_->setText(
                    QStringLiteral("연결 실패: %1").arg(reply->errorString()));
                statusLabel_->setStyleSheet(
                    QStringLiteral("color: #F0883E; font-size: 12px;"));
            }
        }
    });
}

}} // namespace occt::gui
