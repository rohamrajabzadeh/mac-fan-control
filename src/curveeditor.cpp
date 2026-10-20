#include "curveeditor.h"
#include "theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

CurveEditor::CurveEditor(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumHeight(200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    disp_ = [](double c) { return c; };
}

void CurveEditor::setRanges(double tMin, double tMax, double rMin, double rMax) {
    tMin_ = tMin;
    tMax_ = tMax > tMin ? tMax : tMin + 1;
    rMin_ = rMin;
    rMax_ = rMax > rMin ? rMax : rMin + 1;
    update();
}

void CurveEditor::setPoints(const QVector<QPointF> &pts) {
    if (drag_ >= 0) return;
    pts_ = pts;
    std::sort(pts_.begin(), pts_.end(), [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    update();
}

void CurveEditor::setLive(double t, double r, bool show) {
    liveT_ = t;
    liveR_ = r;
    showLive_ = show;
    update();
}

QRectF CurveEditor::plot() const { return QRectF(54, 12, width() - 54 - 16, height() - 12 - 30); }

QPointF CurveEditor::toPx(const QPointF &v) const {
    QRectF pl = plot();
    return QPointF(pl.left() + (v.x() - tMin_) / (tMax_ - tMin_) * pl.width(),
                   pl.bottom() - (v.y() - rMin_) / (rMax_ - rMin_) * pl.height());
}

QPointF CurveEditor::toVal(const QPointF &px) const {
    QRectF pl = plot();
    return QPointF(tMin_ + (px.x() - pl.left()) / pl.width() * (tMax_ - tMin_),
                   rMin_ + (pl.bottom() - px.y()) / pl.height() * (rMax_ - rMin_));
}

int CurveEditor::hit(const QPointF &px) const {
    for (int i = 0; i < pts_.size(); ++i)
        if (QLineF(toPx(pts_[i]), px).length() <= 12) return i;
    return -1;
}

double CurveEditor::curveAt(double t) const {
    if (pts_.isEmpty()) return rMin_;
    if (t <= pts_.first().x()) return pts_.first().y();
    if (t >= pts_.last().x()) return pts_.last().y();
    for (int i = 0; i + 1 < pts_.size(); ++i)
        if (t <= pts_[i + 1].x()) {
            double s = pts_[i + 1].x() - pts_[i].x();
            double f = s > 0 ? (t - pts_[i].x()) / s : 0;
            return pts_[i].y() + (pts_[i + 1].y() - pts_[i].y()) * f;
        }
    return pts_.last().y();
}

void CurveEditor::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF pl = plot();

    // Background panel
    p.setPen(QPen(Theme::border(), 1));
    p.setBrush(QColor(0x15, 0x18, 0x1e));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);

    // Grid: temperatures every 10°, RPM in 4 bands
    const QFont af = Theme::font(10.5, QFont::Medium, true);
    p.setFont(af);
    for (int t = int(std::ceil(tMin_ / 10.0)) * 10; t <= tMax_; t += 10) {
        qreal x = toPx(QPointF(t, rMin_)).x();
        p.setPen(QPen(QColor(255, 255, 255, 12), 1));
        p.drawLine(QPointF(x, pl.top()), QPointF(x, pl.bottom()));
        p.setPen(Theme::textMuted());
        p.drawText(QRectF(x - 20, pl.bottom() + 6, 40, 16), Qt::AlignCenter,
                   QString::number(qRound(disp_(t))) + "°");
    }
    for (int k = 0; k <= 4; ++k) {
        double r = rMin_ + (rMax_ - rMin_) * k / 4.0;
        qreal y = toPx(QPointF(tMin_, r)).y();
        p.setPen(QPen(QColor(255, 255, 255, 12), 1, Qt::DashLine));
        p.drawLine(QPointF(pl.left(), y), QPointF(pl.right(), y));
        p.setPen(Theme::textMuted());
        p.drawText(QRectF(0, y - 8, pl.left() - 8, 16), Qt::AlignRight | Qt::AlignVCenter,
                   Theme::rpmCompact(r));
    }

    if (pts_.size() < 1) return;

    // Curve polyline extended flat to both edges.
    QVector<QPointF> poly;
    poly << toPx(QPointF(tMin_, pts_.first().y()));
    for (const auto &v : pts_) poly << toPx(v);
    poly << toPx(QPointF(tMax_, pts_.last().y()));

    QLinearGradient tg(pl.left(), 0, pl.right(), 0);
    for (int t = int(tMin_); t <= int(tMax_); t += 5)
        tg.setColorAt((t - tMin_) / (tMax_ - tMin_), Theme::tempColor(t));

    QPainterPath line;
    line.moveTo(poly[0]);
    for (int i = 1; i < poly.size(); ++i) line.lineTo(poly[i]);
    QPainterPath area = line;
    area.lineTo(pl.right(), pl.bottom());
    area.lineTo(pl.left(), pl.bottom());
    area.closeSubpath();

    // Temperature-tinted area that fades into the panel toward the bottom.
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(tg);
    p.setOpacity(0.24);
    p.drawPath(area);
    p.setOpacity(1);
    QLinearGradient fade(0, pl.top(), 0, pl.bottom());
    fade.setColorAt(0, QColor(0x15, 0x18, 0x1e, 0));
    fade.setColorAt(1, QColor(0x15, 0x18, 0x1e, 230));
    p.setBrush(fade);
    p.drawPath(area);
    p.restore();

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QBrush(tg), 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setOpacity(0.25);
    p.drawPath(line);
    p.setOpacity(1);
    p.setPen(QPen(QBrush(tg), 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(line);

    // Live operating point
    if (showLive_ && liveT_ > 0) {
        double t = std::clamp(liveT_, tMin_, tMax_);
        QPointF lp = toPx(QPointF(t, curveAt(t)));
        QColor lc = Theme::tempColor(liveT_);
        p.setPen(QPen(QColor(lc.red(), lc.green(), lc.blue(), 140), 1, Qt::DashLine));
        p.drawLine(QPointF(lp.x(), pl.bottom()), lp);
        p.drawLine(QPointF(pl.left(), lp.y()), lp);
        p.setPen(Qt::NoPen);
        QColor glow = lc; glow.setAlpha(70);
        p.setBrush(glow);
        p.drawEllipse(lp, 11, 11);
        p.setBrush(lc);
        p.drawEllipse(lp, 5, 5);
        p.setBrush(Qt::white);
        p.drawEllipse(lp, 2, 2);

        QString txt = QString("%1°  →  %2").arg(qRound(disp_(liveT_))).arg(Theme::rpm(curveAt(t)));
        p.setFont(Theme::font(11, QFont::DemiBold, true));
        QFontMetricsF fm(p.font());
        qreal w = fm.horizontalAdvance(txt) + 18;
        QRectF bub(std::clamp(lp.x() - w / 2, pl.left(), pl.right() - w), lp.y() - 34, w, 22);
        if (bub.top() < pl.top()) bub.moveTop(lp.y() + 14);
        p.setPen(QPen(Theme::border(), 1));
        p.setBrush(QColor(0x1d, 0x21, 0x29, 235));
        p.drawRoundedRect(bub, 8, 8);
        p.setPen(Theme::text());
        p.drawText(bub, Qt::AlignCenter, txt);
    }

    // Control points
    for (int i = 0; i < pts_.size(); ++i) {
        QPointF c = toPx(pts_[i]);
        QColor col = Theme::tempColor(pts_[i].x());
        bool hot = i == hover_ || i == drag_;
        p.setPen(Qt::NoPen);
        if (hot) {
            QColor g = col; g.setAlpha(80);
            p.setBrush(g);
            p.drawEllipse(c, 13, 13);
        }
        p.setBrush(QColor(0, 0, 0, 80));
        p.drawEllipse(c + QPointF(0, 1.5), hot ? 9 : 7.5, hot ? 9 : 7.5);
        p.setBrush(Qt::white);
        p.drawEllipse(c, hot ? 8.5 : 7, hot ? 8.5 : 7);
        p.setBrush(col);
        p.drawEllipse(c, 3.5, 3.5);
    }

    // Drag tooltip
    if (drag_ >= 0) {
        QPointF c = toPx(pts_[drag_]);
        QString txt = QString("%1°  ·  %2").arg(qRound(disp_(pts_[drag_].x()))).arg(Theme::rpm(pts_[drag_].y()));
        p.setFont(Theme::font(11, QFont::DemiBold, true));
        QFontMetricsF fm(p.font());
        qreal w = fm.horizontalAdvance(txt) + 18;
        QRectF bub(std::clamp(c.x() - w / 2, 4.0, width() - w - 4.0), c.y() - 38, w, 22);
        if (bub.top() < 2) bub.moveTop(c.y() + 16);
        QLinearGradient bg(bub.topLeft(), bub.bottomRight());
        bg.setColorAt(0, Theme::blue());
        bg.setColorAt(1, Theme::violet());
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(bub, 8, 8);
        p.setPen(Qt::white);
        p.drawText(bub, Qt::AlignCenter, txt);
    } else {
        p.setFont(Theme::font(10, QFont::Medium));
        p.setPen(Theme::textMuted());
        p.drawText(pl.adjusted(0, 4, -6, 0), Qt::AlignRight | Qt::AlignTop,
                   "Drag points · double-click to add · right-click to remove");
    }
}

void CurveEditor::mousePressEvent(QMouseEvent *e) {
    int i = hit(e->position());
    if (e->button() == Qt::RightButton) {
        if (i >= 0 && pts_.size() > 2) {
            pts_.remove(i);
            hover_ = -1;
            update();
            emit pointsEdited(pts_);
        }
        return;
    }
    if (e->button() == Qt::LeftButton && i >= 0) {
        drag_ = i;
        update();
    }
}

void CurveEditor::mouseMoveEvent(QMouseEvent *e) {
    if (drag_ < 0) {
        int i = hit(e->position());
        if (i != hover_) {
            hover_ = i;
            setCursor(i >= 0 ? Qt::OpenHandCursor : Qt::CrossCursor);
            update();
        }
        return;
    }
    setCursor(Qt::ClosedHandCursor);
    QPointF v = toVal(e->position());
    double lo = drag_ > 0 ? pts_[drag_ - 1].x() + 1 : tMin_;
    double hi = drag_ + 1 < pts_.size() ? pts_[drag_ + 1].x() - 1 : tMax_;
    double t = std::clamp(std::round(v.x()), lo, hi);
    double r = std::clamp(std::round(v.y() / 50.0) * 50.0, rMin_, rMax_);
    pts_[drag_] = QPointF(t, r);
    update();
}

void CurveEditor::mouseReleaseEvent(QMouseEvent *) {
    if (drag_ < 0) return;
    drag_ = -1;
    setCursor(Qt::OpenHandCursor);
    update();
    emit pointsEdited(pts_);
}

void CurveEditor::mouseDoubleClickEvent(QMouseEvent *e) {
    if (hit(e->position()) >= 0 || pts_.size() >= 8 || !plot().contains(e->position())) return;
    QPointF v = toVal(e->position());
    double t = std::clamp(std::round(v.x()), tMin_, tMax_);
    for (const auto &q : pts_)
        if (std::abs(q.x() - t) < 2) return;
    double r = std::clamp(std::round(v.y() / 50.0) * 50.0, rMin_, rMax_);
    pts_.push_back(QPointF(t, r));
    std::sort(pts_.begin(), pts_.end(), [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    update();
    emit pointsEdited(pts_);
}

void CurveEditor::leaveEvent(QEvent *) {
    hover_ = -1;
    update();
}
