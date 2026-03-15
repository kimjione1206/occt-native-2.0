#include "benchmark_panel.h"
#include "panel_styles.h"
#include "../../engines/benchmark/cache_benchmark.h"
#include "../../engines/benchmark/memory_benchmark.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QTimer>
#include <QThread>
#include <QtConcurrent/QtConcurrent>

namespace occt { namespace gui {

BenchmarkPanel::BenchmarkPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void BenchmarkPanel::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    mainLayout->addWidget(createControlSection());

    // Scrollable results area
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    auto* resultsWidget = new QWidget(scroll);
    auto* resultsLayout = new QVBoxLayout(resultsWidget);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    resultsLayout->setSpacing(16);

    resultsLayout->addWidget(createLatencySection());
    resultsLayout->addWidget(createBandwidthSection());
    resultsLayout->addWidget(createMemorySection());
    resultsLayout->addStretch();

    scroll->setWidget(resultsWidget);
    mainLayout->addWidget(scroll, 1);
}

QFrame* BenchmarkPanel::createControlSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(16);

    auto* titleLayout = new QVBoxLayout();
    auto* title = new QLabel("캐시 및 메모리 벤치마크", frame);
    title->setStyleSheet(styles::kPanelTitle);
    titleLayout->addWidget(title);

    statusLabel_ = new QLabel("벤치마크 실행 준비", frame);
    statusLabel_->setAccessibleDescription("bench_status");
    statusLabel_->setStyleSheet(styles::kPanelSubtitle);
    titleLayout->addWidget(statusLabel_);

    layout->addLayout(titleLayout, 1);

    runBtn_ = new QPushButton("벤치마크 실행", frame);
    runBtn_->setCursor(Qt::PointingHandCursor);
    runBtn_->setFixedSize(180, 44);
    runBtn_->setStyleSheet(
        "QPushButton { background-color: #8E44AD; color: white; border: none; "
        "border-radius: 6px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background-color: #9B59B6; }"
    );
    connect(runBtn_, &QPushButton::clicked, this, &BenchmarkPanel::onRunClicked);
    layout->addWidget(runBtn_);

    return frame;
}

QFrame* BenchmarkPanel::createLatencySection()
{
    latencyFrame_ = new QFrame();
    latencyFrame_->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(latencyFrame_);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    auto* title = new QLabel("캐시 지연시간", latencyFrame_);
    title->setStyleSheet(styles::kSectionTitle);
    layout->addWidget(title);

    auto createLatRow = [this, layout](const QString& label, QLabel*& valLabel, QProgressBar*& bar) {
        auto* row = new QFrame(latencyFrame_);
        row->setStyleSheet("QFrame { border: none; background: transparent; }");
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(10);

        auto* lbl = new QLabel(label, row);
        lbl->setFixedWidth(50);
        lbl->setStyleSheet("color: #C9D1D9; font-weight: bold; font-size: 13px; border: none; background: transparent;");
        rl->addWidget(lbl);

        bar = new QProgressBar(row);
        bar->setRange(0, 1000);
        bar->setValue(0);
        bar->setTextVisible(false);
        bar->setFixedHeight(24);
        bar->setStyleSheet(
            "QProgressBar { background-color: #0D1117; border: 1px solid #30363D; border-radius: 4px; }"
            "QProgressBar::chunk { background-color: #3498DB; border-radius: 3px; }"
        );
        rl->addWidget(bar, 1);

        valLabel = new QLabel("--", row);
        valLabel->setFixedWidth(80);
        valLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valLabel->setStyleSheet("color: #F0F6FC; font-size: 13px; font-weight: bold; border: none; background: transparent;");
        rl->addWidget(valLabel);

        layout->addWidget(row);
    };

    createLatRow("L1", l1LatLabel_, l1LatBar_);
    l1LatLabel_->setAccessibleDescription("bench_l1_latency");
    createLatRow("L2", l2LatLabel_, l2LatBar_);
    l2LatLabel_->setAccessibleDescription("bench_l2_latency");
    createLatRow("L3", l3LatLabel_, l3LatBar_);
    l3LatLabel_->setAccessibleDescription("bench_l3_latency");
    createLatRow("DRAM", dramLatLabel_, dramLatBar_);
    dramLatLabel_->setAccessibleDescription("bench_dram_latency");

    return latencyFrame_;
}

