#include "certificate_panel.h"
#include "panel_styles.h"

#include "scheduler/test_scheduler.h"
#include "scheduler/preset_schedules.h"
#include "certification/certificate.h"
#include "certification/cert_generator.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QJsonDocument>

namespace occt { namespace gui {

CertificatePanel::CertificatePanel(QWidget* parent)
    : QWidget(parent)
{
    scheduler_ = new TestScheduler(this);
    connect(scheduler_, &TestScheduler::stepStarted, this, &CertificatePanel::onStepStarted);
    connect(scheduler_, &TestScheduler::stepCompleted, this, &CertificatePanel::onStepCompleted);
    connect(scheduler_, &TestScheduler::scheduleCompleted, this, &CertificatePanel::onScheduleCompleted);
    connect(scheduler_, &TestScheduler::progressChanged, this, &CertificatePanel::onProgressChanged);

    setupUi();
    updateTierInfo();
}

void CertificatePanel::setupUi()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    // Left column: tier selection + progress
    auto* leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(16);
    leftLayout->addWidget(createTierSection());
    leftLayout->addWidget(createProgressSection(), 1);

    auto* leftWidget = new QWidget();
    leftWidget->setFixedWidth(380);
    leftWidget->setLayout(leftLayout);
    mainLayout->addWidget(leftWidget);

    // Right column: preview
    mainLayout->addWidget(createPreviewSection(), 1);
}

QFrame* CertificatePanel::createTierSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto* title = new QLabel("안정성 인증서", frame);
    title->setStyleSheet(styles::kPanelTitle);
    layout->addWidget(title);

    auto* subtitle = new QLabel("시스템 안정성을 검증하기 위한 인증 테스트 실행", frame);
    subtitle->setStyleSheet(styles::kPanelSubtitle);
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    layout->addSpacing(8);

    auto* tierLabel = new QLabel("등급 선택", frame);
    tierLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(tierLabel);

    auto createTierBtn = [frame](const QString& text, const QString& color) -> QPushButton* {
        auto* btn = new QPushButton(text, frame);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(42);
        btn->setCheckable(true);
        btn->setStyleSheet(
            QString(
                "QPushButton { background-color: #0D1117; color: %1; border: 2px solid #30363D; "
                "border-radius: 6px; font-size: 14px; font-weight: bold; }"
                "QPushButton:hover { border-color: %1; }"
                "QPushButton:checked { border-color: %1; background-color: #1F2937; }"
            ).arg(color)
        );
        return btn;
    };

    bronzeBtn_   = createTierBtn("브론즈  (~1시간)", "#CD7F32");
    silverBtn_   = createTierBtn("실버  (~3시간)", "#C0C0C0");
    goldBtn_     = createTierBtn("골드  (~6시간)", "#FFD700");
    platinumBtn_ = createTierBtn("플래티넘  (~12시간)", "#E5E4E2");

    bronzeBtn_->setChecked(true);

    layout->addWidget(bronzeBtn_);
    layout->addWidget(silverBtn_);
    layout->addWidget(goldBtn_);
    layout->addWidget(platinumBtn_);

    connect(bronzeBtn_, &QPushButton::clicked, this, [this]() { onTierSelected(0); });
    connect(silverBtn_, &QPushButton::clicked, this, [this]() { onTierSelected(1); });
    connect(goldBtn_, &QPushButton::clicked, this, [this]() { onTierSelected(2); });
    connect(platinumBtn_, &QPushButton::clicked, this, [this]() { onTierSelected(3); });

    tierInfoLabel_ = new QLabel("", frame);
    tierInfoLabel_->setStyleSheet(styles::kSmallInfo);
    tierInfoLabel_->setWordWrap(true);
    layout->addWidget(tierInfoLabel_);

    return frame;
}

QFrame* CertificatePanel::createProgressSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto* title = new QLabel("인증 진행 상황", frame);
    title->setStyleSheet(styles::kSectionTitle);
    layout->addWidget(title);

    certProgress_ = new QProgressBar(frame);
    certProgress_->setAccessibleDescription("cert_progress");
    certProgress_->setRange(0, 100);
    certProgress_->setValue(0);
    certProgress_->setTextVisible(true);
    certProgress_->setStyleSheet(
        "QProgressBar { background-color: #0D1117; border: 1px solid #30363D; border-radius: 6px; "
        "height: 24px; text-align: center; color: #F0F6FC; font-weight: bold; }"
        "QProgressBar::chunk { background-color: #C0392B; border-radius: 5px; }"
    );
    layout->addWidget(certProgress_);

    currentStepLabel_ = new QLabel("시작 전", frame);
    currentStepLabel_->setAccessibleDescription("cert_current_step");
    currentStepLabel_->setStyleSheet("color: #8B949E; font-size: 13px; border: none; background: transparent;");
    layout->addWidget(currentStepLabel_);

    statusLabel_ = new QLabel("등급을 선택하고 인증 시작을 클릭하세요", frame);
    statusLabel_->setAccessibleDescription("cert_status");
    statusLabel_->setStyleSheet(
        "color: #C9D1D9; font-size: 12px; border: none; background-color: #0D1117; "
        "padding: 12px; border-radius: 6px;"
    );
    statusLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_, 1);

    startBtn_ = new QPushButton("인증 시작", frame);
    startBtn_->setCursor(Qt::PointingHandCursor);
    startBtn_->setFixedHeight(48);
    startBtn_->setStyleSheet(
        styles::kStartButton
    );
    connect(startBtn_, &QPushButton::clicked, this, &CertificatePanel::onStartCertification);
    layout->addWidget(startBtn_);

    return frame;
}

