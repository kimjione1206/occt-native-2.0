#include "sysinfo_panel.h"
#include "panel_styles.h"
#include "../../monitor/system_info.h"

#include <QHBoxLayout>
#include <QScrollArea>
#include <QGridLayout>

namespace occt { namespace gui {

using Row = QPair<QString,QString>;

SysInfoPanel::SysInfoPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

QFrame* SysInfoPanel::createSection(const QString& title,
                                     const QVector<QPair<QString,QString>>& rows)
{
    auto* frame = new QFrame(this);
    frame->setStyleSheet(
        styles::kSectionFrame);

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    auto* titleLabel = new QLabel(title, frame);
    titleLabel->setStyleSheet(
        "color: #F0F6FC; font-size: 15px; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(titleLabel);

    auto* sep = new QFrame(frame);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background-color: #30363D; max-height: 1px; border: none;");
    layout->addWidget(sep);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(6);

    int row = 0;
    for (const auto& kv : rows) {
        auto* keyLabel = new QLabel(kv.first, frame);
        keyLabel->setStyleSheet("color: #8B949E; font-size: 13px; border: none; background: transparent;");

        auto* valLabel = new QLabel(kv.second, frame);
        valLabel->setStyleSheet("color: #C9D1D9; font-size: 13px; border: none; background: transparent;");
        valLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        valLabel->setAccessibleDescription(
            "sysinfo_" + title.toLower().replace(" ", "_") + "_" + kv.first.toLower().replace(" ", "_"));

        grid->addWidget(keyLabel, row, 0);
        grid->addWidget(valLabel, row, 1);
        ++row;
    }
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);

    return frame;
}

void SysInfoPanel::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    auto* title = new QLabel("시스템 정보", this);
    title->setStyleSheet("color: #F0F6FC; font-size: 20px; font-weight: bold; background: transparent;");
    mainLayout->addWidget(title);

    auto* subtitle = new QLabel("상세 하드웨어 및 소프트웨어 구성", this);
    subtitle->setStyleSheet("color: #8B949E; font-size: 13px; background: transparent;");
    mainLayout->addWidget(subtitle);

    // Scroll area for the sections
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet(
        "QScrollArea { border: none; background: transparent; }"
        "QScrollBar:vertical { background: #0D1117; width: 8px; }"
        "QScrollBar::handle:vertical { background: #30363D; border-radius: 4px; }"
    );

    auto* scrollContent = new QWidget(scrollArea);
    auto* contentLayout = new QVBoxLayout(scrollContent);
    contentLayout->setSpacing(16);
    contentLayout->setContentsMargins(0, 0, 8, 0);

    // Collect system info
    auto info = occt::collect_system_info();

    // CPU section
    QVector<Row> cpuRows;
    cpuRows << Row(QStringLiteral("모델"), info.cpu.model.isEmpty() ? QStringLiteral("알 수 없음") : info.cpu.model);
    cpuRows << Row(QStringLiteral("물리 코어"), QString::number(info.cpu.physical_cores));
    cpuRows << Row(QStringLiteral("논리 코어"), QString::number(info.cpu.logical_cores));
    if (info.cpu.base_clock_mhz > 0)
        cpuRows << Row(QStringLiteral("기본 클럭"), QString::number(info.cpu.base_clock_mhz) + " MHz");
    if (info.cpu.boost_clock_mhz > 0)
        cpuRows << Row(QStringLiteral("부스트 클럭"), QString::number(info.cpu.boost_clock_mhz) + " MHz");
    if (info.cpu.l1_cache_kb > 0)
        cpuRows << Row(QStringLiteral("L1 캐시"), QString::number(info.cpu.l1_cache_kb) + " KB");
    if (info.cpu.l2_cache_kb > 0)
        cpuRows << Row(QStringLiteral("L2 캐시"), QString::number(info.cpu.l2_cache_kb) + " KB");
    if (info.cpu.l3_cache_kb > 0)
        cpuRows << Row(QStringLiteral("L3 캐시"), QString::number(info.cpu.l3_cache_kb) + " KB");
    if (!info.cpu.microarchitecture.isEmpty())
        cpuRows << Row(QStringLiteral("마이크로아키텍처"), info.cpu.microarchitecture);
    contentLayout->addWidget(createSection("CPU", cpuRows));

