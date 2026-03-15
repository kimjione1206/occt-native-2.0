#include "realtime_chart.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>

namespace occt { namespace gui {

RealtimeChart::RealtimeChart(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(200, 100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Initialize with one default series for backward compatibility
    ChartSeries defaultSeries;
    defaultSeries.name = title_.isEmpty() ? QStringLiteral("Default") : title_;
    defaultSeries.color = lineColor_;
    defaultSeries.unit = unit_;
    defaultSeries.visible = true;
    series_.append(defaultSeries);
}

int RealtimeChart::addSeries(const QString& name, const QColor& color, const QString& unit)
{
    ChartSeries s;
    s.name = name;
    s.color = color;
    s.unit = unit;
    s.visible = true;
    series_.append(s);
    return series_.size() - 1;
}

void RealtimeChart::addPoint(int seriesIndex, double value)
{
    if (seriesIndex < 0 || seriesIndex >= series_.size())
        return;

    auto& s = series_[seriesIndex];
    s.data.append(value);
    if (s.data.size() > maxPoints_)
        s.data.removeFirst();

    if (autoScale_)
        updateAutoScale();

    update();
}

void RealtimeChart::addPoint(double value)
{
    addPoint(0, value);
}

void RealtimeChart::setSeriesVisible(int seriesIndex, bool visible)
{
    if (seriesIndex < 0 || seriesIndex >= series_.size())
        return;
    series_[seriesIndex].visible = visible;
    if (autoScale_)
        updateAutoScale();
    update();
}

void RealtimeChart::clearSeries(int seriesIndex)
{
    if (seriesIndex < 0 || seriesIndex >= series_.size())
        return;
    series_[seriesIndex].data.clear();
    if (autoScale_)
        updateAutoScale();
    update();
}

void RealtimeChart::clear()
{
    for (auto& s : series_)
        s.data.clear();
    update();
}

bool RealtimeChart::hasAnyData() const
{
    for (const auto& s : series_) {
        if (s.visible && !s.data.isEmpty())
            return true;
    }
    return false;
}

void RealtimeChart::updateAutoScale()
{
    bool found = false;
    double globalMin = 0.0;
    double globalMax = 0.0;

    for (const auto& s : series_) {
        if (!s.visible || s.data.isEmpty())
            continue;

        double sMin = *std::min_element(s.data.begin(), s.data.end());
        double sMax = *std::max_element(s.data.begin(), s.data.end());

        if (!found) {
            globalMin = sMin;
            globalMax = sMax;
            found = true;
        } else {
            globalMin = std::min(globalMin, sMin);
            globalMax = std::max(globalMax, sMax);
        }
    }

    if (!found) return;

    double range = globalMax - globalMin;
    if (range < 1.0) range = 1.0;

    minY_ = globalMin - range * 0.1;
    maxY_ = globalMax + range * 0.1;

    if (minY_ < 0 && globalMin >= 0) minY_ = 0;
}

void RealtimeChart::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Margins
    double leftMargin = 55;
    double rightMargin = 15;
    double topMargin = title_.isEmpty() ? 10 : 30;
    double bottomMargin = 10;

    QRectF plotArea(
        leftMargin, topMargin,
        width() - leftMargin - rightMargin,
        height() - topMargin - bottomMargin
    );

    drawBackground(painter, plotArea);
    if (gridVisible_) drawGrid(painter, plotArea);
    drawYAxis(painter, plotArea);

    // Draw all visible series
    for (const auto& s : series_) {
        if (s.visible && !s.data.isEmpty())
            drawSeriesLine(painter, plotArea, s);
    }

    // Draw legend if more than 1 series
    if (series_.size() > 1)
        drawLegend(painter, plotArea);

    if (!title_.isEmpty()) drawTitle(painter, rect());
}

void RealtimeChart::drawBackground(QPainter& painter, const QRectF& plotArea)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(13, 17, 23));
    painter.drawRoundedRect(plotArea, 4, 4);
}

void RealtimeChart::drawGrid(QPainter& painter, const QRectF& plotArea)
{
    QPen gridPen(QColor(48, 54, 61), 1, Qt::DotLine);
    painter.setPen(gridPen);

    int hLines = 5;
    for (int i = 0; i <= hLines; ++i) {
        double y = plotArea.top() + (plotArea.height() * i / hLines);
        painter.drawLine(QPointF(plotArea.left(), y), QPointF(plotArea.right(), y));
    }

    int vLines = 6;
    for (int i = 0; i <= vLines; ++i) {
        double x = plotArea.left() + (plotArea.width() * i / vLines);
        painter.drawLine(QPointF(x, plotArea.top()), QPointF(x, plotArea.bottom()));
    }
}

void RealtimeChart::drawYAxis(QPainter& painter, const QRectF& plotArea)
{
    QFont axisFont = font();
    axisFont.setPixelSize(10);
    painter.setFont(axisFont);
    painter.setPen(QColor(149, 165, 166));

    int ticks = 5;
    for (int i = 0; i <= ticks; ++i) {
        double y = plotArea.top() + (plotArea.height() * i / ticks);
        double val = maxY_ - (maxY_ - minY_) * i / ticks;

        QString text;
        if (std::abs(val) >= 1000)
            text = QString::number(val, 'f', 0);
        else if (std::abs(val) >= 10)
            text = QString::number(val, 'f', 1);
        else
            text = QString::number(val, 'f', 2);

        if (!unit_.isEmpty())
            text += " " + unit_;

        QRectF labelRect(0, y - 8, plotArea.left() - 5, 16);
        painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, text);
    }
}