QFrame* BenchmarkPanel::createBandwidthSection()
{
    bwFrame_ = new QFrame();
    bwFrame_->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(bwFrame_);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    auto* title = new QLabel("캐시 대역폭 (읽기)", bwFrame_);
    title->setStyleSheet(styles::kSectionTitle);
    layout->addWidget(title);

    auto createBwRow = [this, layout](const QString& label, QLabel*& valLabel, QProgressBar*& bar) {
        auto* row = new QFrame(bwFrame_);
        row->setStyleSheet("QFrame { border: none; background: transparent; }");
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(10);

        auto* lbl = new QLabel(label, row);
        lbl->setFixedWidth(50);
        lbl->setStyleSheet("color: #C9D1D9; font-weight: bold; font-size: 13px; border: none; background: transparent;");
        rl->addWidget(lbl);

        bar = new QProgressBar(row);
        bar->setRange(0, 1000);
        bar->setValue(0);
        bar->setTextVisible(false);
        bar->setFixedHeight(24);
        bar->setStyleSheet(
            "QProgressBar { background-color: #0D1117; border: 1px solid #30363D; border-radius: 4px; }"
            "QProgressBar::chunk { background-color: #2ECC71; border-radius: 3px; }"
        );
        rl->addWidget(bar, 1);

        valLabel = new QLabel("--", row);
        valLabel->setFixedWidth(100);
        valLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valLabel->setStyleSheet("color: #F0F6FC; font-size: 13px; font-weight: bold; border: none; background: transparent;");
        rl->addWidget(valLabel);

        layout->addWidget(row);
    };

    createBwRow("L1", l1BwLabel_, l1BwBar_);
    l1BwLabel_->setAccessibleDescription("bench_l1_bandwidth");
    createBwRow("L2", l2BwLabel_, l2BwBar_);
    l2BwLabel_->setAccessibleDescription("bench_l2_bandwidth");
    createBwRow("L3", l3BwLabel_, l3BwBar_);
    l3BwLabel_->setAccessibleDescription("bench_l3_bandwidth");
    createBwRow("DRAM", dramBwLabel_, dramBwBar_);
    dramBwLabel_->setAccessibleDescription("bench_dram_bandwidth");

    return bwFrame_;
}

QFrame* BenchmarkPanel::createMemorySection()
{
    memFrame_ = new QFrame();
    memFrame_->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(memFrame_);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    auto* title = new QLabel("메모리 벤치마크", memFrame_);
    title->setStyleSheet(styles::kSectionTitle);
    layout->addWidget(title);

    auto createResultRow = [this, layout](const QString& label) -> QLabel* {
        auto* row = new QFrame(memFrame_);
        row->setStyleSheet(styles::kCardFrame);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(12, 8, 12, 8);

        auto* lbl = new QLabel(label, row);
        lbl->setStyleSheet(styles::kPanelSubtitle);
        rl->addWidget(lbl);
        rl->addStretch();

        auto* val = new QLabel("--", row);
        val->setStyleSheet(styles::kSectionTitle);
        rl->addWidget(val);

        layout->addWidget(row);
        return val;
    };

    memReadLabel_ = createResultRow("읽기 대역폭");
    memReadLabel_->setAccessibleDescription("bench_mem_read");
    memWriteLabel_ = createResultRow("쓰기 대역폭");
    memWriteLabel_->setAccessibleDescription("bench_mem_write");
    memCopyLabel_ = createResultRow("복사 대역폭");
    memCopyLabel_->setAccessibleDescription("bench_mem_copy");
    memLatencyLabel_ = createResultRow("랜덤 지연시간");
    memLatencyLabel_->setAccessibleDescription("bench_mem_latency");
    memChannelsLabel_ = createResultRow("채널 (추정)");
    memChannelsLabel_->setAccessibleDescription("bench_mem_channels");

    return memFrame_;
}

