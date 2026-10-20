#include "charts.h"
#include "history.h"
#include "theme.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------- helpers

static QPainterPath smoothPath(const QVector<QPointF> &pts) {
    QPainterPath path;
    if (pts.isEmpty()) return path;
    path.moveTo(pts[0]);
    if (pts.size() == 1) return path;
    for (int i = 1; i + 1 < pts.size(); ++i) {
        QPointF mid = (pts[i] + pts[i + 1]) / 2.0;
        path.quadTo(pts[i], mid);
    }
    path.lineTo(pts.last());
    return path;
}

// Average points into ~2px buckets so long histories stay smooth and fast.
static QVector<QPointF> downsample(const QVector<QPointF> &pts, qreal bucketPx) {
    if (pts.size() < 3) return pts;
    QVector<QPointF> out;
    qreal bx = pts[0].x();
    double sx = 0, sy = 0;
    int n = 0;
    for (const QPointF &p : pts) {
        if (p.x() - bx > bucketPx && n > 0) {
            out << QPointF(sx / n, sy / n);
            sx = sy = 0;
            n = 0;
            bx = p.x();
        }
        sx += p.x();
        sy += p.y();
        ++n;
    }
    if (n > 0) out << QPointF(sx / n, sy / n);
    return out;
}

static double niceStep(double span, int ticks) {
    double raw = span / std::max(1, ticks);
    double mag = std::pow(10.0, std::floor(std::log10(std::max(1e-9, raw))));
    double f = raw / mag;
    double nice = f < 1.5 ? 1 : f < 3 ? 2 : f < 7 ? 5 : 10;
    return nice * mag;
}

