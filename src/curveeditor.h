// curveeditor.h — interactive fan curve: drag points, double-click to add,
// right-click to remove. Shows the live operating point on the curve.
#pragma once

#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>
#include <functional>

class CurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit CurveEditor(QWidget *parent = nullptr);
    void setRanges(double tMin, double tMax, double rMin, double rMax);
    void setPoints(const QVector<QPointF> &pts); // (°C, RPM); ignored while dragging
    QVector<QPointF> points() const { return pts_; }
    void setLive(double tempC, double rpm, bool show);
    void setTempDisplay(std::function<double(double)> toDisplay) { disp_ = std::move(toDisplay); update(); }
    QSize sizeHint() const override { return QSize(460, 220); }

signals:
    void pointsEdited(const QVector<QPointF> &pts);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QRectF plot() const;
    QPointF toPx(const QPointF &v) const;
    QPointF toVal(const QPointF &px) const;
    int hit(const QPointF &px) const;
    double curveAt(double t) const;

    QVector<QPointF> pts_;
    double tMin_ = 30, tMax_ = 100, rMin_ = 1000, rMax_ = 5000;
    int drag_ = -1, hover_ = -1;
    double liveT_ = 0, liveR_ = 0;
    bool showLive_ = false;
    std::function<double(double)> disp_;
};
