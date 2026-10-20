#include "prowidgets.h"
#include "fanmodel.h"
#include "macnative.h"
#include "theme.h"

#include <QAction>
#include <QEnterEvent>
#include <QFontMetricsF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

// ================================================================ helpers

static void softShadow(QPainter &p, const QRectF &r, qreal radius, const QColor &c, int layers = 4) {
    p.save();
    p.setPen(Qt::NoPen);
    for (int i = layers; i > 0; --i) {
        QColor col = c;
        col.setAlphaF(c.alphaF() * (1.0 - double(i) / (layers + 1)) / layers * 2.0);
        p.setBrush(col);
        p.drawRoundedRect(r.adjusted(-i, -i + 1, i, i + 1), radius + i, radius + i);
    }
    p.restore();
}

static QColor withAlpha(QColor c, double a) { c.setAlphaF(a); return c; }

// ================================================================ IconChip

IconChip::IconChip(const QString &symbol, const QColor &color, int size, QWidget *parent)
    : QWidget(parent), symbol_(symbol), color_(color), size_(size) {
    setFixedSize(size, size);
    setAttribute(Qt::WA_TranslucentBackground);
}

void IconChip::set(const QString &symbol, const QColor &color) {
    symbol_ = symbol;
    color_ = color;
    gradient_ = false;
    update();
}

void IconChip::setGradient(const QColor &a, const QColor &b) {
    color_ = a;
    color2_ = b;
    gradient_ = true;
    update();
}

void IconChip::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r(0.5, 0.5, size_ - 1, size_ - 1);
    const qreal rad = size_ * 0.30;
    if (gradient_) {
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0, color_);
        g.setColorAt(1, color2_);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawRoundedRect(r, rad, rad);
        QPixmap pm = MacNative::symbol(symbol_, int(size_ * 0.56), Qt::white, 6);
        p.drawPixmap(QPointF((size_ - size_ * 0.56) / 2, (size_ - size_ * 0.56) / 2), pm);
    } else {
        p.setPen(QPen(withAlpha(color_, 0.28), 1));
        p.setBrush(withAlpha(color_, 0.15));
        p.drawRoundedRect(r, rad, rad);
        QPixmap pm = MacNative::symbol(symbol_, int(size_ * 0.56), color_, 6);
        p.drawPixmap(QPointF((size_ - size_ * 0.56) / 2, (size_ - size_ * 0.56) / 2), pm);
    }
}

// ================================================================ SegmentedControl

SegmentedControl::SegmentedControl(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    anim_ = new QPropertyAnimation(this, "pos", this);
    anim_->setDuration(260);
    anim_->setEasingCurve(QEasingCurve::OutCubic);
}

void SegmentedControl::setItems(const QVector<Item> &items) {
    items_ = items;
    grads_.fill({Theme::blue(), Theme::violet()}, items.size());
    current_ = std::clamp(current_, 0, std::max(0, int(items.size()) - 1));
    pos_ = current_;
    updateGeometry();
    update();
}

void SegmentedControl::setItemGradient(int i, const QColor &a, const QColor &b) {
    if (i >= 0 && i < grads_.size()) grads_[i] = {a, b};
    update();
}

void SegmentedControl::setCompact(bool c) {
    compact_ = c;
    updateGeometry();
    update();
}

void SegmentedControl::setCurrentIndex(int i, bool animate) {
    if (items_.isEmpty()) return;
    i = std::clamp(i, 0, int(items_.size()) - 1);
    current_ = i;
    anim_->stop();
    if (animate && isVisible()) {
        anim_->setStartValue(pos_);
        anim_->setEndValue(qreal(i));
        anim_->start();
    } else {
        pos_ = i;
        update();
    }
}

QSize SegmentedControl::sizeHint() const {
    QFontMetricsF fm(Theme::font(compact_ ? 11.5 : 12.5, QFont::DemiBold));
    qreal maxW = 0;
    for (const auto &it : items_) {
        qreal w = (showText_ ? fm.horizontalAdvance(it.text) : 0) +
                  (it.symbol.isEmpty() ? 0 : (compact_ ? 14 : 16) + (showText_ ? 6 : 0)) + 24;
        maxW = std::max(maxW, w);
    }
    return QSize(int(maxW * std::max<qsizetype>(1, items_.size()) + 6), compact_ ? 30 : 38);
}