static void strokeSeries(QPainter &p, const QVector<QVector<QPointF>> &segs, const QColor &c,
                         qreal bottom, double fillAlpha, qreal width) {
    for (const auto &seg : segs) {
        if (seg.size() < 2) continue;
        QPainterPath line = smoothPath(seg);
        if (fillAlpha > 0) {
            QPainterPath area = line;
            area.lineTo(seg.last().x(), bottom);
            area.lineTo(seg.first().x(), bottom);
            area.closeSubpath();
            qreal top = bottom;
            for (const auto &pt : seg) top = std::min(top, pt.y());
            QLinearGradient g(0, top, 0, bottom);
            QColor a = c; a.setAlphaF(fillAlpha);
            QColor b = c; b.setAlphaF(0.0);
            g.setColorAt(0, a);
            g.setColorAt(1, b);
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawPath(area);
        }
        QColor glow = c; glow.setAlphaF(0.18);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(glow, width + 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(line);
        p.setPen(QPen(c, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(line);
    }
}

// ---------------------------------------------------------------- sparkline

void drawSparkline(QPainter &p, const QRectF &r, const History &h, const QString &id,
                   const QColor &color, int seconds, double fixedMin, double fixedMax) {
    if (h.isEmpty() || !h.has(id)) return;
    const qint64 now = h.latestTime(), t0 = now - qint64(seconds) * 1000;
    const int s = h.firstIndexSince(t0);
    QVector<QPair<qint64, double>> raw;
    double lo = 1e9, hi = -1e9;
    for (int i = s; i < h.size(); ++i) {
        float v = h.value(id, i);
        if (std::isnan(v)) continue;
        raw.push_back({h.time(i), v});
        lo = std::min(lo, double(v));
        hi = std::max(hi, double(v));
    }
    if (raw.size() < 2) return;
    if (!std::isnan(fixedMin)) lo = fixedMin;
    if (!std::isnan(fixedMax)) hi = fixedMax;
    if (hi - lo < 3) { double c = (hi + lo) / 2; lo = c - 1.5; hi = c + 1.5; }

    QVector<QPointF> pts;
    pts.reserve(raw.size());
    for (const auto &rv : raw) {
        qreal x = r.left() + double(rv.first - t0) / (seconds * 1000.0) * r.width();
        qreal y = r.bottom() - (rv.second - lo) / (hi - lo) * r.height();
        pts << QPointF(x, std::clamp(y, r.top(), r.bottom()));
    }
    pts = downsample(pts, 2.0);

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    strokeSeries(p, {pts}, color, r.bottom(), 0.30, 1.6);
    QPointF last = pts.last();
    QColor g = color; g.setAlphaF(0.30);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(last, 4, 4);
    p.setBrush(color);
    p.drawEllipse(last, 2, 2);
    p.restore();
}

// ---------------------------------------------------------------- LineChart

LineChart::LineChart(const History *h, QWidget *parent) : QWidget(parent), h_(h) {
    setMouseTracking(true);
    setMinimumHeight(150);
    fmt_ = [](double v) { return QString::number(qRound(v)); };
}

void LineChart::setSeries(const QVector<ChartSeries> &s) {
    series_ = s;
    update();
}

void LineChart::setSeriesVisible(const QString &id, bool on) {
    for (auto &s : series_)
        if (s.id == id) s.visible = on;
    update();
}

void LineChart::mouseMoveEvent(QMouseEvent *e) {
    hoverX_ = e->position().x();
    update();
}

void LineChart::leaveEvent(QEvent *) {
    hoverX_ = -1;
    update();
}

void LineChart::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF plot(48, 10, width() - 48 - 14, height() - 10 - 28);
    const QFont axisFont = Theme::font(10.5, QFont::Medium, true);
    p.setFont(axisFont);

    if (!h_ || h_->size() < 2) {
        p.setPen(Theme::textMuted());
        p.setFont(Theme::font(12, QFont::Medium));
        p.drawText(rect(), Qt::AlignCenter, "Collecting data…");
        return;
    }

    const qint64 now = h_->latestTime();
    const qint64 t0 = now - qint64(window_) * 1000;
    const int start = h_->firstIndexSince(t0);
    auto tf = [this](double v) { return tf_ ? tf_(v) : v; };

    // Gather visible data and its range.
    struct Raw { const ChartSeries *s; QVector<QPair<qint64, double>> pts; };
    QVector<Raw> data;
    double lo = 1e18, hi = -1e18;
    for (const auto &s : series_) {
        if (!s.visible) continue;
        Raw r{&s, {}};
        for (int i = start; i < h_->size(); ++i) {
            float v = h_->value(s.id, i);
            if (std::isnan(v)) { r.pts.push_back({h_->time(i), NAN}); continue; }
            double tv = tf(v);
            r.pts.push_back({h_->time(i), tv});
            lo = std::min(lo, tv);
            hi = std::max(hi, tv);
        }
        data.push_back(r);
    }

    double ymin, ymax;
    if (fixed_) {
        ymin = ymin_;
        ymax = ymax_;
    } else {
        if (lo > hi) { lo = floor_; hi = floor_ + minSpan_; }
        double span = std::max(minSpan_, hi - lo);
        double c = (hi + lo) / 2;
        lo = std::max(floor_, c - span * 0.65);
        hi = lo + span * 1.3;
        double step = niceStep(hi - lo, 4);
        ymin = std::floor(lo / step) * step;
        ymax = std::ceil(hi / step) * step;
        if (ymax - ymin < step) ymax = ymin + step;
    }
    const double yspan = std::max(1e-6, ymax - ymin);
    auto X = [&](qint64 t) { return plot.left() + double(t - t0) / (window_ * 1000.0) * plot.width(); };
    auto Y = [&](double v) { return plot.bottom() - (v - ymin) / yspan * plot.height(); };

    // Horizontal grid + y labels.
    const double step = niceStep(yspan, 4);
    QPen gridPen(QColor(255, 255, 255, 16), 1, Qt::DashLine);
    for (double v = std::ceil(ymin / step) * step; v <= ymax + 1e-9; v += step) {
        qreal y = Y(v);
        p.setPen(gridPen);
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(Theme::textMuted());
        p.drawText(QRectF(0, y - 8, plot.left() - 8, 16), Qt::AlignRight | Qt::AlignVCenter, fmt_(v));
    }

    // Time ticks.
    const int tstep = window_ <= 300 ? 60 : window_ <= 900 ? 180 : 600;
    for (int k = 0; k * tstep <= window_; ++k) {
        qreal x = X(now - qint64(k) * tstep * 1000);
        if (x < plot.left() - 1) break;
        p.setPen(QPen(QColor(255, 255, 255, 10), 1));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        QString lbl = k == 0 ? QStringLiteral("now") : QString("-%1m").arg(k * tstep / 60);
        p.setPen(Theme::textMuted());
        p.drawText(QRectF(x - 30, plot.bottom() + 6, 60, 16), Qt::AlignCenter, lbl);
    }

    // Series.
    p.save();
    p.setClipRect(plot.adjusted(-2, -6, 2, 2));
    const double fill = data.size() > 3 ? fillAlpha_ * 0.45 : fillAlpha_;
    for (const auto &d : data) {
        QVector<QVector<QPointF>> segs(1);
        qint64 prevT = 0;
        for (const auto &pt : d.pts) {
            bool gap = std::isnan(pt.second) || (prevT && pt.first - prevT > 15000);
            if (gap && !segs.last().isEmpty()) segs.push_back({});
            if (!std::isnan(pt.second))
                segs.last() << QPointF(X(pt.first), Y(std::clamp(pt.second, ymin, ymax)));
            prevT = pt.first;
        }
        for (auto &sg : segs) sg = downsample(sg, 2.0);
        strokeSeries(p, segs, d.s->color, plot.bottom(), fill, 2.2);
        // Live dot at the newest point.
        for (int k = segs.size() - 1; k >= 0; --k) {
            if (segs[k].isEmpty()) continue;
            QPointF last = segs[k].last();
            QColor g = d.s->color; g.setAlphaF(0.3);
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawEllipse(last, 6, 6);
            p.setBrush(d.s->color);
            p.drawEllipse(last, 3, 3);
            break;
        }
    }
    p.restore();

    // Hover crosshair + tooltip.
    if (hoverX_ >= plot.left() && hoverX_ <= plot.right() && !data.isEmpty()) {
        qint64 th = t0 + qint64((hoverX_ - plot.left()) / plot.width() * window_ * 1000.0);
        int i = std::clamp(h_->firstIndexSince(th), start, h_->size() - 1);
        if (i > start && std::abs(h_->time(i - 1) - th) < std::abs(h_->time(i) - th)) --i;
        qreal x = X(h_->time(i));
        p.setPen(QPen(QColor(255, 255, 255, 60), 1));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));

        QStringList lines;
        QVector<QColor> colors;
        for (const auto &d : data) {
            float v = h_->value(d.s->id, i);
            if (std::isnan(v)) continue;
            double tv = tf(v);
            p.setPen(Qt::NoPen);
            QColor g = d.s->color; g.setAlphaF(0.35);
            p.setBrush(g);
            p.drawEllipse(QPointF(x, Y(tv)), 6, 6);
            p.setBrush(d.s->color);
            p.drawEllipse(QPointF(x, Y(tv)), 3, 3);
            lines << d.s->label + "   " + fmt_(tv);
            colors << d.s->color;
        }
        if (!lines.isEmpty()) {
            const QFont tf2 = Theme::font(11.5, QFont::DemiBold, true);
            p.setFont(tf2);
            QFontMetricsF fm(tf2);
            QString when = QDateTime::fromMSecsSinceEpoch(h_->time(i)).toString("HH:mm:ss");
            qreal w = fm.horizontalAdvance(when);
            for (const auto &l : lines) w = std::max(w, fm.horizontalAdvance(l) + 16);
            w += 22;
            qreal hgt = 14 + (lines.size() + 1) * 18;
            qreal bx = x + 14;
            if (bx + w > width() - 4) bx = x - 14 - w;
            QRectF box(bx, plot.top() + 6, w, hgt);
            p.setPen(QPen(Theme::border(), 1));
            p.setBrush(QColor(0x16, 0x19, 0x1f, 240));
            p.drawRoundedRect(box, 9, 9);
            p.setPen(Theme::textMuted());
            p.drawText(QRectF(box.left() + 11, box.top() + 7, w - 20, 18), Qt::AlignLeft | Qt::AlignVCenter, when);
            for (int k = 0; k < lines.size(); ++k) {
                qreal ly = box.top() + 7 + (k + 1) * 18;
                p.setPen(Qt::NoPen);
                p.setBrush(colors[k]);
                p.drawEllipse(QPointF(box.left() + 15, ly + 9), 3.5, 3.5);
                p.setPen(Theme::text());
                p.drawText(QRectF(box.left() + 25, ly, w - 30, 18), Qt::AlignLeft | Qt::AlignVCenter, lines[k]);
            }
        }
    }
}
