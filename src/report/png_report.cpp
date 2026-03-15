#include "png_report.h"

#include <QPainter>
#include <QImage>
#include <QDateTime>
#include <QFont>
#include <QRectF>

namespace occt {

bool PngReport::save(const TestResults& results, const QString& path)
{
    const int W = 800;
    const int H = 600;
    QImage image(W, H, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(13, 17, 23)); // #0D1117

    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // ── Header ──
    p.fillRect(0, 0, W, 60, QColor(192, 57, 43)); // #C0392B
    QFont headerFont("Arial", 18, QFont::Bold);
    p.setFont(headerFont);
    p.setPen(Qt::white);
    p.drawText(20, 38, "OCCT Native Stress Test Report");

    QFont smallFont("Arial", 10);
    p.setFont(smallFont);
    p.drawText(W - 180, 38, "v1.0.0");

    // ── System Info Section ──
    int y = 80;
    QFont sectionFont("Arial", 13, QFont::Bold);
    p.setFont(sectionFont);
    p.setPen(QColor(240, 246, 252)); // #F0F6FC
    p.drawText(20, y, "System Information");
    y += 5;
    p.setPen(QColor(48, 54, 61));
    p.drawLine(20, y, W - 20, y);
    y += 18;

    QFont bodyFont("Arial", 10);
    p.setFont(bodyFont);
    p.setPen(QColor(201, 209, 217)); // #C9D1D9

    const auto& si = results.system_info;
    p.drawText(20, y, QString("CPU: %1 (%2C/%3T)").arg(si.cpu_name).arg(si.cpu_cores).arg(si.cpu_threads));
    y += 16;
    p.drawText(20, y, QString("GPU: %1").arg(si.gpu_name));
    y += 16;
    p.drawText(20, y, QString("RAM: %1  |  OS: %2").arg(si.ram_total, si.os_name));
    y += 28;

    // ── Test Results Summary ──
    p.setFont(sectionFont);
    p.setPen(QColor(240, 246, 252));
    p.drawText(20, y, "Test Results");
    y += 5;
    p.setPen(QColor(48, 54, 61));
    p.drawLine(20, y, W - 20, y);
    y += 6;

    // Table header
    p.setFont(bodyFont);
    p.fillRect(20, y, W - 40, 22, QColor(22, 27, 34)); // #161B22
    p.setPen(QColor(240, 246, 252));
    p.drawText(30,  y + 15, "Test");
    p.drawText(200, y + 15, "Mode");
    p.drawText(370, y + 15, "Duration");
    p.drawText(500, y + 15, "Errors");
    p.drawText(620, y + 15, "Result");
    y += 24;

    int pass_count = 0;
    int fail_count = 0;
    int max_rows = qMin(results.results.size(), 8);
    for (int i = 0; i < max_rows; ++i) {
        const auto& r = results.results[i];
        if (i % 2 == 0) {
            p.fillRect(20, y, W - 40, 20, QColor(22, 27, 34, 80));
        }

        p.setPen(QColor(201, 209, 217));
        p.drawText(30,  y + 14, r.test_type);
        p.drawText(200, y + 14, r.mode);
        p.drawText(370, y + 14, r.duration);
        p.drawText(500, y + 14, QString::number(r.error_count));

        if (r.passed) {
            p.setPen(QColor(46, 204, 113));  // green
            p.drawText(620, y + 14, "PASS");
            pass_count++;
        } else {
            p.setPen(QColor(231, 76, 60));   // red
            p.drawText(620, y + 14, "FAIL");
            fail_count++;
        }
        y += 20;
    }
    if (results.results.size() > 8) {
        p.setPen(QColor(139, 148, 158));
        p.drawText(30, y + 14, QString("... and %1 more").arg(results.results.size() - 8));
        y += 20;
    }
    y += 16;

    // ── Sensor Mini Graph ──
    if (!results.sensor_series.isEmpty()) {
        p.setFont(sectionFont);
        p.setPen(QColor(240, 246, 252));
        p.drawText(20, y, "Sensor Data (Temperature)");
        y += 5;
        p.setPen(QColor(48, 54, 61));
        p.drawLine(20, y, W - 20, y);
        y += 10;

        // Draw a simple line graph area
        int graphX = 40;
        int graphY = y;
        int graphW = W - 80;
        int graphH = qMin(120, H - y - 80);

        // Background
        p.fillRect(graphX, graphY, graphW, graphH, QColor(22, 27, 34));
        p.setPen(QColor(48, 54, 61));
        p.drawRect(graphX, graphY, graphW, graphH);

        // Find data range
        double minVal = 1e9, maxVal = -1e9;
        double maxTime = 0.0;
        for (const auto& dp : results.sensor_series) {
            if (dp.value < minVal) minVal = dp.value;
            if (dp.value > maxVal) maxVal = dp.value;
            if (dp.timestamp_sec > maxTime) maxTime = dp.timestamp_sec;
        }
        if (maxVal <= minVal) { maxVal = minVal + 10.0; }
        if (maxTime <= 0.0) maxTime = 1.0;

        double valueRange = maxVal - minVal;
        double padding = valueRange * 0.1;
        minVal -= padding;
        maxVal += padding;
        valueRange = maxVal - minVal;

        // Draw data points
        p.setPen(QPen(QColor(52, 152, 219), 1.5)); // blue line
        QPointF prev;
        bool first = true;
        for (const auto& dp : results.sensor_series) {
            double px = graphX + (dp.timestamp_sec / maxTime) * graphW;
            double py = graphY + graphH - ((dp.value - minVal) / valueRange) * graphH;
            QPointF pt(px, py);
            if (!first) {
                p.drawLine(prev, pt);
            }
            prev = pt;
            first = false;
        }

        // Axis labels
        p.setFont(QFont("Arial", 8));
        p.setPen(QColor(139, 148, 158));
        p.drawText(graphX, graphY - 2, QString::number(maxVal, 'f', 1));
        p.drawText(graphX, graphY + graphH + 12, QString::number(minVal, 'f', 1));
        p.drawText(graphX + graphW - 30, graphY + graphH + 12, QString("%1s").arg(int(maxTime)));

        y += graphH + 24;
    }

    // ── Footer ──
    int footerY = H - 40;
    p.fillRect(0, footerY, W, 40, QColor(22, 27, 34));

    // Overall verdict
    QFont verdictFont("Arial", 14, QFont::Bold);
    p.setFont(verdictFont);
    bool overall_pass = (fail_count == 0 && pass_count > 0);
    if (overall_pass) {
        p.setPen(QColor(46, 204, 113));
        p.drawText(20, footerY + 26, "OVERALL: PASS");
    } else {
        p.setPen(QColor(231, 76, 60));
        p.drawText(20, footerY + 26, "OVERALL: FAIL");
    }

    p.setFont(QFont("Arial", 9));
    p.setPen(QColor(139, 148, 158));
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    p.drawText(W - 200, footerY + 26, "Generated: " + ts);

    p.end();

    return image.save(path, "PNG");
}

} // namespace occt
