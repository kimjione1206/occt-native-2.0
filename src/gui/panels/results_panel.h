#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QFrame>

namespace occt { namespace gui {

struct TestResult {
    QString timestamp;
    QString testType;
    QString mode;
    QString duration;
    QString score;
    bool passed;
    QString details;
    int errorCount = 0;
};

class ResultsPanel : public QWidget {
    Q_OBJECT

public:
    explicit ResultsPanel(QWidget* parent = nullptr);

    void addResult(const TestResult& result);
    void clearResults();

public slots:
    void onExportHtmlClicked();
    void onExportPngClicked();
    void onExportCsvClicked();
    void onExportJsonClicked();

private slots:
    void onClearClicked();
    void onResultSelected(int row, int column);

private:
    void setupUi();
    QFrame* createToolbar();
    void doExport(const QString& format, const QString& filter, const QString& defaultName);

    QTableWidget* resultsTable_ = nullptr;
    QLabel* detailsLabel_ = nullptr;
    QPushButton* exportHtmlBtn_ = nullptr;
    QPushButton* exportPngBtn_ = nullptr;
    QPushButton* exportCsvBtn_ = nullptr;
    QPushButton* exportJsonBtn_ = nullptr;
    QPushButton* clearBtn_ = nullptr;
    QLabel* summaryLabel_ = nullptr;

    QVector<TestResult> results_;
};

}} // namespace occt::gui
