#include "traypopup.h"
#include "appsettings.h"
#include "fancard.h"
#include "fanmodel.h"
#include "macnative.h"
#include "procmon.h"
#include "procview.h"
#include "prowidgets.h"
#include "theme.h"

#include <QApplication>
#include <QDebug>
#include <QCursor>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QScreen>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <cmath>

static const int kPad = 16; // room for the drawn shadow

// ================================================================ TrayPopup

static QFrame *divider() {
    auto *d = new QFrame;
    d->setObjectName("popupDivider");
    d->setFixedHeight(1);
    return d;
}

static QLabel *small(const QString &t, const char *obj = "popupNote") {
    auto *l = new QLabel(t);
    l->setObjectName(obj);
    l->setWordWrap(true);
    return l;
}

TrayPopup::TrayPopup(FanModel *model, ProcessMonitor *procs, QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                          Qt::NoDropShadowWindowHint),
      m_(model) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setObjectName("trayPopup");
    setFixedWidth(348 + kPad * 2);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(kPad, kPad - 4, kPad, kPad + 4);
    auto *cardW = new QWidget;
    cardW->setObjectName("popupCard");
    outer->addWidget(cardW);
    auto *v = new QVBoxLayout(cardW);
    v->setContentsMargins(16, 15, 16, 14);
    v->setSpacing(12);

    // Header
    auto *head = new QHBoxLayout;
    head->setSpacing(11);
    chip_ = new IconChip("fan.fill", Theme::blue(), 36);
    chip_->setGradient(Theme::blue(), Theme::violet());
    head->addWidget(chip_);
    auto *hb = new QVBoxLayout;
    hb->setSpacing(1);
    auto *title = new QLabel("MacFans");
    title->setObjectName("popupTitle");
    modeLine_ = new QLabel;
    modeLine_->setObjectName("popupSub");
    hb->addWidget(title);
    hb->addWidget(modeLine_);
    head->addLayout(hb, 1);
    rpm_ = new QLabel("—");
    rpm_->setObjectName("popupRpm");
    rpm_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    head->addWidget(rpm_);
    v->addLayout(head);

    // Status strip: thermal pressure · power · clock
    auto *strip = new QHBoxLayout;
    strip->setSpacing(8);
    pressure_ = new QLabel;
    pressure_->setObjectName("popupPill");
    strip->addWidget(pressure_);
    powerLine_ = new QLabel;
    powerLine_->setObjectName("popupNote");
    strip->addWidget(powerLine_, 1);
    v->addLayout(strip);

    v->addWidget(divider());

    // Temperatures (top-level groups) in two columns
    auto *temps = new QGridLayout;
    temps->setHorizontalSpacing(14);
    temps->setVerticalSpacing(6);
    int k = 0;
    for (int t : m_->topLevelNodes()) {
        const SensorNode &n = m_->nodes()[t];
        auto *cell = new QHBoxLayout;
        cell->setSpacing(7);
        auto *ic = new QLabel;
        ic->setPixmap(MacNative::symbol(n.symbol, 13, Theme::categoryColor(n.category)));
        cell->addWidget(ic);
        auto *name = new QLabel(n.label);
        name->setObjectName("popupTempName");
        cell->addWidget(name);
        cell->addStretch();
        auto *val = new QLabel("—");
        val->setObjectName("popupTempVal");
        cell->addWidget(val);
        temps->addLayout(cell, k / 2, k % 2);
        tempVals_ << val;
        tempIds_ << n.id;
        ++k;
    }
    v->addLayout(temps);

    v->addWidget(divider());

    auto *phd = new QLabel("WHAT'S HEATING YOUR MAC");
    phd->setObjectName("popupCap");
    v->addWidget(phd);
    v->addWidget(new ProcessListView(procs, 3, true, false));

    v->addWidget(divider());

    // System bars
    auto *sys = new QGridLayout;
    sys->setHorizontalSpacing(16);
    sys->setVerticalSpacing(6);
    cpu_ = new MiniBar;
    mem_ = new MiniBar;
    swap_ = new MiniBar;
    disk_ = new MiniBar;
    sys->addWidget(cpu_, 0, 0);
    sys->addWidget(mem_, 0, 1);
    sys->addWidget(swap_, 1, 0);
    sys->addWidget(disk_, 1, 1);
    v->addLayout(sys);

    v->addWidget(divider());

    // Modes
    modes_ = new SegmentedControl;
    modes_->setCompact(true);
    QVector<SegmentedControl::Item> items;
    for (int i = 0; i < 5; ++i) {
        ModeStyle st = modeStyle(FanMode(i));
        items.push_back({st.name, QString(), st.name});
    }
    modes_->setItems(items);
    for (int i = 0; i < 5; ++i) {
        ModeStyle st = modeStyle(FanMode(i));
        modes_->setItemGradient(i, st.a, st.b);
    }
    v->addWidget(modes_);

    panels_ = new PanelStack;
    // 0 Auto
    panels_->addWidget(small("macOS is controlling the fan. Pick a mode to take over."));
    // 1 Custom
    {
        auto *w = new QWidget;
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(10);
        slider_ = new ProSlider;
        slider_->setStep(50);
        if (!m_->fans().isEmpty())
            slider_->setRange(m_->fans().first().minRpm, m_->fans().first().maxRpm);
        h->addWidget(slider_, 1);
        sliderVal_ = new QLabel;
        sliderVal_->setObjectName("popupAccent");
        sliderVal_->setMinimumWidth(78);
        sliderVal_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        h->addWidget(sliderVal_);
        panels_->addWidget(w);
        connect(slider_, &ProSlider::valueChanged, this, [this](double v) {
            sliderVal_->setText(Theme::rpm(v));
            if (!syncing_) push();
        });
    }
    // 2 Curve
    {
        auto *w = new QWidget;
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(10);
        curveLine_ = small(QString());
        h->addWidget(curveLine_, 1);
        auto *edit = new ProButton("Edit", "slider.horizontal.3", ProButton::Secondary);
        edit->setFixedHeight(28);
        connect(edit, &QAbstractButton::clicked, this, [this] { dismiss(); emit openWindowRequested(0); });
        h->addWidget(edit);
        panels_->addWidget(w);
    }
    // 3 Smart
    {
        auto *w = new QWidget;
        auto *vb = new QVBoxLayout(w);
        vb->setContentsMargins(0, 0, 0, 0);
        vb->setSpacing(8);
        profiles_ = new SegmentedControl;
        profiles_->setCompact(true);
        profiles_->setItems({{"Quiet", "moon.fill", ""}, {"Balanced", "dial.medium", ""}, {"Cool", "snowflake", ""}});
        profiles_->setItemGradient(0, QColor(0x6b, 0x6f, 0xd8), Theme::violet());
        profiles_->setItemGradient(1, Theme::violet(), Theme::pink());
        profiles_->setItemGradient(2, Theme::cyan(), Theme::blue());
        vb->addWidget(profiles_);
        smartLine_ = small(QString());
        vb->addWidget(smartLine_);
        panels_->addWidget(w);
        connect(profiles_, &SegmentedControl::currentChanged, this, [this](int) { if (!syncing_) push(); });
    }
    // 4 Target
    {
        auto *w = new QWidget;
        auto *vb = new QVBoxLayout(w);
        vb->setContentsMargins(0, 0, 0, 0);
        vb->setSpacing(4);
        range_ = new RangeSlider;
        range_->setRange(30, 95);
        range_->setFormatter([](double c) { return AppSettings::instance().formatTemp(c); });
        vb->addWidget(range_);
        targetLine_ = small(QString());
        vb->addWidget(targetLine_);
        panels_->addWidget(w);
        connect(range_, &RangeSlider::rangeCommitted, this, [this](double, double) { if (!syncing_) push(); });
    }
    v->addWidget(panels_);

    connect(modes_, &SegmentedControl::currentChanged, this, [this](int) {
        if (!syncing_) push();
    });

    v->addWidget(divider());

    // Footer
    auto *foot = new QHBoxLayout;
    foot->setSpacing(8);
    auto *open = new ProButton("Open MacFans", "macwindow", ProButton::Secondary);
    auto *settings = new ProButton(QString(), "gearshape.fill", ProButton::IconOnly);
    settings->setToolTip("Settings");
    auto *quit = new ProButton(QString(), "power", ProButton::IconOnly);
    quit->setToolTip("Quit MacFans");
    foot->addWidget(open);
    foot->addStretch();
    foot->addWidget(settings);
    foot->addWidget(quit);
    v->addLayout(foot);
    connect(open, &QAbstractButton::clicked, this, [this] { dismiss(); emit openWindowRequested(0); });
    connect(settings, &QAbstractButton::clicked, this, [this] { dismiss(); emit openWindowRequested(2); });
    connect(quit, &QAbstractButton::clicked, this, &TrayPopup::quitRequested);

    syncConfig();
    refresh();
    winId(); // create the native panel now so it can be configured
    MacNative::configurePopupPanel(this);
}

