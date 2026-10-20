#include "sensortree.h"
#include "appsettings.h"
#include "charts.h"
#include "fanmodel.h"
#include "history.h"
#include "macnative.h"
#include "theme.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>

static const qreal kTopH = 48;
static const qreal kChildH = 34;
static const qreal kGap = 8;

SensorTreeView::SensorTreeView(FanModel *model, QWidget *parent) : QWidget(parent), m_(model) {
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    for (const QString &id : AppSettings::instance().expandedNodes()) expanded_.insert(id);
    anim_ = new QVariantAnimation(this);
    anim_->setDuration(220);
    anim_->setEasingCurve(QEasingCurve::OutCubic);
    anim_->setStartValue(0.0);
    anim_->setEndValue(1.0);
    connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        animT_ = v.toReal();
        layoutRows();
        update();
    });
    connect(anim_, &QVariantAnimation::finished, this, [this] {
        animNode_.clear();
        animT_ = 1.0;
        layoutRows();
        update();
    });
    rebuild();
}

void SensorTreeView::rebuild() {
    layoutRows();
    update();
}

bool SensorTreeView::expanded(int node) const {
    return expanded_.contains(m_->nodes()[node].id);
}

void SensorTreeView::addRows(int node, qreal factor, qreal &y) {
    const SensorNode &n = m_->nodes()[node];
    const bool animating = !animNode_.isEmpty() && n.id == animNode_;
    const bool open = expanded(node);
    if (!(open || animating)) return;
    qreal f = factor;
    if (animating) f *= animExpanding_ ? animT_ : (1.0 - animT_);
    if (f <= 0.001) return;
    for (int c : n.children) {
        const qreal h = kChildH;
        rows_.push_back({c, y, h, f});
        y += h * f;
        addRows(c, f, y);
    }
}

void SensorTreeView::layoutRows() {
    rows_.clear();
    blocks_.clear();
    qreal y = 0;
    for (int t : m_->topLevelNodes()) {
        Block b;
        b.top = y;
        rows_.push_back({t, y, kTopH, 1.0});
        y += kTopH;
        addRows(t, 1.0, y);
        b.bottom = y;
        if (b.bottom - b.top > kTopH + 0.5) y += 6; // breathing room under children
        b.bottom = y;
        blocks_.push_back(b);
        y += kGap;
    }
    setFixedHeight(int(std::ceil(std::max<qreal>(0, y - kGap))) + 2);
}

void SensorTreeView::toggle(int node) {
    const QString id = m_->nodes()[node].id;
    animExpanding_ = !expanded_.contains(id);
    if (animExpanding_) expanded_.insert(id);
    else expanded_.remove(id);
    // addRows() keeps the animating node's children laid out (shrinking or
    // growing) until the animation finishes.
    animNode_ = id;
    anim_->stop();
    animT_ = 0;
    anim_->start();
    AppSettings::instance().setExpandedNodes(QStringList(expanded_.begin(), expanded_.end()));
}

int SensorTreeView::rowAt(const QPointF &p) const {
    for (int i = 0; i < rows_.size(); ++i) {
        const Row &r = rows_[i];
        if (r.f < 0.6) continue;
        if (p.y() >= r.y && p.y() < r.y + r.h * r.f) return i;
    }
    return -1;
}

void SensorTreeView::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    int i = rowAt(e->position());
    if (i < 0) return;
    int node = rows_[i].node;
    if (!m_->nodes()[node].children.isEmpty()) toggle(node);
}

