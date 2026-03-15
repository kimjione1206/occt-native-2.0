#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QProgressBar>
#include <QThread>

namespace occt {
class CacheBenchmark;
class MemoryBenchmark;
struct CacheLatencyResult;
struct MemoryBenchmarkResult;
}

namespace occt { namespace gui {

class BenchmarkPanel : public QWidget {
    Q_OBJECT

public:
    explicit BenchmarkPanel(QWidget* parent = nullptr);

signals:
    void benchmarkStartRequested();

private slots:
    void onRunClicked();

private:
    void setupUi();
    QFrame* createControlSection();
    QFrame* createLatencySection();
    QFrame* createBandwidthSection();
    QFrame* createMemorySection();
    QWidget* createBarWidget(const QString& label, double value, double maxValue,
                             const QColor& color, const QString& unit);
    void clearResults();

    QPushButton* runBtn_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    // Cache latency bars
    QFrame* latencyFrame_ = nullptr;
    QLabel* l1LatLabel_ = nullptr;
    QLabel* l2LatLabel_ = nullptr;
    QLabel* l3LatLabel_ = nullptr;
    QLabel* dramLatLabel_ = nullptr;
    QProgressBar* l1LatBar_ = nullptr;
    QProgressBar* l2LatBar_ = nullptr;
    QProgressBar* l3LatBar_ = nullptr;
    QProgressBar* dramLatBar_ = nullptr;

    // Bandwidth bars
    QFrame* bwFrame_ = nullptr;
    QLabel* l1BwLabel_ = nullptr;
    QLabel* l2BwLabel_ = nullptr;
    QLabel* l3BwLabel_ = nullptr;
    QLabel* dramBwLabel_ = nullptr;
    QProgressBar* l1BwBar_ = nullptr;
    QProgressBar* l2BwBar_ = nullptr;
    QProgressBar* l3BwBar_ = nullptr;
    QProgressBar* dramBwBar_ = nullptr;

    // Memory results
    QFrame* memFrame_ = nullptr;
    QLabel* memReadLabel_ = nullptr;
    QLabel* memWriteLabel_ = nullptr;
    QLabel* memCopyLabel_ = nullptr;
    QLabel* memLatencyLabel_ = nullptr;
    QLabel* memChannelsLabel_ = nullptr;

    bool isRunning_ = false;

    void onBenchmarkFinished(const CacheLatencyResult& cacheRes,
                             const MemoryBenchmarkResult& memRes);
};

}} // namespace occt::gui