void RealtimeChart::drawSeriesLine(QPainter& painter, const QRectF& plotArea, const ChartSeries& series)
{
    if (series.data.size() < 2) return;
    if (maxPoints_ < 2) return;

    double range = maxY_ - minY_;
    if (range < 0.001) range = 1.0;

    QPainterPath path;
    int count = series.data.size();
    double dx = plotArea.width() / (maxPoints_ - 1);
    double startX = plotArea.right() - (count - 1) * dx;

    for (int i = 0; i < count; ++i) {
        double x = startX + i * dx;
        double normalized = (series.data[i] - minY_) / range;
        normalized = std::clamp(normalized, 0.0, 1.0);
        double y = plotArea.bottom() - normalized * plotArea.height();

        if (i == 0)
            path.moveTo(x, y);
        else
            path.lineTo(x, y);
    }

    // Fill under curve
    if (fillEnabled_) {
        QPainterPath fillPath = path;
        double lastX = startX + (count - 1) * dx;
        fillPath.lineTo(lastX, plotArea.bottom());
        fillPath.lineTo(startX, plotArea.bottom());
        fillPath.closeSubpath();

        QLinearGradient grad(0, plotArea.top(), 0, plotArea.bottom());
        QColor fillColor = series.color;
        fillColor.setAlpha(80);
        grad.setColorAt(0, fillColor);
        fillColor.setAlpha(10);
        grad.setColorAt(1, fillColor);

        painter.setPen(Qt::NoPen);
        painter.setBrush(grad);
        painter.drawPath(fillPath);
    }

    // Draw line
    QPen linePen(series.color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(linePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    // Current value dot
    if (count > 0) {
        double lastNorm = (series.data.last() - minY_) / range;
        lastNorm = std::clamp(lastNorm, 0.0, 1.0);
        double lastY = plotArea.bottom() - lastNorm * plotArea.height();
        double lastXp = startX + (count - 1) * dx;

        painter.setPen(Qt::NoPen);
        painter.setBrush(series.color);
        painter.drawEllipse(QPointF(lastXp, lastY), 4, 4);

        // Glow effect
        QColor glow = series.color;
        glow.setAlpha(60);
        painter.setBrush(glow);
        painter.drawEllipse(QPointF(lastXp, lastY), 8, 8);
    }
}

void RealtimeChart::drawLegend(QPainter& painter, const QRectF& plotArea)
{
    QFont legendFont = font();
    legendFont.setPixelSize(10);
    painter.setFont(legendFont);
    QFontMetrics fm(legendFont);

    // Calculate legend dimensions
    const int swatchSize = 8;
    const int itemPadding = 6;
    const int innerPadding = 4;
    const int legendPadding = 6;

    int totalWidth = 0;
    int visibleCount = 0;
    for (const auto& s : series_) {
        if (!s.visible) continue;
        int textWidth = fm.horizontalAdvance(s.name);
        totalWidth += swatchSize + innerPadding + textWidth;
        visibleCount++;
    }
    if (visibleCount == 0) return;

    totalWidth += (visibleCount - 1) * itemPadding;
    totalWidth += 2 * legendPadding;
    int legendHeight = fm.height() + 2 * legendPadding;

    // Position in top-right corner of plot area
    double legendX = plotArea.right() - totalWidth - 4;
    double legendY = plotArea.top() + 4;

    // Background
    QRectF legendRect(legendX, legendY, totalWidth, legendHeight);
    painter.setPen(Qt::NoPen);
    QColor bgColor(13, 17, 23, 200);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(legendRect, 3, 3);

    // Border
    painter.setPen(QPen(QColor(48, 54, 61), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(legendRect, 3, 3);

    // Draw items
    double cx = legendX + legendPadding;
    double cy = legendY + legendPadding;

    for (const auto& s : series_) {
        if (!s.visible) continue;

        // Color swatch
        painter.setPen(Qt::NoPen);
        painter.setBrush(s.color);
        double swatchY = cy + (fm.height() - swatchSize) / 2.0;
        painter.drawRoundedRect(QRectF(cx, swatchY, swatchSize, swatchSize), 2, 2);
        cx += swatchSize + innerPadding;

        // Name
        painter.setPen(QColor(201, 209, 217));
        painter.drawText(QPointF(cx, cy + fm.ascent()), s.name);
        cx += fm.horizontalAdvance(s.name) + itemPadding;
    }
}

void RealtimeChart::drawTitle(QPainter& painter, const QRectF& fullArea)
{
    QFont titleFont = font();
    titleFont.setPixelSize(12);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor(201, 209, 217));

    QRectF titleRect(fullArea.left() + 55, fullArea.top(), fullArea.width() - 70, 25);
    painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, title_);
}

}} // namespace occt::gui