void BenchmarkPanel::clearResults()
{
    l1LatLabel_->setText("--");
    l2LatLabel_->setText("--");
    l3LatLabel_->setText("--");
    dramLatLabel_->setText("--");
    l1LatBar_->setValue(0);
    l2LatBar_->setValue(0);
    l3LatBar_->setValue(0);
    dramLatBar_->setValue(0);

    l1BwLabel_->setText("--");
    l2BwLabel_->setText("--");
    l3BwLabel_->setText("--");
    dramBwLabel_->setText("--");
    l1BwBar_->setValue(0);
    l2BwBar_->setValue(0);
    l3BwBar_->setValue(0);
    dramBwBar_->setValue(0);

    memReadLabel_->setText("--");
    memWriteLabel_->setText("--");
    memCopyLabel_->setText("--");
    memLatencyLabel_->setText("--");
    memChannelsLabel_->setText("--");
}

void BenchmarkPanel::onRunClicked()
{
    if (isRunning_) return;

    isRunning_ = true;
    runBtn_->setEnabled(false);
    runBtn_->setText("실행 중...");
    runBtn_->setStyleSheet(
        "QPushButton { background-color: #5D3F6A; color: #999; border: none; "
        "border-radius: 6px; font-size: 14px; font-weight: bold; }"
    );

    statusLabel_->setText("벤치마크 진행 중...");
    clearResults();

    emit benchmarkStartRequested();

    // Run benchmarks in a background thread to keep UI responsive
    auto future = QtConcurrent::run([this]() {
        CacheBenchmark cacheBench;
        auto cacheRes = cacheBench.run();

        MemoryBenchmark memBench;
        auto memRes = memBench.run();

        // Post results back to the UI thread
        QMetaObject::invokeMethod(this, [this, cacheRes, memRes]() {
            onBenchmarkFinished(cacheRes, memRes);
        }, Qt::QueuedConnection);
    });
}

void BenchmarkPanel::onBenchmarkFinished(const CacheLatencyResult& c,
                                          const MemoryBenchmarkResult& m)
{
    // Cache latency results (bars scaled: max ~100 ns for L3/DRAM)
    auto setLat = [](QLabel* label, QProgressBar* bar, double ns) {
        label->setText(QString::number(ns, 'f', 1) + " ns");
        bar->setValue(static_cast<int>(qMin(ns * 10.0, 1000.0))); // scale: 100 ns = full
    };
    setLat(l1LatLabel_,   l1LatBar_,   c.l1_latency_ns);
    setLat(l2LatLabel_,   l2LatBar_,   c.l2_latency_ns);
    setLat(l3LatLabel_,   l3LatBar_,   c.l3_latency_ns);
    setLat(dramLatLabel_, dramLatBar_, c.dram_latency_ns);

    // Cache bandwidth results (bars scaled: max ~100 GB/s)
    auto setBw = [](QLabel* label, QProgressBar* bar, double gbs) {
        label->setText(QString::number(gbs, 'f', 1) + " GB/s");
        bar->setValue(static_cast<int>(qMin(gbs * 10.0, 1000.0))); // scale: 100 GB/s = full
    };
    setBw(l1BwLabel_,   l1BwBar_,   c.l1_bw_gbs);
    setBw(l2BwLabel_,   l2BwBar_,   c.l2_bw_gbs);
    setBw(l3BwLabel_,   l3BwBar_,   c.l3_bw_gbs);
    setBw(dramBwLabel_, dramBwBar_, c.dram_bw_gbs);

    // Memory benchmark results
    memReadLabel_->setText(QString::number(m.read_bw_gbs, 'f', 1) + " GB/s");
    memWriteLabel_->setText(QString::number(m.write_bw_gbs, 'f', 1) + " GB/s");
    memCopyLabel_->setText(QString::number(m.copy_bw_gbs, 'f', 1) + " GB/s");
    memLatencyLabel_->setText(QString::number(m.latency_ns, 'f', 1) + " ns");
    memChannelsLabel_->setText(QString::number(m.channels_detected));

    // Re-enable the button
    isRunning_ = false;
    runBtn_->setEnabled(true);
    runBtn_->setText("벤치마크 실행");
    runBtn_->setStyleSheet(
        "QPushButton { background-color: #8E44AD; color: white; border: none; "
        "border-radius: 6px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background-color: #9B59B6; }"
    );
    statusLabel_->setText("벤치마크 완료");
}

}} // namespace occt::gui
