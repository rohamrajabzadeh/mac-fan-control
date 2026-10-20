// charts.h — time-series line chart with gradient areas, smooth curves and a
// hover crosshair, plus a sparkline painter used by tiles and tree rows.
#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>
#include <cmath>
#include <functional>

class History;
class QPainter;

struct ChartSeries {
    QString id;      // history series id, e.g. "t:cpu", "f:0", "s:cpu"
    QString label;
    QColor color;
    bool visible = true;
};

class LineChart : public QWidget {
    Q_OBJECT
public:
    explicit LineChart(const History *h, QWidget *parent = nullptr);

    void setSeries(const QVector<ChartSeries> &s);
    void setSeriesVisible(const QString &id, bool on);
    void setWindow(int seconds) { window_ = seconds; update(); }
    void setFixedYRange(double mn, double mx) { fixed_ = true; ymin_ = mn; ymax_ = mx; update(); }
    void setAutoY(double floorMin = 0, double minSpan = 10) { fixed_ = false; floor_ = floorMin; minSpan_ = minSpan; update(); }
    // Applied to raw values before plotting (e.g. °C -> °F).
    void setTransform(std::function<double(double)> f) { tf_ = std::move(f); update(); }
    // Formats axis/tooltip values (already transformed).
    void setFormatter(std::function<QString(double)> f) { fmt_ = std::move(f); update(); }
    void setFillAlpha(double a) { fillAlpha_ = a; }
    // Shade the background wherever this series is > 0 (thermal pressure).
    void setBand(const QString &id) { band_ = id; update(); }

    QSize sizeHint() const override { return QSize(600, 220); }

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    const History *h_;
    QVector<ChartSeries> series_;
    int window_ = 300;
    bool fixed_ = false;
    double ymin_ = 0, ymax_ = 100, floor_ = 0, minSpan_ = 10;
    double fillAlpha_ = 0.28;
    std::function<double(double)> tf_;
    std::function<QString(double)> fmt_;
    qreal hoverX_ = -1;
    QString band_;
};

// Draw a compact smoothed sparkline (with soft area fill) for one series.
void drawSparkline(QPainter &p, const QRectF &r, const History &h, const QString &id,
                   const QColor &color, int seconds, double fixedMin = NAN, double fixedMax = NAN);
