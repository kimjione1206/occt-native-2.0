#include "results_panel.h"
#include "panel_styles.h"
#include "report/report_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QDateTime>

namespace occt { namespace gui {

ResultsPanel::ResultsPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void ResultsPanel::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    // Title
    auto* title = new QLabel("테스트 결과", this);
    title->setStyleSheet("color: #F0F6FC; font-size: 20px; font-weight: bold; background: transparent;");
    mainLayout->addWidget(title);

    auto* subtitle = new QLabel("완료된 스트레스 테스트 기록", this);
    subtitle->setStyleSheet("color: #8B949E; font-size: 13px; background: transparent;");
    mainLayout->addWidget(subtitle);

    // Toolbar
    mainLayout->addWidget(createToolbar());

    // Results table
    resultsTable_ = new QTableWidget(this);
    resultsTable_->setAccessibleDescription("results_table");
    resultsTable_->setColumnCount(7);
    resultsTable_->setHorizontalHeaderLabels({
        "시간", "테스트 유형", "모드", "소요 시간", "점수", "오류", "결과"
    });
    resultsTable_->horizontalHeader()->setStretchLastSection(false);
    resultsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    resultsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    resultsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    resultsTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    resultsTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    resultsTable_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    resultsTable_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    resultsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    resultsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultsTable_->setAlternatingRowColors(true);
    resultsTable_->verticalHeader()->setVisible(false);

    connect(resultsTable_, &QTableWidget::cellClicked, this, &ResultsPanel::onResultSelected);
    mainLayout->addWidget(resultsTable_, 1);

    // Details section
    auto* detailsFrame = new QFrame(this);
    detailsFrame->setStyleSheet(
        styles::kSectionFrame
    );
    detailsFrame->setMinimumHeight(100);

    auto* detailsLayout = new QVBoxLayout(detailsFrame);
    detailsLayout->setContentsMargins(16, 12, 16, 12);

    auto* detailsTitle = new QLabel("상세 정보", detailsFrame);
    detailsTitle->setStyleSheet("color: #C9D1D9; font-size: 14px; font-weight: bold; border: none; background: transparent;");
    detailsLayout->addWidget(detailsTitle);

    detailsLabel_ = new QLabel("결과를 선택하여 상세 정보를 확인하세요", detailsFrame);
    detailsLabel_->setAccessibleDescription("results_details");
    detailsLabel_->setStyleSheet("color: #8B949E; font-size: 12px; border: none; background: transparent;");
    detailsLabel_->setWordWrap(true);
    detailsLayout->addWidget(detailsLabel_);

    mainLayout->addWidget(detailsFrame);

    // Summary
    summaryLabel_ = new QLabel("테스트 결과가 없습니다", this);
    summaryLabel_->setAccessibleDescription("results_summary");
    summaryLabel_->setStyleSheet("color: #8B949E; font-size: 12px; background: transparent;");
    mainLayout->addWidget(summaryLabel_);
}

QFrame* ResultsPanel::createToolbar()
{
    auto* frame = new QFrame(this);
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(16, 8, 16, 8);
    layout->setSpacing(8);

    auto makeBtn = [&](const QString& text, const QString& color) -> QPushButton* {
        auto* btn = new QPushButton(text, frame);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            QString("QPushButton { background-color: %1; color: white; border: none; "
                    "border-radius: 4px; padding: 8px 14px; font-weight: bold; font-size: 12px; }"
                    "QPushButton:hover { background-color: %1; opacity: 0.8; }").arg(color)
        );
        return btn;
    };

    exportHtmlBtn_ = makeBtn("HTML 내보내기", "#2980B9");
    connect(exportHtmlBtn_, &QPushButton::clicked, this, &ResultsPanel::onExportHtmlClicked);
    layout->addWidget(exportHtmlBtn_);

    exportPngBtn_ = makeBtn("PNG 내보내기", "#27AE60");
    connect(exportPngBtn_, &QPushButton::clicked, this, &ResultsPanel::onExportPngClicked);
    layout->addWidget(exportPngBtn_);

    exportCsvBtn_ = makeBtn("CSV 내보내기", "#8E44AD");
    connect(exportCsvBtn_, &QPushButton::clicked, this, &ResultsPanel::onExportCsvClicked);
    layout->addWidget(exportCsvBtn_);

    exportJsonBtn_ = makeBtn("JSON 내보내기", "#D35400");
    connect(exportJsonBtn_, &QPushButton::clicked, this, &ResultsPanel::onExportJsonClicked);
    layout->addWidget(exportJsonBtn_);

    layout->addSpacing(12);