QSize SegmentedControl::minimumSizeHint() const {
    return QSize(int(items_.size()) * (compact_ ? 44 : 56), compact_ ? 30 : 38);
}

QRectF SegmentedControl::segRect(int i) const {
    const qreal pad = 3;
    const qreal w = (width() - 2 * pad) / std::max<qsizetype>(1, items_.size());
    return QRectF(pad + i * w, pad, w, height() - 2 * pad);
}

int SegmentedControl::indexAt(const QPointF &p) const {
    for (int i = 0; i < items_.size(); ++i)
        if (segRect(i).contains(p)) return i;
    return -1;
}

QPair<QColor, QColor> SegmentedControl::gradAt(qreal pos) const {
    if (grads_.isEmpty()) return {Theme::blue(), Theme::violet()};
    int a = std::clamp(int(std::floor(pos)), 0, int(grads_.size()) - 1);
    int b = std::clamp(a + 1, 0, int(grads_.size()) - 1);
    double t = pos - a;
    return {Theme::mix(grads_[a].first, grads_[b].first, t),
            Theme::mix(grads_[a].second, grads_[b].second, t)};
}

void SegmentedControl::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal rad = std::min<qreal>(11, r.height() / 2);

    // Track
    p.setPen(QPen(Theme::border(), 1));
    p.setBrush(Theme::elevated());
    p.drawRoundedRect(r, rad, rad);
    if (items_.isEmpty()) return;

    // Hover
    if (hover_ >= 0 && hover_ != current_) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 12));
        QRectF h = segRect(hover_).adjusted(1, 1, -1, -1);
        p.drawRoundedRect(h, rad - 3, rad - 3);
    }

    // Sliding highlight
    const qreal pad = 3;
    const qreal w = (width() - 2 * pad) / items_.size();
    QRectF hl(pad + pos_ * w, pad, w, height() - 2 * pad);
    auto g2 = gradAt(pos_);
    softShadow(p, hl, rad - 3, withAlpha(g2.second, 0.55), 5);
    QLinearGradient g(hl.topLeft(), hl.bottomRight());
    g.setColorAt(0, g2.first);
    g.setColorAt(1, g2.second);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(hl, rad - 3, rad - 3);
    // Gloss
    QLinearGradient gloss(hl.topLeft(), hl.bottomLeft());
    gloss.setColorAt(0, QColor(255, 255, 255, 45));
    gloss.setColorAt(0.5, QColor(255, 255, 255, 0));
    p.setBrush(gloss);
    p.drawRoundedRect(hl, rad - 3, rad - 3);

    // Labels
    const QFont f = Theme::font(compact_ ? 11.5 : 12.5, QFont::DemiBold);
    p.setFont(f);
    QFontMetricsF fm(f);
    const int iconPx = compact_ ? 13 : 15;
    for (int i = 0; i < items_.size(); ++i) {
        const auto &it = items_[i];
        const double sel = std::max(0.0, 1.0 - std::abs(pos_ - i));
        QColor c = Theme::mix(i == hover_ ? Theme::text() : Theme::textDim(), Qt::white, sel);
        QRectF sr = segRect(i);
        qreal tw = showText_ ? fm.horizontalAdvance(it.text) : 0;
        qreal iw = it.symbol.isEmpty() ? 0 : iconPx + (showText_ ? 6 : 0);
        qreal x = sr.center().x() - (tw + iw) / 2;
        if (!it.symbol.isEmpty()) {
            QPixmap pm = MacNative::symbol(it.symbol, iconPx, c, 6);
            p.drawPixmap(QPointF(x, sr.center().y() - iconPx / 2.0), pm);
            x += iw;
        }
        if (showText_) {
            p.setPen(c);
            p.drawText(QRectF(x, sr.top(), tw + 2, sr.height()), Qt::AlignVCenter | Qt::AlignLeft, it.text);
        }
    }
}

void SegmentedControl::mousePressEvent(QMouseEvent *e) {
    int i = indexAt(e->position());
    if (i < 0 || i == current_) return;
    setCurrentIndex(i, true);
    emit currentChanged(i);
}

void SegmentedControl::mouseMoveEvent(QMouseEvent *e) {
    int i = indexAt(e->position());
    if (i != hover_) {
        hover_ = i;
        if (i >= 0 && i < items_.size()) setToolTip(items_[i].tip);
        update();
    }
}