void TrayPopup::push() {
    if (m_->fans().isEmpty()) return;
    FanControlConfig c = m_->fanConfig(0);
    c.mode = FanMode(modes_->currentIndex());
    if (c.mode == FanMode::Manual) c.manualRpm = slider_->value();
    c.smart = SmartProfile(profiles_->currentIndex());
    c.targetLow = range_->low();
    c.targetHigh = range_->high();
    m_->setFanConfig(0, c);
}

void TrayPopup::showModePanel(int mode) {
    panels_->setCurrentIndex(mode);
    if (isVisible()) {
        // Keep the top edge anchored while the height changes.
        QPoint tl = pos();
        adjustSize();
        move(tl);
    }
}

void TrayPopup::syncConfig() {
    if (m_->fans().isEmpty()) return;
    const FanInfo &f = m_->fans().first();
    syncing_ = true;
    const int mode = int(f.cfg.mode);
    if (modes_->currentIndex() != mode) modes_->setCurrentIndex(mode, isVisible());
    showModePanel(mode);
    if (!slider_->isDragging()) {
        slider_->setValue(f.cfg.manualRpm > 0 ? f.cfg.manualRpm : f.actual);
        sliderVal_->setText(Theme::rpm(slider_->value()));
    }
    profiles_->setCurrentIndex(int(f.cfg.smart), isVisible());
    range_->setValues(f.cfg.targetLow, f.cfg.targetHigh);
    ModeStyle st = modeStyle(f.cfg.mode);
    chip_->set(st.symbol, st.a);
    chip_->setGradient(st.a, st.b);
    syncing_ = false;
}