    clearBtn_ = new QPushButton("모두 삭제", frame);
    clearBtn_->setCursor(Qt::PointingHandCursor);
    clearBtn_->setStyleSheet(
        "QPushButton { background-color: #C0392B; color: white; border: none; "
        "border-radius: 4px; padding: 8px 14px; font-weight: bold; font-size: 12px; }"
        "QPushButton:hover { background-color: #E74C3C; }"
    );
    connect(clearBtn_, &QPushButton::clicked, this, &ResultsPanel::onClearClicked);
    layout->addWidget(clearBtn_);

    layout->addStretch();

    return frame;
}

void ResultsPanel::addResult(const TestResult& result)
{
    results_.append(result);

    int row = resultsTable_->rowCount();
    resultsTable_->insertRow(row);

    resultsTable_->setItem(row, 0, new QTableWidgetItem(result.timestamp));
    resultsTable_->setItem(row, 1, new QTableWidgetItem(result.testType));
    resultsTable_->setItem(row, 2, new QTableWidgetItem(result.mode));
    resultsTable_->setItem(row, 3, new QTableWidgetItem(result.duration));
    resultsTable_->setItem(row, 4, new QTableWidgetItem(result.score));
    resultsTable_->setItem(row, 5, new QTableWidgetItem(QString::number(result.errorCount)));

    auto* resultItem = new QTableWidgetItem(result.passed ? "통과" : "실패");
    resultItem->setForeground(result.passed ? QColor(46, 204, 113) : QColor(231, 76, 60));
    QFont boldFont = resultItem->font();
    boldFont.setBold(true);
    resultItem->setFont(boldFont);
    resultsTable_->setItem(row, 6, resultItem);

    int passCount = 0;
    for (const auto& r : results_) {
        if (r.passed) passCount++;
    }
    summaryLabel_->setText(QString("총: %1 테스트 | 통과: %2 | 실패: %3")
        .arg(results_.size()).arg(passCount).arg(results_.size() - passCount));
}

void ResultsPanel::clearResults()
{
    results_.clear();
    resultsTable_->setRowCount(0);
    detailsLabel_->setText("결과를 선택하여 상세 정보를 확인하세요");
    summaryLabel_->setText("테스트 결과가 없습니다");
}

void ResultsPanel::doExport(const QString& format, const QString& filter, const QString& defaultName)
{
    if (results_.isEmpty()) {
        QMessageBox::information(this, "내보내기", "내보낼 결과가 없습니다.");
        return;
    }

    QString filename = QFileDialog::getSaveFileName(this, "보고서 내보내기", defaultName, filter);
    if (filename.isEmpty()) return;

    // Build TestResults from GUI results
    occt::TestResults testResults;
    for (const auto& r : results_) {
        occt::TestResultData rd;
        rd.timestamp = r.timestamp;
        rd.test_type = r.testType;
        rd.mode = r.mode;
        rd.duration = r.duration;
        rd.score = r.score;
        rd.passed = r.passed;
        rd.error_count = r.errorCount;
        rd.details = r.details;
        testResults.results.append(rd);
    }

    // Basic system info
    testResults.system_info.cpu_name = "N/A";
    testResults.system_info.gpu_name = "N/A";
    testResults.system_info.os_name = "N/A";
    testResults.system_info.ram_total = "N/A";

    int passCount = 0;
    for (const auto& r : results_) { if (r.passed) passCount++; }
    testResults.overall_verdict = (passCount == results_.size() && passCount > 0) ? "PASS" : "FAIL";

    occt::ReportManager mgr;
    bool ok = false;
    if (format == "html") ok = mgr.save_html(testResults, filename);
    else if (format == "png") ok = mgr.save_png(testResults, filename);
    else if (format == "csv") ok = mgr.save_csv(testResults, filename);
    else if (format == "json") ok = mgr.save_json(testResults, filename);

    if (ok) {
        QMessageBox::information(this, "내보내기", "보고서가 성공적으로 내보내졌습니다.");
    } else {
        QMessageBox::warning(this, "오류", "보고서 내보내기에 실패했습니다.");
    }
}

void ResultsPanel::onExportHtmlClicked()
{
    doExport("html", "HTML Files (*.html)", "occt_report.html");
}

void ResultsPanel::onExportPngClicked()
{
    doExport("png", "PNG Images (*.png)", "occt_report.png");
}

void ResultsPanel::onExportCsvClicked()
{
    doExport("csv", "CSV Files (*.csv)", "occt_report.csv");
}

void ResultsPanel::onExportJsonClicked()
{
    doExport("json", "JSON Files (*.json)", "occt_report.json");
}

void ResultsPanel::onClearClicked()
{
    auto reply = QMessageBox::question(this, "결과 삭제",
        "모든 테스트 결과를 삭제하시겠습니까?",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        clearResults();
    }
}

void ResultsPanel::onResultSelected(int row, int /*column*/)
{
    if (row >= 0 && row < results_.size()) {
        detailsLabel_->setText(results_[row].details);
    }
}

}} // namespace occt::gui