void SegmentedControl::leaveEvent(QEvent *) {
    hover_ = -1;
    update();
}

// ================================================================ ProSlider

ProSlider::ProSlider(QWidget *parent) : QWidget(parent), a_(Theme::blue()), b_(Theme::blueLight()) {
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(30);
    knobAnim_ = new QPropertyAnimation(this, "knob", this);
    knobAnim_->setDuration(160);
    knobAnim_->setEasingCurve(QEasingCurve::OutCubic);
}

void ProSlider::setRange(double mn, double mx) {
    min_ = mn;
    max_ = mx > mn ? mx : mn + 1;
    value_ = std::clamp(value_, min_, max_);
    update();
}

void ProSlider::setValue(double v) {
    v = std::clamp(v, min_, max_);
    if (dragging_ || std::abs(v - value_) < 1e-9) return;
    value_ = v;
    update();
}

QRectF ProSlider::track() const {
    const qreal kr = 11;
    return QRectF(kr, height() / 2.0 - 3, width() - 2 * kr, 6);
}

double ProSlider::toValue(double x) const {
    QRectF t = track();
    double f = std::clamp((x - t.left()) / std::max<qreal>(1, t.width()), 0.0, 1.0);
    double v = min_ + f * (max_ - min_);
    if (step_ > 0) v = min_ + std::round((v - min_) / step_) * step_;
    return std::clamp(v, min_, max_);
}

double ProSlider::toX(double v) const {
    QRectF t = track();
    return t.left() + (v - min_) / (max_ - min_) * t.width();
}

void ProSlider::animateKnob(qreal to) {
    knobAnim_->stop();
    knobAnim_->setStartValue(knob_);
    knobAnim_->setEndValue(to);
    knobAnim_->start();
}

void ProSlider::setFromUser(double v) {
    if (std::abs(v - value_) < 1e-9) return;
    value_ = v;
    update();
    emit valueChanged(v);
}

void ProSlider::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) p.setOpacity(0.45);
    const QRectF t = track();
    const qreal x = toX(value_);

    p.setPen(Qt::NoPen);
    p.setBrush(Theme::track());
    p.drawRoundedRect(t, 3, 3);

    QRectF fill(t.left(), t.top(), std::max<qreal>(0, x - t.left()), t.height());
    if (fill.width() > 0.5) {
        softShadow(p, fill, 3, withAlpha(b_, 0.35), 3);
        QLinearGradient g(t.left(), 0, t.right(), 0);
        g.setColorAt(0, a_);
        g.setColorAt(1, b_);
        p.setBrush(g);
        p.drawRoundedRect(fill, 3, 3);
    }

    if (showMarker_ && marker_ >= min_ && marker_ <= max_) {
        qreal mx = toX(marker_);
        p.setBrush(QColor(255, 255, 255, 150));
        p.drawRoundedRect(QRectF(mx - 1, t.top() - 4, 2, t.height() + 8), 1, 1);
    }

    const qreal r = 9 * knob_;
    QPointF c(x, height() / 2.0);
    p.setBrush(QColor(0, 0, 0, 60));
    p.drawEllipse(c + QPointF(0, 1.5), r + 1.5, r + 1.5);
    p.setBrush(withAlpha(b_, 0.25 * (knob_ - 1.0) * 4));
    p.drawEllipse(c, r + 5, r + 5);
    p.setBrush(Qt::white);
    p.drawEllipse(c, r, r);
    QLinearGradient dot(c.x() - 4, c.y() - 4, c.x() + 4, c.y() + 4);
    dot.setColorAt(0, a_);
    dot.setColorAt(1, b_);
    p.setBrush(dot);
    p.drawEllipse(c, 3.5, 3.5);
}

void ProSlider::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton || !isEnabled()) return;
    dragging_ = true;
    animateKnob(1.25);
    setFromUser(toValue(e->position().x()));
}

void ProSlider::mouseMoveEvent(QMouseEvent *e) {
    if (dragging_) setFromUser(toValue(e->position().x()));
}

void ProSlider::mouseReleaseEvent(QMouseEvent *e) {
    if (!dragging_) return;
    setFromUser(toValue(e->position().x()));
    dragging_ = false;
    animateKnob(hover_ ? 1.12 : 1.0);
    emit valueCommitted(value_);
}

