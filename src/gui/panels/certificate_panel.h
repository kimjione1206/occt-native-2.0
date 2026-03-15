#pragma once

#include <QWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QFrame>
#include <QTextBrowser>

namespace occt {
class TestScheduler;
}

namespace occt { namespace gui {

class CertificatePanel : public QWidget {
    Q_OBJECT

public:
    explicit CertificatePanel(QWidget* parent = nullptr);

private slots:
    void onTierSelected(int tier);
    void onStartCertification();
    void onStepStarted(int index, const QString& engine);
    void onStepCompleted(int index, bool passed, int errors);
    void onScheduleCompleted(bool all_passed, int total_errors);
    void onProgressChanged(double pct);
    void onSaveHtml();
    void onSavePng();

private:
    void setupUi();
    QFrame* createTierSection();
    QFrame* createProgressSection();
    QFrame* createPreviewSection();
    void updateTierInfo();

    // Tier buttons
    QPushButton* bronzeBtn_ = nullptr;
    QPushButton* silverBtn_ = nullptr;
    QPushButton* goldBtn_ = nullptr;
    QPushButton* platinumBtn_ = nullptr;
    QLabel* tierInfoLabel_ = nullptr;
    int selectedTier_ = 0; // 0=Bronze, 1=Silver, 2=Gold, 3=Platinum

    // Progress
    QProgressBar* certProgress_ = nullptr;
    QLabel* currentStepLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* startBtn_ = nullptr;

    // Preview / Save
    QTextBrowser* previewBrowser_ = nullptr;
    QPushButton* saveHtmlBtn_ = nullptr;
    QPushButton* savePngBtn_ = nullptr;
    QFrame* saveFrame_ = nullptr;

    // State
    TestScheduler* scheduler_ = nullptr;
    bool isRunning_ = false;
    QString lastHtml_;
};

}} // namespace occt::gui