void TrayPopup::refresh() {
    const AppSettings &as = AppSettings::instance();
    if (!m_->fans().isEmpty()) {
        const FanInfo &f = m_->fans().first();
        rpm_->setText(QString("<span style='font-size:20px;font-weight:700;'>%1</span>"
                              "<span style='font-size:11px;color:#8b93a3;'>&nbsp;RPM</span>")
                          .arg(Theme::thousands(f.actual)));
        QString line = modeStyle(f.cfg.mode).name;
        if (f.cfg.mode == FanMode::Smart) {
            static const char *n[] = {"Quiet", "Balanced", "Cool"};
            line += QString(" · %1").arg(n[int(f.cfg.smart)]);
        }
        if (f.cfg.mode != FanMode::Auto && f.cfg.mode != FanMode::Manual)
            line += QString(" · %1").arg(m_->sourceLabel(f.cfg.source));
        modeLine_->setText(line);
        curveLine_->setText(f.status);
        smartLine_->setText(f.status);
        targetLine_->setText(f.status);
        range_->setMarker(f.ema > 0 ? f.ema : f.sourceTemp, true);
        slider_->setMarker(f.actual, true);
    }
    for (int i = 0; i < tempIds_.size(); ++i) {
        bool ok = false;
        double v = m_->nodeValue(tempIds_[i], &ok);
        tempVals_[i]->setText(ok ? as.formatTemp(v) : QStringLiteral("—"));
        Theme::setLabelColor(tempVals_[i], Theme::tempColor(v));
    }
    const PowerSample &pw = m_->power();
    const QColor pc = Theme::pressureColor(pw.pressure);
    pressure_->setText(QString::fromUtf8("\u25CF ") + PowerMonitor::pressureName(pw.pressure));
    const QString pcss = QString("color:%1; border:1px solid rgba(%2,%3,%4,90); background:rgba(%2,%3,%4,26);")
                             .arg(pc.name()).arg(pc.red()).arg(pc.green()).arg(pc.blue());
    if (pressure_->styleSheet() != pcss) pressure_->setStyleSheet(pcss);
    pressure_->setToolTip(PowerMonitor::pressureDescription(pw.pressure));
    powerLine_->setText(pw.valid ? QString("%1 W  ·  P-cores %2 GHz")
                                       .arg(pw.totalW, 0, 'f', 1)
                                       .arg(pw.pFreqMHz / 1000.0, 0, 'f', 2)
                                 : QString());
    const SysUsage &su = m_->sysUsage();
    cpu_->set("CPU", QString("%1%").arg(qRound(su.cpu)), su.cpu, Theme::blue(), Theme::blueLight());
    mem_->set("Memory", QString("%1 / %2 GB").arg(su.memUsedGB, 0, 'f', 1).arg(su.memTotalGB, 0, 'f', 0),
              su.memPercent, Theme::violet(), Theme::violetLight());
    swap_->set("Swap", QString("%1 GB").arg(su.swapUsedGB, 0, 'f', 2), su.swapPercent, Theme::amber(), Theme::orange());
    disk_->set("Disk", QString("%1%").arg(qRound(su.diskPercent)), su.diskPercent, Theme::cyan(), Theme::blue());
}