void SensorTreeView::mouseMoveEvent(QMouseEvent *e) {
    int i = rowAt(e->position());
    if (i != hover_) {
        hover_ = i;
        bool expandable = i >= 0 && !m_->nodes()[rows_[i].node].children.isEmpty();
        setCursor(expandable ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void SensorTreeView::leaveEvent(QEvent *) {
    hover_ = -1;
    update();
}

void SensorTreeView::contextMenuEvent(QContextMenuEvent *e) {
    int i = rowAt(e->pos());
    if (i < 0) return;
    const SensorNode &n = m_->nodes()[rows_[i].node];
    const QString id = n.id;
    QMenu menu(this);
    menu.addAction(QIcon(MacNative::symbol("gauge.with.dots.needle.67percent", 16, Theme::blue())),
                   "Show on Gauge", this, [this, id] { emit showOnGauge(id); });
    menu.addAction(QIcon(MacNative::symbol("menubar.rectangle", 16, Theme::violet())),
                   "Show in Menu Bar", this, [this, id] { emit showInMenuBar(id); });
    menu.exec(e->globalPos());
}

void SensorTreeView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto &nodes = m_->nodes();
    const History &h = m_->history();
    const AppSettings &as = AppSettings::instance();
    const qreal W = width();

    // Category cards.
    for (const Block &b : blocks_) {
        QRectF r(0.5, b.top + 0.5, W - 1, b.bottom - b.top - 1);
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0, Theme::cardHi());
        g.setColorAt(1, Theme::card());
        p.setPen(QPen(Theme::border(), 1));
        p.setBrush(g);
        p.drawRoundedRect(r, 13, 13);
    }

    for (int i = 0; i < rows_.size(); ++i) {
        const Row &row = rows_[i];
        const SensorNode &n = nodes[row.node];
        const bool top = n.parent < 0;
        const qreal rowH = row.h * row.f;
        if (rowH < 1) continue;
        const QColor cat = Theme::categoryColor(n.category);

        p.save();
        p.setClipRect(QRectF(0, row.y, W, rowH));
        p.setOpacity(std::clamp(row.f * 1.2 - 0.2, 0.0, 1.0));
        const QRectF rr(0, row.y, W, row.h);

        if (i == hover_) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 9));
            p.drawRoundedRect(rr.adjusted(4, 3, -4, -3), 10, 10);
        }
        if (!top) {
            p.setPen(QPen(QColor(255, 255, 255, 13), 1));
            p.drawLine(QPointF(16, rr.top() + 0.5), QPointF(W - 16, rr.top() + 0.5));
        }

        const qreal indent = top ? 14 : 22 + (n.depth - 1) * 18;
        qreal x = indent;

        // Chevron
        if (!n.children.isEmpty()) {
            qreal angle = expanded(row.node) ? 90 : 0;
            if (n.id == animNode_) angle = animExpanding_ ? 90 * animT_ : 90 * (1 - animT_);
            QPixmap chev = MacNative::symbol("chevron.right", 11, Theme::textMuted(), 7);
            p.save();
            p.translate(x + 5.5, rr.center().y());
            p.rotate(angle);
            p.drawPixmap(QPointF(-5.5, -5.5), chev);
            p.restore();
        }
        x += 16;

        // Icon
        if (top) {
            QRectF chip(x, rr.center().y() - 15, 30, 30);
            p.setPen(QPen(QColor(cat.red(), cat.green(), cat.blue(), 70), 1));
            p.setBrush(QColor(cat.red(), cat.green(), cat.blue(), 38));
            p.drawRoundedRect(chip, 9, 9);
            p.drawPixmap(QPointF(chip.left() + 7, chip.top() + 7), MacNative::symbol(n.symbol, 16, cat, 6));
            x += 40;
        } else {
            p.drawPixmap(QPointF(x, rr.center().y() - 7), MacNative::symbol(n.symbol, 14, cat, 5));
            x += 22;
        }

        // Value (right)
        const QFont vf = Theme::font(top ? 14 : 12.5, top ? QFont::Bold : QFont::DemiBold, true);
        p.setFont(vf);
        const QString val = n.valid ? as.formatTemp(n.value, 1) : QStringLiteral("—");
        const qreal valW = 58;
        QRectF valRect(W - 16 - valW, rr.top(), valW, rr.height());
        p.setPen(n.valid ? Theme::tempColor(n.value) : Theme::textMuted());
        p.drawText(valRect, Qt::AlignRight | Qt::AlignVCenter, val);

        // Sparkline (left of value)
        const qreal spW = top ? 74 : 56, spH = top ? 22 : 16;
        QRectF sp(valRect.left() - 12 - spW, rr.center().y() - spH / 2, spW, spH);
        drawSparkline(p, sp, h, "t:" + n.id, cat, 300);

        // Label
        p.setFont(Theme::font(top ? 13.5 : 12.5, top ? QFont::DemiBold : QFont::Medium));
        p.setPen(top ? Theme::text() : Theme::textDim());
        QRectF lr(x, rr.top(), sp.left() - 8 - x, rr.height());
        QString label = p.fontMetrics().elidedText(n.label, Qt::ElideRight, int(lr.width()));
        p.drawText(lr, Qt::AlignLeft | Qt::AlignVCenter, label);
        if (top && !n.children.isEmpty() && lr.width() > 140) {
            // Child count hint
            QFontMetricsF fm(p.font());
            qreal lw = fm.horizontalAdvance(label);
            p.setFont(Theme::font(10.5, QFont::DemiBold));
            p.setPen(Theme::textMuted());
            p.drawText(QRectF(x + lw + 8, rr.top(), 60, rr.height()), Qt::AlignLeft | Qt::AlignVCenter,
                       QString::number(n.children.size()));
        }
        p.restore();
    }
}