QFrame* CertificatePanel::createPreviewSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto* title = new QLabel("인증서 미리보기", frame);
    title->setStyleSheet(styles::kSectionTitle);
    layout->addWidget(title);

    previewBrowser_ = new QTextBrowser(frame);
    previewBrowser_->setStyleSheet(
        "QTextBrowser { background-color: #0D1117; border: 1px solid #30363D; border-radius: 6px; "
        "color: #C9D1D9; padding: 12px; }"
    );
    previewBrowser_->setHtml(
        "<div style='text-align:center; padding:60px; color:#484F58;'>"
        "<h2>인증서 없음</h2>"
        "<p>인증 테스트를 완료하면 여기에 인증서가 표시됩니다.</p></div>"
    );
    layout->addWidget(previewBrowser_, 1);

    // Save buttons (hidden until cert complete)
    saveFrame_ = new QFrame(frame);
    saveFrame_->setStyleSheet("border: none; background: transparent;");
    saveFrame_->setVisible(false);
    auto* saveLayout = new QHBoxLayout(saveFrame_);
    saveLayout->setContentsMargins(0, 0, 0, 0);
    saveLayout->setSpacing(10);

    saveHtmlBtn_ = new QPushButton("HTML 저장", saveFrame_);
    savePngBtn_ = new QPushButton("PNG 저장", saveFrame_);

    auto styleBtn = [](QPushButton* btn) {
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(36);
        btn->setStyleSheet(
            "QPushButton { background-color: #21262D; color: #C9D1D9; border: 1px solid #30363D; "
            "border-radius: 4px; padding: 0 16px; font-size: 13px; }"
            "QPushButton:hover { background-color: #30363D; }"
        );
    };
    styleBtn(saveHtmlBtn_);
    styleBtn(savePngBtn_);

    saveLayout->addWidget(saveHtmlBtn_);
    saveLayout->addWidget(savePngBtn_);
    saveLayout->addStretch();
    layout->addWidget(saveFrame_);

    connect(saveHtmlBtn_, &QPushButton::clicked, this, &CertificatePanel::onSaveHtml);
    connect(savePngBtn_, &QPushButton::clicked, this, &CertificatePanel::onSavePng);

    return frame;
}

void CertificatePanel::onTierSelected(int tier)
{
    if (isRunning_) return;
    selectedTier_ = tier;

    bronzeBtn_->setChecked(tier == 0);
    silverBtn_->setChecked(tier == 1);
    goldBtn_->setChecked(tier == 2);
    platinumBtn_->setChecked(tier == 3);

    updateTierInfo();
}

void CertificatePanel::updateTierInfo()
{
    struct TierInfo { QString name; QString tests; QString duration; };
    static const TierInfo infos[] = {
        {"Bronze", "CPU SSE 30min + RAM 30min", "~1 hour"},
        {"Silver", "CPU AVX2 60min + GPU 60min + RAM 60min", "~3 hours"},
        {"Gold", "CPU Linpack 120min + GPU VRAM 60min + RAM 120min + Storage 60min", "~6 hours"},
        {"Platinum", "CPU AVX2 300min + RAM 300min + GPU 120min", "~12 hours"}
    };

    const auto& info = infos[selectedTier_];
    tierInfoLabel_->setText(
        QString("소요 시간: %1\n테스트: %2").arg(info.duration, info.tests));
}

void CertificatePanel::onStartCertification()
{
    if (isRunning_) {
        scheduler_->stop();
        isRunning_ = false;
        startBtn_->setText("인증 시작");
        startBtn_->setStyleSheet(
            styles::kStartButton
        );
        currentStepLabel_->setText("인증 중지됨");
        return;
    }

    QVector<TestStep> steps;
    switch (selectedTier_) {
        case 0: steps = preset_cert_bronze(); break;
        case 1: steps = preset_cert_silver(); break;
        case 2: steps = preset_cert_gold(); break;
        case 3: steps = preset_cert_platinum(); break;
    }

    scheduler_->load_schedule(steps);
    scheduler_->set_stop_on_error(true);
    isRunning_ = true;
    certProgress_->setValue(0);
    saveFrame_->setVisible(false);
    statusLabel_->setText("인증 시작 중...\n");

    startBtn_->setText("인증 중지");
    startBtn_->setStyleSheet(
        styles::kStopButton
    );

    scheduler_->start();
}