void ProSlider::wheelEvent(QWheelEvent *e) {
    if (!isEnabled()) return;
    const double dir = e->angleDelta().y() > 0 ? 1 : (e->angleDelta().y() < 0 ? -1 : 0);
    if (dir == 0) return;
    double s = step_ > 0 ? step_ : (max_ - min_) / 100.0;
    setFromUser(std::clamp(value_ + dir * s * 2, min_, max_));
    emit valueCommitted(value_);
    e->accept();
}

void ProSlider::enterEvent(QEnterEvent *) { hover_ = true; if (!dragging_) animateKnob(1.12); }
void ProSlider::leaveEvent(QEvent *) { hover_ = false; if (!dragging_) animateKnob(1.0); }

// ================================================================ RangeSlider

RangeSlider::RangeSlider(QWidget *parent) : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(54);
    fmt_ = [](double v) { return QString::number(qRound(v)) + "°"; };
}

void RangeSlider::setRange(double mn, double mx) {
    min_ = mn;
    max_ = mx > mn ? mx : mn + 1;
    update();
}

void RangeSlider::setValues(double lo, double hi) {
    if (active_ >= 0) return;
    lo_ = std::clamp(std::min(lo, hi), min_, max_);
    hi_ = std::clamp(std::max(lo, hi), min_, max_);
    update();
}

QRectF RangeSlider::track() const { return QRectF(12, 15, width() - 24, 6); }

double RangeSlider::toValue(double x) const {
    QRectF t = track();
    double f = std::clamp((x - t.left()) / std::max<qreal>(1, t.width()), 0.0, 1.0);
    return std::round(min_ + f * (max_ - min_));
}

double RangeSlider::toX(double v) const {
    QRectF t = track();
    return t.left() + (v - min_) / (max_ - min_) * t.width();
}

void RangeSlider::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) p.setOpacity(0.45);
    const QRectF t = track();
    const qreal xl = toX(lo_), xh = toX(hi_);

    p.setPen(Qt::NoPen);
    p.setBrush(Theme::track());
    p.drawRoundedRect(t, 3, 3);

    QRectF sel(xl, t.top(), std::max<qreal>(1, xh - xl), t.height());
    QLinearGradient g(xl, 0, xh, 0);
    g.setColorAt(0, Theme::tempColor(lo_));
    g.setColorAt(1, Theme::tempColor(hi_));
    softShadow(p, sel, 3, withAlpha(Theme::tempColor((lo_ + hi_) / 2), 0.35), 3);
    p.setBrush(g);
    p.drawRoundedRect(sel, 3, 3);

    if (showMarker_) {
        double mv = std::clamp(marker_, min_, max_);
        qreal mx = toX(mv);
        QColor mc = Theme::tempColor(marker_);
        p.setBrush(withAlpha(mc, 0.25));
        p.drawEllipse(QPointF(mx, t.center().y()), 9, 9);
        p.setBrush(mc);
        p.drawRoundedRect(QRectF(mx - 1.5, t.top() - 7, 3, t.height() + 14), 1.5, 1.5);
    }

    auto knob = [&](qreal x, double v, bool active) {
        QPointF c(x, t.center().y());
        qreal r = active ? 10.5 : 9;
        p.setBrush(QColor(0, 0, 0, 70));
        p.drawEllipse(c + QPointF(0, 1.5), r + 1.5, r + 1.5);
        p.setBrush(Qt::white);
        p.drawEllipse(c, r, r);
        p.setBrush(Theme::tempColor(v));
        p.drawEllipse(c, 3.5, 3.5);
    };
    knob(xl, lo_, active_ == 0);
    knob(xh, hi_, active_ == 1);

    // Value labels under the knobs (pushed apart if they would collide).
    QFont f = Theme::font(11.5, QFont::DemiBold, true);
    p.setFont(f);
    QFontMetricsF fm(f);
    QString sl = fmt_(lo_), sh = fmt_(hi_);
    qreal wl = fm.horizontalAdvance(sl) + 4, wh = fm.horizontalAdvance(sh) + 4;
    qreal cl = xl, ch = xh;
    qreal minDist = (wl + wh) / 2 + 6;
    if (ch - cl < minDist) {
        qreal mid = (cl + ch) / 2;
        cl = mid - minDist / 2;
        ch = mid + minDist / 2;
    }
    cl = std::clamp(cl, wl / 2, width() - wl / 2);
    ch = std::clamp(ch, wh / 2, width() - wh / 2);
    p.setPen(Theme::tempColor(lo_));
    p.drawText(QRectF(cl - wl / 2, 30, wl, 18), Qt::AlignCenter, sl);
    p.setPen(Theme::tempColor(hi_));
    p.drawText(QRectF(ch - wh / 2, 30, wh, 18), Qt::AlignCenter, sh);
}