void TrayPopup::popUpAt(const QRect &anchor) {
    syncConfig();
    refresh();
    adjustSize();
    const QSize s = size();
    QPoint pos = anchor.isEmpty() ? QCursor::pos() - QPoint(s.width() / 2, -4)
                                  : QPoint(anchor.center().x() - s.width() / 2, anchor.bottom() + 2 - (kPad - 4));
    QScreen *scr = QGuiApplication::screenAt(anchor.isEmpty() ? QCursor::pos() : anchor.center());
    if (!scr) scr = QGuiApplication::primaryScreen();
    if (scr) {
        QRect g = scr->availableGeometry();
        pos.setX(std::clamp(pos.x(), g.left() - kPad + 6, g.right() - s.width() + kPad - 6));
    }

    // Fade + slide in.
    setWindowOpacity(0.0);
    move(pos - QPoint(0, 8));
    show();
    MacNative::configurePopupPanel(this);
    raise();
    auto *group = new QParallelAnimationGroup(this);
    auto *fade = new QPropertyAnimation(this, "windowOpacity");
    fade->setDuration(170);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    auto *slide = new QPropertyAnimation(this, "pos");
    slide->setDuration(220);
    slide->setStartValue(pos - QPoint(0, 8));
    slide->setEndValue(pos);
    slide->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(fade);
    group->addAnimation(slide);
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void TrayPopup::dismiss() {
    if (!isVisible()) return;
    hide();
    emit dismissed();
}

void TrayPopup::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) dismiss();
    else QWidget::keyPressEvent(e);
}

void TrayPopup::paintEvent(QPaintEvent *) {
    // Soft layered drop shadow behind the card.
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF card = QRectF(rect()).adjusted(kPad, kPad - 4, -kPad, -kPad - 4);
    p.setPen(Qt::NoPen);
    for (int i = kPad; i > 0; --i) {
        p.setBrush(QColor(0, 0, 0, int(3 + (kPad - i) * 0.9)));
        p.drawRoundedRect(card.adjusted(-i, -i + 4, i, i + 4), 18 + i, 18 + i);
    }
}

// ================================================================ TrayController

TrayController::TrayController(FanModel *model, ProcessMonitor *procs, QObject *parent)
    : QObject(parent), m_(model) {
    item_.setSymbol("fan");
    popup_ = new TrayPopup(model, procs);
    item_.onClick = [this](QRect r) { toggle(r); };

    connect(popup_, &TrayPopup::openWindowRequested, this, &TrayController::openWindow);
    connect(popup_, &TrayPopup::quitRequested, qApp, &QApplication::quit);
    connect(popup_, &TrayPopup::dismissed, this, [this] {
        monitor_.stop();
        item_.setHighlighted(false);
        sinceHide_.start();
    });
    connect(model, &FanModel::updated, this, [this] {
        updateStatusItem();
        if (popup_->isVisible()) popup_->refresh();
    });
    connect(model, &FanModel::configChanged, this, [this](int) { popup_->syncConfig(); });
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this] {
        updateStatusItem();
        popup_->refresh();
    });
    updateStatusItem();
}

void TrayController::toggle(const QRect &anchor) {
    if (popup_->isVisible()) {
        popup_->dismiss();
        return;
    }
    // A click on the item that just dismissed the popup should not reopen it.
    if (sinceHide_.isValid() && sinceHide_.elapsed() < 200) return;
    showPopupAt(anchor);
}

void TrayController::showPopupAt(const QRect &anchor) {
    popup_->popUpAt(anchor);
    item_.setHighlighted(true);
    monitor_.start(popup_, &item_, [this] { popup_->dismiss(); });
}

void TrayController::hidePopup() { popup_->dismiss(); }

void TrayController::updateStatusItem() {
    const AppSettings &as = AppSettings::instance();
    QStringList parts;
    const QString src = as.menuBarSource();
    const double t = m_->sourceTemp(src);
    if (as.menuBarShowTemp() && t > 0) parts << as.formatTemp(t);
    double rpm = m_->fans().isEmpty() ? 0 : m_->fans().first().actual;
    if (as.menuBarShowRpm() && rpm > 0) parts << Theme::rpmCompact(rpm);
    item_.setTitle(parts.join(QString::fromUtf8(" · ")));
    item_.setToolTip(QString("MacFans — %1 %2 · Fan %3")
                         .arg(m_->sourceLabel(src), as.formatTemp(t), Theme::rpm(rpm)));
    if (qEnvironmentVariableIsSet("MACFANS_DEBUG"))
        qInfo() << "status item" << item_.buttonRect() << parts.join(" · ");
}
