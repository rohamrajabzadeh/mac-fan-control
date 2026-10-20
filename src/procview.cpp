#include "procview.h"
#include "macnative.h"
#include "procmon.h"
#include "theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <algorithm>

ProcessListView::ProcessListView(ProcessMonitor *pm, int maxRows, bool compact, bool allowQuit,
                                 QWidget *parent)
    : QWidget(parent), pm_(pm), maxRows_(maxRows), compact_(compact), allowQuit_(allowQuit) {
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(int(rowH() * maxRows));
    connect(pm, &ProcessMonitor::updated, this, qOverload<>(&QWidget::update));
}

void ProcessListView::showEvent(QShowEvent *) { pm_->retain(); }
void ProcessListView::hideEvent(QHideEvent *) { pm_->release(); }

int ProcessListView::rowAt(const QPointF &p) const {
    int i = int(p.y() / rowH());
    return (i >= 0 && i < std::min<int>(maxRows_, pm_->top().size())) ? i : -1;
}

QRectF ProcessListView::quitRect(int row) const {
    const qreal h = rowH();
    return QRectF(width() - 92, row * h + (h - 24) / 2, 84, 24);
}

void ProcessListView::mouseMoveEvent(QMouseEvent *e) {
    int r = rowAt(e->position());
    if (r != hover_) {
        hover_ = r;
        update();
    }
    bool overQuit = false;
    if (allowQuit_ && r >= 0 && r < pm_->top().size()) {
        const ProcGroup &g = pm_->top()[r];
        overQuit = g.isApp() && !g.self && quitRect(r).contains(e->position());
    }
    setCursor(overQuit ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void ProcessListView::leaveEvent(QEvent *) {
    hover_ = -1;
    update();
}

void ProcessListView::mousePressEvent(QMouseEvent *e) {
    if (!allowQuit_ || e->button() != Qt::LeftButton) return;
    int r = rowAt(e->position());
    if (r < 0 || r >= pm_->top().size() || !quitRect(r).contains(e->position())) return;
    const ProcGroup g = pm_->top()[r];
    if (!g.isApp() || g.self) return;
    // Two-step: first click arms "Confirm", second click within 3 s quits.
    if (armedKey_ == g.key && armedAt_.isValid() && armedAt_.elapsed() < 3000) {
        ProcessMonitor::quitApp(g);
        armedKey_.clear();
    } else {
        armedKey_ = g.key;
        armedAt_.start();
        QTimer::singleShot(3100, this, qOverload<>(&QWidget::update)); // disarm visually
    }
    update();
}

void ProcessListView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto &top = pm_->top();
    const qreal h = rowH(), W = width();

    if (top.isEmpty()) {
        p.setPen(Theme::textMuted());
        p.setFont(Theme::font(12, QFont::Medium));
        p.drawText(rect(), Qt::AlignCenter, pm_->hasData() ? "Nothing busy right now" : "Measuring…");
        return;
    }
    const double scale = std::max(100.0, top.first().cpu);
    const int rows = std::min<int>(maxRows_, top.size());
    const int iconPx = compact_ ? 18 : 24;

    for (int i = 0; i < rows; ++i) {
        const ProcGroup &g = top[i];
        const QRectF rr(0, i * h, W, h);
        if (i == hover_ && !compact_) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 9));
            p.drawRoundedRect(rr.adjusted(0, 2, 0, -2), 10, 10);
        }
        const qreal pad = compact_ ? 0 : 10;
        qreal x = pad;

        // Icon
        QPixmap icon = g.iconPath.isEmpty() ? QPixmap() : MacNative::fileIcon(g.iconPath, iconPx);
        if (icon.isNull())
            icon = MacNative::symbol("gearshape.fill", iconPx - 4, Theme::textMuted());
        p.drawPixmap(QPointF(x, rr.center().y() - iconPx / 2.0), icon);
        x += iconPx + (compact_ ? 8 : 12);

        // CPU value (right)
        const QColor load = Theme::loadColor(std::min(100.0, g.cpu), Theme::blueLight());
        p.setFont(Theme::font(compact_ ? 11.5 : 13, QFont::Bold, true));
        p.setPen(load);
        const qreal valW = compact_ ? 50 : 62;
        const bool showQuit = allowQuit_ && i == hover_ && g.isApp() && !g.self;
        QRectF valRect(W - pad - valW, rr.top(), valW, h);
        if (showQuit) valRect.moveRight(quitRect(i).left() - 8);
        p.drawText(valRect, Qt::AlignRight | Qt::AlignVCenter, QString::number(g.cpu, 'f', g.cpu < 10 ? 1 : 0) + "%");

        // Share bar (behind the name, under it)
        const qreal barLeft = x, barRight = valRect.left() - 12;
        const qreal barW = std::max<qreal>(0, barRight - barLeft);
        if (!compact_) {
            QRectF track(barLeft, rr.bottom() - 11, barW, 3);
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::track());
            p.drawRoundedRect(track, 1.5, 1.5);
            QLinearGradient grad(barLeft, 0, barRight, 0);
            grad.setColorAt(0, Theme::blue());
            grad.setColorAt(1, load);
            p.setBrush(grad);
            p.drawRoundedRect(QRectF(barLeft, track.top(), barW * std::min(1.0, g.cpu / scale), 3), 1.5, 1.5);
        }

        // Name (+ process count)
        p.setFont(Theme::font(compact_ ? 12 : 13, QFont::DemiBold));
        p.setPen(Theme::text());
        QString name = g.name;
        const QString extra = g.count > 1 ? QString("  ×%1").arg(g.count) : QString();
        QFontMetricsF fm(p.font());
        const qreal nameY = compact_ ? rr.top() : rr.top() - 5;
        const qreal avail = barW - fm.horizontalAdvance(extra);
        name = fm.elidedText(name, Qt::ElideRight, avail);
        p.drawText(QRectF(x, nameY, barW, h), Qt::AlignLeft | Qt::AlignVCenter, name);
        if (!extra.isEmpty()) {
            qreal nw = fm.horizontalAdvance(name);
            p.setFont(Theme::font(compact_ ? 11 : 11.5, QFont::Medium, true));
            p.setPen(Theme::textMuted());
            p.drawText(QRectF(x + nw, nameY, 60, h), Qt::AlignLeft | Qt::AlignVCenter, extra);
        }

        // Quit button
        if (showQuit) {
            const bool armed = armedKey_ == g.key && armedAt_.isValid() && armedAt_.elapsed() < 3000;
            QRectF qr = quitRect(i);
            QColor c = armed ? Theme::red() : Theme::textDim();
            p.setPen(QPen(armed ? QColor(255, 90, 95, 150) : Theme::border(), 1));
            p.setBrush(armed ? QColor(255, 90, 95, 40) : Theme::elevated());
            p.drawRoundedRect(qr, 8, 8);
            p.setFont(Theme::font(11.5, QFont::DemiBold));
            p.setPen(c);
            p.drawText(qr, Qt::AlignCenter, armed ? "Confirm" : "Quit");
        }
    }
}
