#pragma once

#include <QWidget>
#include <QTimer>
#include <QVector>
#include <QColor>
#include <QPainter>
#include <QString>

namespace occt { namespace gui {

struct ChartSeries {
    QString name;
    QVector<double> data;
    QColor color;
    bool visible = true;
    QString unit;
};

class RealtimeChart : public QWidget {
    Q_OBJECT

public:
    explicit RealtimeChart(QWidget* parent = nullptr);

    // Multi-series API
    int addSeries(const QString& name, const QColor& color, const QString& unit = "");
    void addPoint(int seriesIndex, double value);
    void setSeriesVisible(int seriesIndex, bool visible);
    void clearSeries(int seriesIndex);

    // Backward-compatible single-series API
    void addPoint(double value);
    void clear();

    void setMaxPoints(int maxPoints) { maxPoints_ = maxPoints; }
    void setYRange(double minY, double maxY) { minY_ = minY; maxY_ = maxY; autoScale_ = false; update(); }
    void setAutoScale(bool on) { autoScale_ = on; }
    void setLineColor(const QColor& color) { lineColor_ = color; if (!series_.isEmpty()) series_[0].color = color; update(); }
    void setTitle(const QString& title) { title_ = title; update(); }
    void setUnit(const QString& unit) { unit_ = unit; if (!series_.isEmpty()) series_[0].unit = unit; update(); }
    void setGridVisible(bool visible) { gridVisible_ = visible; update(); }
    void setFillEnabled(bool enabled) { fillEnabled_ = enabled; update(); }

    QSize sizeHint() const override { return QSize(400, 200); }
    QSize minimumSizeHint() const override { return QSize(200, 100); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void drawBackground(QPainter& painter, const QRectF& plotArea);
    void drawGrid(QPainter& painter, const QRectF& plotArea);
    void drawYAxis(QPainter& painter, const QRectF& plotArea);
    void drawSeriesLine(QPainter& painter, const QRectF& plotArea, const ChartSeries& series);
    void drawLegend(QPainter& painter, const QRectF& plotArea);
    void drawTitle(QPainter& painter, const QRectF& fullArea);
    void updateAutoScale();

    bool hasAnyData() const;

    QVector<ChartSeries> series_;
    int maxPoints_ = 120;
    double minY_ = 0.0;
    double maxY_ = 100.0;
    bool autoScale_ = true;
    QColor lineColor_ = QColor(192, 57, 43); // #C0392B
    QString title_;
    QString unit_;
    bool gridVisible_ = true;
    bool fillEnabled_ = true;
};

}} // namespace occt::gui