    // GPU section
    for (int i = 0; i < info.gpus.size(); ++i) {
        const auto& gpu = info.gpus[i];
        QString sec_title = info.gpus.size() > 1 ? QString("GPU %1").arg(i) : QStringLiteral("GPU");
        QVector<Row> gpuRows;
        gpuRows << Row(QStringLiteral("모델"), gpu.model.isEmpty() ? QStringLiteral("알 수 없음") : gpu.model);
        if (gpu.vram_mb > 0)
            gpuRows << Row(QStringLiteral("VRAM"), QString::number(gpu.vram_mb) + " MB");
        if (!gpu.driver_version.isEmpty())
            gpuRows << Row(QStringLiteral("드라이버"), gpu.driver_version);
        gpuRows << Row(QStringLiteral("OpenCL"), gpu.has_opencl ? QStringLiteral("예") : QStringLiteral("아니오"));
        gpuRows << Row(QStringLiteral("Vulkan"), gpu.has_vulkan ? QStringLiteral("예") : QStringLiteral("아니오"));
        contentLayout->addWidget(createSection(sec_title, gpuRows));
    }
    if (info.gpus.isEmpty()) {
        QVector<Row> gpuRows;
        gpuRows << Row(QStringLiteral("상태"), QStringLiteral("시스템 API를 통해 GPU가 감지되지 않음"));
        contentLayout->addWidget(createSection("GPU", gpuRows));
    }

    // RAM section
    QVector<Row> ramRows;
    ramRows << Row(QStringLiteral("총 용량"),
                   QString::number(info.ram.total_mb) + " MB (" +
                   QString::number(info.ram.total_mb / 1024) + " GB)");
    if (info.ram.speed_mhz > 0)
        ramRows << Row(QStringLiteral("속도"), QString::number(info.ram.speed_mhz) + " MHz");
    if (!info.ram.timing.isEmpty())
        ramRows << Row(QStringLiteral("타이밍"), info.ram.timing);
    if (info.ram.slot_count > 0)
        ramRows << Row(QStringLiteral("슬롯"), QString::number(info.ram.slot_count));
    contentLayout->addWidget(createSection("RAM", ramRows));

    // Storage section
    for (int i = 0; i < info.storage.size(); ++i) {
        const auto& disk = info.storage[i];
        QVector<Row> diskRows;
        diskRows << Row(QStringLiteral("모델"), disk.model.isEmpty() ? QStringLiteral("알 수 없음") : disk.model);
        if (disk.capacity_gb > 0)
            diskRows << Row(QStringLiteral("용량"), QString::number(disk.capacity_gb) + " GB");
        diskRows << Row(QStringLiteral("인터페이스"),
                        disk.interface_type.isEmpty() ? QStringLiteral("알 수 없음") : disk.interface_type);
        contentLayout->addWidget(createSection(
            info.storage.size() > 1 ? QString("저장장치 %1").arg(i) : QStringLiteral("저장장치"), diskRows));
    }
    if (info.storage.isEmpty()) {
        QVector<Row> diskRows;
        diskRows << Row(QStringLiteral("상태"), QStringLiteral("저장장치 감지에는 플랫폼별 API가 필요합니다"));
        contentLayout->addWidget(createSection("저장장치", diskRows));
    }

    // OS section
    QVector<Row> osRows;
    osRows << Row(QStringLiteral("이름"), info.os.name);
    if (!info.os.version.isEmpty())
        osRows << Row(QStringLiteral("버전"), info.os.version);
    if (!info.os.build.isEmpty())
        osRows << Row(QStringLiteral("빌드"), info.os.build);
    osRows << Row(QStringLiteral("아키텍처"), info.os.architecture);
    contentLayout->addWidget(createSection("운영체제", osRows));

    contentLayout->addStretch();
    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea, 1);
}

}} // namespace occt::gui
