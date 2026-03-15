#include "circular_gauge.h"

#include <QPainter>
#include <QPainterPath>
#include <QConicalGradient>
#include <cmath>

namespace occt { namespace gui {

CircularGauge::CircularGauge(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(100, 100);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

void CircularGauge::setValue(double val)
{
    val = std::clamp(val, 0.0, 100.0);
    if (std::abs(value_ - val) > 0.01) {
        value_ = val;
        emit valueChanged(value_);
        update();
    }
}

QColor CircularGauge::valueToColor(double val) const
{
    if (arcColor_.isValid())
        return arcColor_;

    // Green -> Yellow -> Red gradient
    if (val < 50.0) {
        int r = static_cast<int>(39 + (val / 50.0) * (243 - 39));
        int g = static_cast<int>(174 + (val / 50.0) * (156 - 174));
        int b = static_cast<int>(96 + (val / 50.0) * (18 - 96));
        return QColor(r, g, b);
    } else {
        int r = static_cast<int>(243 + ((val - 50.0) / 50.0) * (192 - 243));
        int g = static_cast<int>(156 - ((val - 50.0) / 50.0) * 156);
        int b = static_cast<int>(18 + ((val - 50.0) / 50.0) * (43 - 18));
        return QColor(r, g, b);
    }
}

void CircularGauge::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int side = qMin(width(), height());
    int margin = 10;
    QRectF gaugeRect(
        (width() - side) / 2.0 + margin,
        (height() - side) / 2.0 + margin,
        side - 2 * margin,
        side - 2 * margin
    );

    double penWidth = side * 0.08;

    // Background arc (track)
    QPen trackPen(QColor(30, 36, 48), penWidth, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(trackPen);
    painter.drawArc(gaugeRect, 225 * 16, -270 * 16);

    // Value arc
    QColor arcCol = valueToColor(value_);
    QPen valuePen(arcCol, penWidth, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(valuePen);
    double spanAngle = -(value_ / 100.0) * 270.0;
    painter.drawArc(gaugeRect, 225 * 16, static_cast<int>(spanAngle * 16));

    // Center value text
    painter.setPen(Qt::NoPen);
    QFont valueFont = font();
    valueFont.setPixelSize(static_cast<int>(side * 0.22));
    valueFont.setBold(true);
    painter.setFont(valueFont);
    painter.setPen(QColor(236, 240, 241));

    QString valueStr = overlayText_.isEmpty()
        ? QString::number(static_cast<int>(value_)) + unit_
        : overlayText_;
    QRectF textRect = gaugeRect;
    textRect.moveTop(textRect.top() - side * 0.05);
    painter.drawText(textRect, Qt::AlignCenter, valueStr);

    // Label text below value
    if (!label_.isEmpty()) {
        QFont labelFont = font();
        labelFont.setPixelSize(static_cast<int>(side * 0.10));
        painter.setFont(labelFont);
        painter.setPen(QColor(149, 165, 166));

        QRectF labelRect = gaugeRect;
        labelRect.moveTop(labelRect.top() + side * 0.15);
        painter.drawText(labelRect, Qt::AlignCenter, label_);
    }
}

}} // namespace occt::gui
