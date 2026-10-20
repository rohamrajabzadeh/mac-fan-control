#include "gauge.h"

#include <QPainter>
#include <QPainterPath>
#include <QConicalGradient>
#include <QtMath>

Gauge::Gauge(QWidget *parent) : QWidget(parent), anim_(this, "displayValue") {
    anim_.setDuration(600);
    anim_.setEasingCurve(QEasingCurve::OutCubic);
    setMinimumSize(130, 130);
}

void Gauge::setRange(double min, double max) {
    min_ = min;
    max_ = (max > min) ? max : min + 1;
    update();
}

void Gauge::setValue(double v) {
    if (v < min_) v = min_;
    if (v > max_) v = max_;
    target_ = v;
    anim_.stop();
    anim_.setStartValue(display_);
    anim_.setEndValue(v);
    anim_.start();
}

void Gauge::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const double side = qMin(width(), height());
    const QRectF bounds((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    const double margin = side * 0.12;
    const QRectF arcRect = bounds.adjusted(margin, margin, -margin, -margin);
    const double penW = side * 0.075;

    // 270° sweep, starting at 225° (bottom-left) going clockwise.
    const double startAngle = 225.0;
    const double spanAngle = -270.0;
    const double frac = (max_ > min_) ? (display_ - min_) / (max_ - min_) : 0.0;

    // Track.
    QPen track(QColor(255, 255, 255, 28));
    track.setWidthF(penW);
    track.setCapStyle(Qt::RoundCap);
    p.setPen(track);
    p.drawArc(arcRect, int(startAngle * 16), int(spanAngle * 16));

    // Soft outer glow for depth.
    if (frac > 0.01) {
        QColor glow = accent_;
        glow.setAlpha(55);
        QPen glowPen(glow, penW * 1.9);
        glowPen.setCapStyle(Qt::RoundCap);
        p.setPen(glowPen);
        p.drawArc(arcRect, int(startAngle * 16), int(spanAngle * frac * 16));
    }

    // Vibrant multi-stop gradient arc: deep accent -> accent -> bright tip.
    QColor deep = accent_.darker(130);
    QColor mid = accent_;
    QColor bright = accent_.lighter(150);
    QConicalGradient grad(arcRect.center(), startAngle);
    grad.setColorAt(0.00, deep);
    grad.setColorAt(0.35, mid);
    grad.setColorAt(0.68, bright);
    grad.setColorAt(0.75, bright.lighter(112));
    QPen valuePen(QBrush(grad), penW);
    valuePen.setCapStyle(Qt::RoundCap);
    p.setPen(valuePen);
    p.drawArc(arcRect, int(startAngle * 16), int(spanAngle * frac * 16));

    // Center value text.
    p.setPen(QColor(245, 247, 250));
    QFont f = font();
    f.setPointSizeF(side * 0.17);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    QString valText = QString::number(qRound(display_));
    QRectF textRect = arcRect.adjusted(0, -side * 0.04, 0, -side * 0.02);
    p.drawText(textRect, Qt::AlignCenter, valText);

    // Unit under the value.
    QFont uf = font();
    uf.setPointSizeF(side * 0.072);
    p.setFont(uf);
    p.setPen(QColor(150, 158, 170));
    QRectF unitRect(arcRect.left(), arcRect.center().y() + side * 0.11,
                    arcRect.width(), side * 0.16);
    p.drawText(unitRect, Qt::AlignHCenter | Qt::AlignTop, unit_);

    // Caption below the gauge.
    if (!caption_.isEmpty()) {
        QFont cf = font();
        cf.setPointSizeF(side * 0.078);
        cf.setWeight(QFont::Medium);
        p.setFont(cf);
        p.setPen(QColor(205, 212, 222));
        QRectF capRect(bounds.left(), bounds.bottom() - side * 0.12,
                       bounds.width(), side * 0.14);
        p.drawText(capRect, Qt::AlignCenter, caption_);
    }
}