void RangeSlider::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton || !isEnabled()) return;
    qreal x = e->position().x();
    qreal dl = std::abs(x - toX(lo_)), dh = std::abs(x - toX(hi_));
    active_ = (dl < dh || (dl == dh && x < toX(lo_))) ? 0 : 1;
    mouseMoveEvent(e);
}

void RangeSlider::mouseMoveEvent(QMouseEvent *e) {
    if (active_ < 0) return;
    double v = toValue(e->position().x());
    if (active_ == 0) lo_ = std::clamp(v, min_, hi_ - gap_);
    else hi_ = std::clamp(v, lo_ + gap_, max_);
    update();
    emit rangeChanged(lo_, hi_);
}

void RangeSlider::mouseReleaseEvent(QMouseEvent *) {
    if (active_ < 0) return;
    active_ = -1;
    update();
    emit rangeCommitted(lo_, hi_);
}

// ================================================================ ToggleSwitch

ToggleSwitch::ToggleSwitch(QWidget *parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(44, 26);
    anim_ = new QPropertyAnimation(this, "offset", this);
    anim_->setDuration(180);
    anim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(this, &QAbstractButton::toggled, this, [this](bool on) {
        anim_->stop();
        anim_->setStartValue(offset_);
        anim_->setEndValue(on ? 1.0 : 0.0);
        anim_->start();
    });
}

void ToggleSwitch::setCheckedSilently(bool on) {
    blockSignals(true);
    setChecked(on);
    blockSignals(false);
    anim_->stop();
    offset_ = on ? 1.0 : 0.0;
    update();
}

void ToggleSwitch::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) p.setOpacity(0.45);
    QRectF r(1, 1, width() - 2, height() - 2);
    const qreal rad = r.height() / 2;
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::elevatedHi());
    p.drawRoundedRect(r, rad, rad);
    if (offset_ > 0.01) {
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0, Theme::blue());
        g.setColorAt(1, Theme::violet());
        p.setOpacity((isEnabled() ? 1.0 : 0.45) * offset_);
        p.setBrush(g);
        p.drawRoundedRect(r, rad, rad);
        p.setOpacity(isEnabled() ? 1.0 : 0.45);
    }
    const qreal kr = rad - 3;
    QPointF c(r.left() + rad + offset_ * (r.width() - 2 * rad), r.center().y());
    p.setBrush(QColor(0, 0, 0, 60));
    p.drawEllipse(c + QPointF(0, 1), kr + 0.5, kr + 0.5);
    p.setBrush(Qt::white);
    p.drawEllipse(c, kr, kr);
}

// ================================================================ ProButton

ProButton::ProButton(const QString &text, const QString &symbol, Variant v, QWidget *parent)
    : QAbstractButton(parent), symbol_(symbol), variant_(v), a_(Theme::blue()), b_(Theme::violet()) {
    setText(text);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    anim_ = new QPropertyAnimation(this, "hover", this);
    anim_->setDuration(140);
}

QSize ProButton::sizeHint() const {
    if (variant_ == IconOnly) return QSize(32, 32);
    QFontMetricsF fm(Theme::font(12.5, QFont::DemiBold));
    int w = int(fm.horizontalAdvance(text())) + (symbol_.isEmpty() ? 0 : 21) + 28;
    return QSize(w, 34);
}

void ProButton::enterEvent(QEnterEvent *) {
    anim_->stop();
    anim_->setStartValue(hover_);
    anim_->setEndValue(1.0);
    anim_->start();
}

void ProButton::leaveEvent(QEvent *) {
    anim_->stop();
    anim_->setStartValue(hover_);
    anim_->setEndValue(0.0);
    anim_->start();
}

void ProButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) p.setOpacity(0.45);
    QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    const qreal rad = variant_ == IconOnly ? r.height() / 2 : 10;

    QColor fg = Theme::text();
    p.setPen(Qt::NoPen);
    switch (variant_) {
    case Primary: {
        if (hover_ > 0) softShadow(p, r, rad, withAlpha(b_, 0.45 * hover_), 5);
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0, Theme::mix(a_, Qt::white, 0.10 * hover_));
        g.setColorAt(1, Theme::mix(b_, Qt::white, 0.10 * hover_));
        p.setBrush(g);
        p.drawRoundedRect(r, rad, rad);
        QLinearGradient gloss(r.topLeft(), r.bottomLeft());
        gloss.setColorAt(0, QColor(255, 255, 255, 40));
        gloss.setColorAt(0.5, QColor(255, 255, 255, 0));
        p.setBrush(gloss);
        p.drawRoundedRect(r, rad, rad);
        fg = Qt::white;
        break;
    }
    case Ghost:
        p.setBrush(withAlpha(Theme::elevatedHi(), 0.9 * hover_));
        p.drawRoundedRect(r, rad, rad);
        fg = Theme::mix(Theme::textDim(), Theme::text(), hover_);
        break;
    case Danger:
        p.setPen(QPen(withAlpha(Theme::red(), 0.35 + 0.3 * hover_), 1));
        p.setBrush(withAlpha(Theme::red(), 0.08 + 0.08 * hover_));
        p.drawRoundedRect(r, rad, rad);
        fg = Theme::red();
        break;
    case IconOnly:
    case Secondary:
    default:
        p.setPen(QPen(Theme::mix(Theme::border(), Theme::textMuted(), 0.4 * hover_), 1));
        p.setBrush(Theme::mix(Theme::elevated(), Theme::elevatedHi(), hover_));
        p.drawRoundedRect(r, rad, rad);
        fg = variant_ == IconOnly ? Theme::mix(Theme::textDim(), Theme::text(), hover_) : Theme::text();
        break;
    }
    if (isDown()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 45));
        p.drawRoundedRect(r, rad, rad);
    }

    const QFont f = Theme::font(12.5, QFont::DemiBold);
    p.setFont(f);
    QFontMetricsF fm(f);
    const int iconPx = variant_ == IconOnly ? 15 : 14;
    qreal tw = text().isEmpty() ? 0 : fm.horizontalAdvance(text());
    qreal iw = symbol_.isEmpty() ? 0 : iconPx + (tw > 0 ? 7 : 0);
    qreal x = r.center().x() - (tw + iw) / 2;
    if (!symbol_.isEmpty()) {
        p.drawPixmap(QPointF(x, r.center().y() - iconPx / 2.0),
                     MacNative::symbol(symbol_, iconPx, fg, 6));
        x += iw;
    }
    if (tw > 0) {
        p.setPen(fg);
        p.drawText(QRectF(x, r.top(), tw + 2, r.height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }
}

// ================================================================ SourcePicker

SourcePicker::SourcePicker(FanModel *model, QWidget *parent)
    : ProButton(QString(), QString(), Secondary, parent), model_(model) {
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(this, &QAbstractButton::clicked, this, &SourcePicker::openMenu);
    refreshText();
}

void SourcePicker::setSource(const QString &id) {
    source_ = id;
    refreshText();
}

void SourcePicker::refreshText() {
    const QString t = model_->sourceLabel(source_) + QString::fromUtf8("  ▾");
    if (t == text()) return; // avoid relayout on every tick
    setText(t);
    setSymbol(model_->sourceSymbol(source_));
    updateGeometry();
    update();
}

void SourcePicker::openMenu() {
    QMenu menu(this);
    auto add = [&](QMenu *m, const QString &id, const QString &label, const QString &symbol,
                   const QColor &c) {
        QAction *a = m->addAction(QIcon(MacNative::symbol(symbol, 16, c)), label);
        a->setCheckable(true);
        a->setChecked(id == source_);
        connect(a, &QAction::triggered, this, [this, id] {
            setSource(id);
            emit sourceChanged(id);
        });
    };
    auto withTemp = [this](const QString &label, const QString &id) {
        bool ok = false;
        double v = model_->nodeValue(id, &ok);
        return ok ? QString("%1  ·  %2").arg(label, QString::number(qRound(v)) + "°") : label;
    };

    if (allowAuto_)
        add(&menu, "__auto__", "Automatic — hottest of CPU / GPU", "sparkles", Theme::violet());
    if (allowHottest_)
        add(&menu, "__hottest__", "Hottest Sensor", "flame.fill", Theme::orange());
    if (allowAuto_ || allowHottest_) menu.addSeparator();

    std::function<void(QMenu *, int)> addNode = [&](QMenu *m, int idx) {
        const SensorNode &n = model_->nodes()[idx];
        const QColor c = Theme::categoryColor(n.category);
        if (n.children.isEmpty()) {
            add(m, n.id, withTemp(n.label, n.id), n.symbol, c);
            return;
        }
        QMenu *sub = m->addMenu(QIcon(MacNative::symbol(n.symbol, 16, c)), withTemp(n.label, n.id));
        add(sub, n.id, withTemp(n.depth == 0 ? n.label + " (hottest)" : n.label + " (average)", n.id),
            n.symbol, c);
        sub->addSeparator();
        for (int ch : n.children) addNode(sub, ch);
    };
    for (int t : model_->topLevelNodes()) addNode(&menu, t);
    menu.exec(mapToGlobal(QPoint(0, height() + 4)));
}

// ================================================================ LegendChip

LegendChip::LegendChip(const QString &text, const QColor &color, QWidget *parent)
    : QAbstractButton(parent), color_(color) {
    setText(text);
    setCheckable(true);
    setChecked(true);
    setCursor(Qt::PointingHandCursor);
}

QSize LegendChip::sizeHint() const {
    QFontMetricsF fm(Theme::font(11.5, QFont::DemiBold));
    return QSize(int(fm.horizontalAdvance(text())) + 34, 26);
}

void LegendChip::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal rad = r.height() / 2;
    const bool on = isChecked();
    p.setPen(QPen(on ? withAlpha(color_, 0.45) : Theme::border(), 1));
    p.setBrush(on ? withAlpha(color_, 0.14) : QColor(0, 0, 0, 0));
    p.drawRoundedRect(r, rad, rad);
    QPointF dot(r.left() + 14, r.center().y());
    if (on) {
        p.setPen(Qt::NoPen);
        p.setBrush(color_);
        p.drawEllipse(dot, 4, 4);
    } else {
        p.setPen(QPen(Theme::textMuted(), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(dot, 3.5, 3.5);
    }
    p.setFont(Theme::font(11.5, QFont::DemiBold));
    p.setPen(on ? Theme::text() : Theme::textMuted());
    p.drawText(r.adjusted(24, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, text());
}
// ================================================================ MiniBar

MiniBar::MiniBar(QWidget *parent) : QWidget(parent) { setFixedHeight(32); }

void MiniBar::set(const QString &label, const QString &value, double pct, const QColor &a, const QColor &b) {
    label_ = label;
    value_ = value;
    pct_ = std::clamp(pct, 0.0, 100.0);
    a_ = a;
    b_ = b;
    update();
}

void MiniBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal w = width();
    p.setFont(Theme::font(11, QFont::DemiBold));
    p.setPen(Theme::textDim());
    p.drawText(QRectF(0, 0, w, 15), Qt::AlignLeft | Qt::AlignVCenter, label_);
    p.setFont(Theme::font(11, QFont::Bold, true));
    p.setPen(Theme::text());
    p.drawText(QRectF(0, 0, w, 15), Qt::AlignRight | Qt::AlignVCenter, value_);
    QRectF track(0, 21, w, 5);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::track());
    p.drawRoundedRect(track, 2.5, 2.5);
    if (pct_ > 0.3) {
        QLinearGradient g(0, 0, w, 0);
        g.setColorAt(0, a_);
        g.setColorAt(1, b_);
        p.setBrush(g);
        p.drawRoundedRect(QRectF(0, 21, w * pct_ / 100.0, 5), 2.5, 2.5);
    }
}


// ================================================================ PanelStack

PanelStack::PanelStack(QWidget *parent) : QWidget(parent) {
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
}

void PanelStack::addWidget(QWidget *w) {
    pages_.push_back(w);
    layout()->addWidget(w);
    w->setVisible(pages_.size() - 1 == current_);
    if (current_ < 0) setCurrentIndex(0);
}

void PanelStack::setCurrentIndex(int i) {
    if (i < 0 || i >= pages_.size()) return;
    current_ = i;
    for (int k = 0; k < pages_.size(); ++k) pages_[k]->setVisible(k == i);
    updateGeometry();
}