// ================================================================ StatTile

StatTile::StatTile(const History *h, QWidget *parent) : QWidget(parent), h_(h) {
    setMinimumSize(170, 118);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(124);
}

void StatTile::setup(const QString &title, const QString &symbol, const QColor &a,
                     const QColor &b, const QString &seriesId) {
    title_ = title;
    symbol_ = symbol;
    a_ = a;
    b_ = b;
    series_ = seriesId;
    update();
}

void StatTile::setValues(const QString &big, const QString &sub, double pct) {
    big_ = big;
    sub_ = sub;
    pct_ = std::clamp(pct, 0.0, 100.0);
    update();
}

void StatTile::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0, Theme::cardHi());
    g.setColorAt(1, Theme::card());
    p.setPen(QPen(Theme::border(), 1));
    p.setBrush(g);
    p.drawRoundedRect(r, 14, 14);

    // Header
    QRectF chip(r.left() + 14, r.top() + 13, 26, 26);
    QColor ca = a_;
    ca.setAlpha(40);
    p.setPen(Qt::NoPen);
    p.setBrush(ca);
    p.drawRoundedRect(chip, 8, 8);
    p.drawPixmap(QPointF(chip.left() + 6, chip.top() + 6), MacNative::symbolGradient(symbol_, 14, a_, b_, 6));
    p.setFont(Theme::font(12, QFont::DemiBold));
    p.setPen(Theme::textDim());
    p.drawText(QRectF(chip.right() + 9, chip.top(), r.width() - 60, chip.height()),
               Qt::AlignLeft | Qt::AlignVCenter, title_);

    // Big value + subtitle
    p.setFont(Theme::font(21, QFont::Bold, true));
    p.setPen(Theme::text());
    p.drawText(QRectF(r.left() + 15, chip.bottom() + 6, r.width() - 30, 28),
               Qt::AlignLeft | Qt::AlignVCenter, big_);
    p.setFont(Theme::font(11, QFont::Medium, true));
    p.setPen(Theme::textMuted());
    p.drawText(QRectF(r.left() + 15, chip.bottom() + 33, r.width() - 30, 16),
               Qt::AlignLeft | Qt::AlignVCenter, sub_);

    // Sparkline on the right of the big value, bar along the bottom.
    if (!series_.isEmpty() && h_ && h_->size() > 2) {
        const qreal sw = r.width() * 0.42;
        drawSparkline(p, QRectF(r.right() - 14 - sw, chip.bottom() + 8, sw, 42), *h_, series_, b_,
                      300, 0, 100);
    }
    QRectF bar(r.left() + 14, r.bottom() - 15, r.width() - 28, 5);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::track());
    p.drawRoundedRect(bar, 2.5, 2.5);
    if (pct_ > 0.3) {
        QRectF fill(bar.left(), bar.top(), bar.width() * pct_ / 100.0, bar.height());
        QLinearGradient bg(bar.left(), 0, bar.right(), 0);
        bg.setColorAt(0, a_);
        bg.setColorAt(1, b_);
        p.setBrush(bg);
        p.drawRoundedRect(fill, 2.5, 2.5);
    }
}