void CertificatePanel::onStepStarted(int index, const QString& engine)
{
    currentStepLabel_->setText(
        QString("단계 %1/%2: %3")
            .arg(index + 1)
            .arg(scheduler_->steps().size())
            .arg(engine.toUpper()));

    statusLabel_->setText(statusLabel_->text() +
        QString("단계 %1: %2 시작\n").arg(index + 1).arg(engine.toUpper()));
}

void CertificatePanel::onStepCompleted(int index, bool passed, int errors)
{
    QString result = passed ? "통과" : "실패";
    statusLabel_->setText(statusLabel_->text() +
        QString("단계 %1: %2 (오류: %3)\n").arg(index + 1).arg(result).arg(errors));
}

void CertificatePanel::onScheduleCompleted(bool all_passed, int total_errors)
{
    isRunning_ = false;
    certProgress_->setValue(100);

    startBtn_->setText("인증 시작");
    startBtn_->setStyleSheet(
        styles::kStartButton
    );

    // Build certificate
    CertTier tier = static_cast<CertTier>(selectedTier_);
    Certificate cert;
    cert.tier = tier;
    cert.passed = all_passed;
    cert.issued_at = QDateTime::currentDateTime();
    cert.expires_at = cert.issued_at.addDays(90);
    cert.system_info_json = CertGenerator::collect_system_info();

    // Convert scheduler results to cert results
    const auto& schedResults = scheduler_->results();
    for (const auto& sr : schedResults) {
        TestResult tr;
        tr.engine = sr.engine;
        tr.passed = sr.passed;
        tr.errors = sr.errors;
        tr.duration_secs = sr.duration_secs;

        // Find mode from step settings
        if (sr.index >= 0 && sr.index < scheduler_->steps().size()) {
            tr.mode = scheduler_->steps()[sr.index].settings.value("mode", "default").toString();
        }
        cert.results.append(tr);
    }

    // Compute hash
    CertGenerator gen;
    QJsonObject resultsJson = gen.generate_json(cert);
    cert.hash_sha256 = CertGenerator::compute_hash(cert.system_info_json, resultsJson);

    // Generate HTML and show preview
    lastHtml_ = gen.generate_html(cert);
    previewBrowser_->setHtml(lastHtml_);
    saveFrame_->setVisible(true);

    QString summary = all_passed ? "통과" : "실패";
    currentStepLabel_->setText(
        QString("인증 완료: %1 (%2 등급)")
            .arg(summary, cert_tier_name(tier)));

    statusLabel_->setText(statusLabel_->text() +
        QString("\n--- 인증 완료 ---\n등급: %1\n결과: %2\n총 오류: %3\n해시: %4\n")
            .arg(cert_tier_name(tier), summary)
            .arg(total_errors)
            .arg(cert.hash_sha256));
}

void CertificatePanel::onProgressChanged(double pct)
{
    certProgress_->setValue(static_cast<int>(pct));
}

void CertificatePanel::onSaveHtml()
{
    if (lastHtml_.isEmpty()) return;
    QString path = QFileDialog::getSaveFileName(this, "인증서 HTML 저장", "", "HTML (*.html)");
    if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (file.write(lastHtml_.toUtf8()) == -1) {
                qWarning() << "Failed to write certificate HTML to" << path
                           << ":" << file.errorString();
            }
        } else {
            qWarning() << "Failed to open file for writing:" << path
                       << ":" << file.errorString();
        }
    }
}

void CertificatePanel::onSavePng()
{
    // Rebuild certificate from last run for PNG generation
    CertTier tier = static_cast<CertTier>(selectedTier_);
    Certificate cert;
    cert.tier = tier;
    cert.issued_at = QDateTime::currentDateTime();
    cert.expires_at = cert.issued_at.addDays(90);
    cert.system_info_json = CertGenerator::collect_system_info();

    const auto& schedResults = scheduler_->results();
    cert.passed = true;
    for (const auto& sr : schedResults) {
        TestResult tr;
        tr.engine = sr.engine;
        tr.passed = sr.passed;
        tr.errors = sr.errors;
        tr.duration_secs = sr.duration_secs;
        if (sr.index >= 0 && sr.index < scheduler_->steps().size()) {
            tr.mode = scheduler_->steps()[sr.index].settings.value("mode", "default").toString();
        }
        cert.results.append(tr);
        if (!sr.passed) cert.passed = false;
    }

    CertGenerator gen;
    QJsonObject resultsJson = gen.generate_json(cert);
    cert.hash_sha256 = CertGenerator::compute_hash(cert.system_info_json, resultsJson);

    QString path = QFileDialog::getSaveFileName(this, "인증서 PNG 저장", "", "PNG (*.png)");
    if (!path.isEmpty()) {
        QImage img = gen.generate_image(cert);
        img.save(path, "PNG");
    }
}

}} // namespace occt::gui
