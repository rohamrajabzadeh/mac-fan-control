#include "pages.h"
#include "appsettings.h"
#include "charts.h"
#include "fancard.h"
#include "fanmodel.h"
#include "gauge.h"
#include "macnative.h"
#include "alerts.h"
#include "procmon.h"
#include "procview.h"
#include "prowidgets.h"
#include "sensortree.h"
#include "theme.h"

#include <QCoreApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

// ================================================================ helpers

static QScrollArea *pageScroll(QWidget *content) {
    auto *s = new QScrollArea;
    s->setObjectName("pageScroll");
    s->setWidgetResizable(true);
    s->setFrameShape(QFrame::NoFrame);
    s->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    content->setObjectName("page");
    s->setWidget(content);
    return s;
}

static QFrame *card() {
    auto *c = new QFrame;
    c->setObjectName("card");
    return c;
}

static QHBoxLayout *cardHeader(const QString &symbol, const QColor &a, const QColor &b,
                               const QString &title, QLabel **subOut = nullptr,
                               const QString &sub = {}) {
    auto *h = new QHBoxLayout;
    h->setSpacing(10);
    auto *chip = new IconChip(symbol, a, 30);
    chip->setGradient(a, b);
    h->addWidget(chip);
    auto *tb = new QVBoxLayout;
    tb->setSpacing(0);
    auto *t = new QLabel(title);
    t->setObjectName("cardTitle");
    tb->addWidget(t);
    auto *s = new QLabel(sub);
    s->setObjectName("cardSub");
    s->setVisible(!sub.isEmpty() || subOut);
    tb->addWidget(s);
    if (subOut) *subOut = s;
    h->addLayout(tb);
    return h;
}

// ================================================================ Dashboard

DashboardPage::DashboardPage(FanModel *m, ProcessMonitor *pm, QWidget *parent) : QWidget(parent), m_(m) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *content = new QWidget;
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(22, 18, 22, 22);
    v->setSpacing(16);

    // --- Overview tiles
    auto *tiles = new QHBoxLayout;
    tiles->setSpacing(14);
    cpu_ = new StatTile(&m->history());
    cpu_->setup("CPU Load", "cpu", Theme::blue(), Theme::blueLight(), "s:cpu");
    mem_ = new StatTile(&m->history());
    mem_->setup("Memory", "memorychip", Theme::violet(), Theme::violetLight(), "s:mem");
    swap_ = new StatTile(&m->history());
    swap_->setup("Swap", "arrow.left.arrow.right", Theme::amber(), Theme::orange());
    disk_ = new StatTile(&m->history());
    disk_->setup("Disk", "internaldrive.fill", Theme::cyan(), Theme::blue());
    for (StatTile *t : {cpu_, mem_, swap_, disk_}) tiles->addWidget(t);
    v->addLayout(tiles);

    // --- Main row
    auto *row = new QHBoxLayout;
    row->setSpacing(16);
    auto *left = new QVBoxLayout;
    left->setSpacing(16);
    for (const auto &f : m->fans()) {
        auto *c = new FanCard(m, f.index);
        connect(c, &FanCard::enableControlRequested, this, &DashboardPage::enableControlRequested);
        cards_ << c;
        left->addWidget(c);
    }
    if (m->fans().isEmpty()) {
        auto *none = card();
        auto *nl = new QVBoxLayout(none);
        auto *t = new QLabel("No controllable fans were reported by the SMC.");
        t->setObjectName("panelText");
        nl->addWidget(t);
        left->addWidget(none);
    }
    left->addStretch();
    row->addLayout(left, 11);

    // --- Temperatures card
    auto *tc = card();
    tc->setMinimumHeight(560);
    auto *tv = new QVBoxLayout(tc);
    tv->setContentsMargins(18, 18, 18, 14);
    tv->setSpacing(12);
    auto *head = cardHeader("thermometer.medium", Theme::amber(), Theme::red(), "Temperatures", &sensorCount_);
    head->addStretch();
    hotPill_ = new QLabel;
    hotPill_->setObjectName("hotPill");
    head->addWidget(hotPill_);
    tv->addLayout(head);

    auto *hero = new QHBoxLayout;
    hero->setSpacing(16);
    hero_ = new Gauge;
    hero_->setFixedSize(138, 138);
    hero_->setUnit("°C");
    hero->addWidget(hero_);
    auto *heroRight = new QVBoxLayout;
    heroRight->setSpacing(8);
    auto *gl = new QLabel("GAUGE SHOWS");
    gl->setObjectName("capLabel");
    heroRight->addWidget(gl);
    heroSrc_ = new SourcePicker(m);
    heroSrc_->setSource(AppSettings::instance().gaugeSource());
    heroRight->addWidget(heroSrc_, 0, Qt::AlignLeft);
    auto *quick = new QGridLayout;
    quick->setHorizontalSpacing(14);
    quick->setVerticalSpacing(5);
    int qi = 0;
    for (int t : m->topLevelNodes()) {
        const SensorNode &n = m->nodes()[t];
        auto *cell = new QHBoxLayout;
        cell->setSpacing(6);
        auto *ic = new QLabel;
        ic->setPixmap(MacNative::symbol(n.symbol, 12, Theme::categoryColor(n.category)));
        cell->addWidget(ic);
        auto *nm = new QLabel(n.label);
        nm->setObjectName("quickName");
        cell->addWidget(nm);
        auto *val = new QLabel("—");
        val->setObjectName("quickVal");
        cell->addWidget(val);
        cell->addStretch();
        quick->addLayout(cell, qi / 2, qi % 2);
        quickVals_ << val;
        quickIds_ << n.id;
        ++qi;
    }
    heroRight->addLayout(quick);
    heroRight->addStretch();
    hero->addLayout(heroRight, 1);
    tv->addLayout(hero);

    auto *treeScroll = new QScrollArea;
    treeScroll->setObjectName("treeScroll");
    treeScroll->setWidgetResizable(true);
    treeScroll->setFrameShape(QFrame::NoFrame);
    treeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree_ = new SensorTreeView(m);
    auto *treeWrap = new QWidget;
    treeWrap->setObjectName("treeWrap");
    auto *tw = new QVBoxLayout(treeWrap);
    tw->setContentsMargins(0, 0, 8, 0);
    tw->addWidget(tree_);
    tw->addStretch();
    treeScroll->setWidget(treeWrap);
    tv->addWidget(treeScroll, 1);
    row->addWidget(tc, 9);
    v->addLayout(row, 1);

    // --- Row 3: performance & power | top processes
    auto *row3 = new QHBoxLayout;
    row3->setSpacing(16);

    auto *perf = card();
    auto *pv = new QVBoxLayout(perf);
    pv->setContentsMargins(20, 18, 20, 18);
    pv->setSpacing(14);
    auto *ph = cardHeader("bolt.fill", Theme::amber(), Theme::orange(), "Performance & Power", &pressureText_);
    ph->addStretch();
    pressurePill_ = new QLabel;
    pressurePill_->setObjectName("hotPill");
    ph->addWidget(pressurePill_);
    pv->addLayout(ph);
    auto *pbody = new QHBoxLayout;
    pbody->setSpacing(24);
    auto *wcol = new QVBoxLayout;
    wcol->setSpacing(2);
    auto *wcap = new QLabel("PACKAGE POWER");
    wcap->setObjectName("capLabel");
    watts_ = new QLabel("—");
    watts_->setObjectName("bigValue");
    wattsSub_ = new QLabel;
    wattsSub_->setObjectName("cardSub");
    powerSpark_ = new SparkWidget(&m->history(), "p:total", Theme::amber());
    powerSpark_->setFixedHeight(46);
    powerSpark_->setMinimumWidth(170);
    wcol->addWidget(wcap);
    wcol->addWidget(watts_);
    wcol->addWidget(wattsSub_);
    wcol->addWidget(powerSpark_);
    pbody->addLayout(wcol, 4);
    auto *bars = new QVBoxLayout;
    bars->setSpacing(10);
    auto *bcap = new QLabel("CLOCK SPEEDS · ACTIVITY");
    bcap->setObjectName("capLabel");
    bars->addWidget(bcap);
    pBar_ = new MiniBar;
    eBar_ = new MiniBar;
    gBar_ = new MiniBar;
    bars->addWidget(pBar_);
    bars->addWidget(eBar_);
    bars->addWidget(gBar_);
    bars->addStretch();
    pbody->addLayout(bars, 5);
    pv->addLayout(pbody);
    row3->addWidget(perf, 11);

    auto *procs = card();
    auto *prv = new QVBoxLayout(procs);
    prv->setContentsMargins(20, 18, 20, 12);
    prv->setSpacing(10);
    auto *prh = cardHeader("flame.fill", Theme::orange(), Theme::red(), "What's Heating Your Mac", nullptr,
                           "Top CPU users, grouped by app");
    prv->addLayout(prh);
    prv->addWidget(new ProcessListView(pm, 5, false, true));
    prv->addStretch();
    row3->addWidget(procs, 9);
    v->addLayout(row3);

    outer->addWidget(pageScroll(content));

    connect(heroSrc_, &SourcePicker::sourceChanged, this, [](const QString &id) {
        AppSettings::instance().setGaugeSource(id);
    });
    connect(tree_, &SensorTreeView::showOnGauge, this, [this](const QString &id) {
        heroSrc_->setSource(id);
        AppSettings::instance().setGaugeSource(id);
    });
    connect(tree_, &SensorTreeView::showInMenuBar, this, [](const QString &id) {
        AppSettings &as = AppSettings::instance();
        as.setMenuBarSource(id);
        as.setMenuBarShowTemp(true);
    });

    int leaves = 0;
    for (const auto &n : m->nodes()) leaves += n.children.isEmpty() ? 1 : 0;
    sensorCount_->setText(QString("%1 sensors in %2 groups").arg(leaves).arg(m->topLevelNodes().size()));
    refresh();
}

void DashboardPage::syncConfig() {
    for (FanCard *c : cards_) c->syncFromModel();
    heroSrc_->setSource(AppSettings::instance().gaugeSource());
}

void DashboardPage::refresh() {
    const SysUsage &su = m_->sysUsage();
    const AppSettings &as = AppSettings::instance();

    cpu_->setValues(QString("%1%").arg(qRound(su.cpu)),
                    QString("User %1% · System %2%").arg(qRound(su.cpuUser)).arg(qRound(su.cpuSystem)), su.cpu);
    mem_->setValues(QString("%1%").arg(qRound(su.memPercent)),
                    QString("%1 of %2 GB").arg(su.memUsedGB, 0, 'f', 1).arg(su.memTotalGB, 0, 'f', 0),
                    su.memPercent);
    swap_->setValues(QString("%1 GB").arg(su.swapUsedGB, 0, 'f', su.swapUsedGB < 10 ? 2 : 1),
                     su.swapTotalGB > 0 ? QString("of %1 GB allocated").arg(su.swapTotalGB, 0, 'f', 1)
                                        : QStringLiteral("No swap in use"),
                     su.swapPercent);
    disk_->setValues(QString("%1%").arg(qRound(su.diskPercent)),
                     QString("%1 of %2 GB used").arg(qRound(su.diskUsedGB)).arg(qRound(su.diskTotalGB)),
                     su.diskPercent);

    for (FanCard *c : cards_) c->refresh();

    const QString src = as.gaugeSource();
    const double t = m_->sourceTemp(src);
    hero_->setUnit(as.tempUnit());
    hero_->setRange(as.displayTemp(20), as.displayTemp(100));
    hero_->setCaption(m_->sourceLabel(src));
    hero_->setAccent(Theme::tempColor(t));
    hero_->setValue(as.displayTemp(t));
    heroSrc_->refreshText();

    const double hot = m_->hottest();
    hotPill_->setText(QString("Hottest  %1  %2").arg(m_->hottestLabel(), as.formatTemp(hot)));
    QColor hc = Theme::tempColor(hot);
    const QString pillCss = QString("color:%1; border:1px solid rgba(%2,%3,%4,90); background:rgba(%2,%3,%4,26);")
                                .arg(hc.name()).arg(hc.red()).arg(hc.green()).arg(hc.blue());
    if (hotPill_->styleSheet() != pillCss) hotPill_->setStyleSheet(pillCss);

    for (int i = 0; i < quickIds_.size(); ++i) {
        bool ok = false;
        double v = m_->nodeValue(quickIds_[i], &ok);
        quickVals_[i]->setText(ok ? as.formatTemp(v) : QStringLiteral("—"));
        Theme::setLabelColor(quickVals_[i], Theme::tempColor(v));
    }
    tree_->update();

    // Performance & power
    const PowerSample &pw = m_->power();
    const QColor pc = Theme::pressureColor(pw.pressure);
    pressurePill_->setText(QString::fromUtf8("\u25CF  ") + PowerMonitor::pressureName(pw.pressure));
    const QString css = QString("color:%1; border:1px solid rgba(%2,%3,%4,90); background:rgba(%2,%3,%4,26);")
                            .arg(pc.name()).arg(pc.red()).arg(pc.green()).arg(pc.blue());
    if (pressurePill_->styleSheet() != css) pressurePill_->setStyleSheet(css);
    pressureText_->setText(PowerMonitor::pressureDescription(pw.pressure));
    if (pw.valid) {
        watts_->setText(QString("%1 W").arg(pw.totalW, 0, 'f', pw.totalW < 10 ? 2 : 1));
        wattsSub_->setText(QString("CPU %1 W  ·  GPU %2 W").arg(pw.cpuW, 0, 'f', 2).arg(pw.gpuW, 0, 'f', 2));
        auto clock = [](double mhz) {
            return mhz >= 1000 ? QString::number(mhz / 1000.0, 'f', 2) + " GHz" : QString::number(qRound(mhz)) + " MHz";
        };
        pBar_->set("Performance cores", QString("%1  ·  %2%").arg(clock(pw.pFreqMHz)).arg(qRound(pw.pUsage)),
                   pw.pUsage, Theme::violet(), Theme::violetLight());
        eBar_->set("Efficiency cores", QString("%1  ·  %2%").arg(clock(pw.eFreqMHz)).arg(qRound(pw.eUsage)),
                   pw.eUsage, Theme::cyan(), Theme::blue());
        gBar_->set("GPU", QString("%1  ·  %2%").arg(clock(pw.gpuFreqMHz)).arg(qRound(pw.gpuUsage)),
                   pw.gpuUsage, Theme::green(), Theme::cyan());
    } else {
        watts_->setText(QStringLiteral("—"));
        wattsSub_->setText(QStringLiteral("Power data isn't available on this Mac"));
    }
    powerSpark_->update();
}

// ================================================================ History

static QFrame *chartCard(const QString &symbol, const QColor &a, const QColor &b, const QString &title,
                         QWidget *chart, QLabel **nowOut, QHBoxLayout **headOut = nullptr) {
    auto *c = card();
    auto *v = new QVBoxLayout(c);
    v->setContentsMargins(18, 16, 14, 12);
    v->setSpacing(10);
    auto *h = cardHeader(symbol, a, b, title);
    h->addStretch();
    if (nowOut) {
        *nowOut = new QLabel;
        (*nowOut)->setObjectName("chartNow");
        (*nowOut)->setStyleSheet(QString("color:%1;").arg(b.name()));
        h->addWidget(*nowOut);
    }
    v->addLayout(h);
    v->addWidget(chart, 1);
    if (headOut) *headOut = h;
    return c;
}

HistoryPage::HistoryPage(FanModel *m, QWidget *parent) : QWidget(parent), m_(m) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *content = new QWidget;
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(22, 18, 22, 22);
    v->setSpacing(16);

    // Header
    auto *head = new QHBoxLayout;
    auto *tb = new QVBoxLayout;
    tb->setSpacing(2);
    auto *t = new QLabel("History");
    t->setObjectName("pageTitle");
    auto *s = new QLabel("Live record of temperatures, fan speed and system load");
    s->setObjectName("pageSub");
    tb->addWidget(t);
    tb->addWidget(s);
    head->addLayout(tb);
    head->addStretch();
    range_ = new SegmentedControl;
    range_->setCompact(true);
    range_->setItems({{"5 min", "clock", ""}, {"15 min", "clock", ""}, {"1 hour", "clock", ""}});
    range_->setFixedWidth(330);
    head->addWidget(range_);
    v->addLayout(head);

    const History *h = &m->history();
    auto tempTf = [](double c) { return AppSettings::instance().displayTemp(c); };
    auto tempFmt = [](double v) { return QString::number(qRound(v)) + "°"; };

    // Temperatures
    temps_ = new LineChart(h);
    temps_->setMinimumHeight(270);
    QVector<ChartSeries> ts;
    const QStringList hidden = AppSettings::instance().hiddenSeries();
    for (int i : m->topLevelNodes()) {
        const SensorNode &n = m->nodes()[i];
        ts.push_back({"t:" + n.id, n.label, Theme::categoryColor(n.category), !hidden.contains("t:" + n.id)});
    }
    temps_->setSeries(ts);
    temps_->setAutoY(20, 12);
    temps_->setTransform(tempTf);
    temps_->setFormatter(tempFmt);
    temps_->setBand("p:pressure");
    QHBoxLayout *tempHead = nullptr;
    auto *tempCard = chartCard("thermometer.medium", Theme::amber(), Theme::red(), "Temperatures", temps_, nullptr, &tempHead);
    tempCard->setMinimumHeight(340);
    for (const auto &se : ts) {
        auto *chip = new LegendChip(se.label, se.color);
        chip->setChecked(se.visible);
        const QString id = se.id;
        connect(chip, &QAbstractButton::toggled, this, [this, id](bool on) {
            temps_->setSeriesVisible(id, on);
            QStringList hs = AppSettings::instance().hiddenSeries();
            hs.removeAll(id);
            if (!on) hs << id;
            AppSettings::instance().setHiddenSeries(hs);
        });
        chips_ << chip;
        tempHead->addWidget(chip);
    }
    v->addWidget(tempCard);

    // Fan / CPU / Memory
    auto *row = new QHBoxLayout;
    row->setSpacing(16);
    fan_ = new LineChart(h);
    QVector<ChartSeries> fs;
    const QColor fanColors[] = {Theme::blueLight(), Theme::violetLight(), Theme::cyan(), Theme::pink()};
    for (const auto &f : m->fans())
        fs.push_back({QString("f:%1").arg(f.index), f.name, fanColors[f.index % 4], true});
    fan_->setSeries(fs);
    if (!m->fans().isEmpty())
        fan_->setFixedYRange(0, std::max(1000.0, m->fans().first().maxRpm));
    fan_->setFormatter([](double v) { return Theme::rpmCompact(v); });
    row->addWidget(chartCard("fan.fill", Theme::blue(), Theme::blueLight(), "Fan Speed", fan_, &fanNow_), 1);

    cpu_ = new LineChart(h);
    cpu_->setSeries({{"s:cpu", "CPU", Theme::blue(), true}});
    cpu_->setFixedYRange(0, 100);
    cpu_->setFormatter([](double v) { return QString::number(qRound(v)) + "%"; });
    row->addWidget(chartCard("cpu", Theme::blue(), Theme::violet(), "CPU Load", cpu_, &cpuNow_), 1);

    mem_ = new LineChart(h);
    mem_->setSeries({{"s:memgb", "Memory", Theme::violet(), true}, {"s:swapgb", "Swap", Theme::amber(), true}});
    mem_->setFixedYRange(0, std::max(1.0, std::ceil(m->sysUsage().memTotalGB)));
    mem_->setFormatter([](double v) { return QString::number(v, 'f', v < 10 ? 1 : 0) + " GB"; });
    row->addWidget(chartCard("memorychip", Theme::violet(), Theme::violetLight(), "Memory & Swap", mem_, &memNow_), 1);
    for (LineChart *c : {fan_, cpu_, mem_}) c->setMinimumHeight(200);
    v->addLayout(row);

    // Power & clocks
    auto *row2 = new QHBoxLayout;
    row2->setSpacing(16);
    power_ = new LineChart(h);
    power_->setSeries({{"p:cpu", "CPU", Theme::blue(), true},
                       {"p:gpu", "GPU", Theme::green(), true},
                       {"p:total", "Total", Theme::amber(), true}});
    power_->setAutoY(0, 2);
    power_->setFormatter([](double v) { return QString::number(v, 'f', v < 10 ? 1 : 0) + " W"; });
    power_->setBand("p:pressure");
    power_->setMinimumHeight(210);
    row2->addWidget(chartCard("bolt.fill", Theme::amber(), Theme::orange(), "Power", power_, &powerNow_), 1);

    clocks_ = new LineChart(h);
    clocks_->setSeries({{"c:p", "P-cores", Theme::violet(), true},
                        {"c:e", "E-cores", Theme::cyan(), true},
                        {"c:g", "GPU", Theme::green(), true}});
    const PowerMonitor &pmon = m->powerMonitor();
    clocks_->setFixedYRange(0, std::max({1000.0, pmon.maxPFreqMHz(), pmon.maxEFreqMHz(), pmon.maxGpuFreqMHz()}));
    clocks_->setFormatter([](double v) {
        return v >= 1000 ? QString::number(v / 1000.0, 'f', 1) + " GHz" : QString::number(qRound(v)) + " MHz";
    });
    clocks_->setMinimumHeight(210);
    row2->addWidget(chartCard("speedometer", Theme::violet(), Theme::pink(), "Clock Speeds", clocks_, &clockNow_), 1);
    v->addLayout(row2);
    v->addStretch();

    outer->addWidget(pageScroll(content));

    const int saved = AppSettings::instance().historyRange();
    range_->setCurrentIndex(saved >= 3600 ? 2 : saved >= 900 ? 1 : 0, false);
    connect(range_, &SegmentedControl::currentChanged, this, [](int i) {
        AppSettings::instance().setHistoryRange(i == 2 ? 3600 : i == 1 ? 900 : 300);
    });
    applySettings();
}

void HistoryPage::applySettings() {
    const int w = AppSettings::instance().historyRange();
    for (LineChart *c : {temps_, fan_, cpu_, mem_, power_, clocks_}) c->setWindow(w);
    temps_->update();
}

void HistoryPage::refresh() {
    const SysUsage &su = m_->sysUsage();
    if (!m_->fans().isEmpty()) fanNow_->setText(Theme::rpm(m_->fans().first().actual));
    cpuNow_->setText(QString("%1%").arg(qRound(su.cpu)));
    memNow_->setText(QString("%1 GB").arg(su.memUsedGB, 0, 'f', 1));
    const PowerSample &pw = m_->power();
    powerNow_->setText(pw.valid ? QString("%1 W").arg(pw.totalW, 0, 'f', 1) : QStringLiteral("—"));
    clockNow_->setText(pw.valid ? QString("%1 GHz").arg(pw.pFreqMHz / 1000.0, 0, 'f', 2) : QStringLiteral("—"));
    for (LineChart *c : {temps_, fan_, cpu_, mem_, power_, clocks_}) c->update();
}

// ================================================================ Settings

static QWidget *settingRow(const QString &symbol, const QColor &color, const QString &title,
                           const QString &desc, QWidget *control) {
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 10, 0, 10);
    h->setSpacing(14);
    h->addWidget(new IconChip(symbol, color, 30), 0, Qt::AlignVCenter);
    auto *vb = new QVBoxLayout;
    vb->setSpacing(2);
    auto *t = new QLabel(title);
    t->setObjectName("rowTitle");
    vb->addWidget(t);
    if (!desc.isEmpty()) {
        auto *d = new QLabel(desc);
        d->setObjectName("rowDesc");
        d->setWordWrap(true);
        vb->addWidget(d);
    }
    h->addLayout(vb, 1);
    h->addWidget(control, 0, Qt::AlignVCenter);
    return w;
}

static QFrame *section(const QString &title, const QVector<QWidget *> &rows) {
    auto *c = card();
    auto *v = new QVBoxLayout(c);
    v->setContentsMargins(20, 16, 20, 8);
    v->setSpacing(0);
    auto *t = new QLabel(title);
    t->setObjectName("sectionTitle");
    v->addWidget(t);
    v->addSpacing(4);
    for (int i = 0; i < rows.size(); ++i) {
        if (i > 0) {
            auto *d = new QFrame;
            d->setObjectName("divider");
            d->setFixedHeight(1);
            v->addWidget(d);
        }
        v->addWidget(rows[i]);
    }
    return c;
}

SettingsPage::SettingsPage(FanModel *m, AlertManager *alerts, QWidget *parent)
    : QWidget(parent), m_(m), alerts_(alerts) {
    AppSettings &as = AppSettings::instance();
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *content = new QWidget;
    auto *center = new QHBoxLayout(content);
    center->setContentsMargins(22, 18, 22, 22);
    auto *colW = new QWidget;
    colW->setMaximumWidth(780);
    auto *v = new QVBoxLayout(colW);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(16);
    center->addStretch();
    center->addWidget(colW, 1);
    center->addStretch();

    auto *tb = new QVBoxLayout;
    tb->setSpacing(2);
    auto *title = new QLabel("Settings");
    title->setObjectName("pageTitle");
    auto *sub = new QLabel("Menu bar, startup, units and fan-control permission");
    sub->setObjectName("pageSub");
    tb->addWidget(title);
    tb->addWidget(sub);
    v->addLayout(tb);

    // --- Menu bar
    mbTemp_ = new ToggleSwitch;
    mbTemp_->setCheckedSilently(as.menuBarShowTemp());
    mbSource_ = new SourcePicker(m);
    mbSource_->setAllowAuto(true);
    mbSource_->setSource(as.menuBarSource());
    mbRpm_ = new ToggleSwitch;
    mbRpm_->setCheckedSilently(as.menuBarShowRpm());
    v->addWidget(section("Menu Bar", {
        settingRow("thermometer.medium", Theme::amber(), "Show temperature in the menu bar",
                   "Displays a live reading next to the fan icon.", mbTemp_),
        settingRow("scope", Theme::violet(), "Sensor shown",
                   "Which temperature appears in the menu bar.", mbSource_),
        settingRow("fan.fill", Theme::blue(), "Show fan speed in the menu bar",
                   "Adds the current RPM (e.g. 2.1k) next to the temperature.", mbRpm_)}));

    // --- General
    login_ = new ToggleSwitch;
    login_->setCheckedSilently(as.launchAtLogin());
    showWin_ = new ToggleSwitch;
    showWin_->setCheckedSilently(as.showWindowAtLaunch());
    unit_ = new SegmentedControl;
    unit_->setCompact(true);
    unit_->setItems({{"°C", "", "Celsius"}, {"°F", "", "Fahrenheit"}});
    unit_->setFixedWidth(130);
    unit_->setCurrentIndex(as.fahrenheit() ? 1 : 0, false);
    poll_ = new SegmentedControl;
    poll_->setCompact(true);
    poll_->setItems({{"1 s", "", "Fastest updates"}, {"1.5 s", "", "Default"}, {"3 s", "", "Lowest overhead"}});
    poll_->setFixedWidth(200);
    poll_->setCurrentIndex(as.pollMs() <= 1000 ? 0 : as.pollMs() <= 1500 ? 1 : 2, false);
    v->addWidget(section("General", {
        settingRow("power", Theme::green(), "Launch at login",
                   "Starts MacFans quietly in the menu bar when you log in.", login_),
        settingRow("macwindow", Theme::blue(), "Open window at launch",
                   "When off, MacFans starts in the menu bar only. Closing the window always keeps it running there.", showWin_),
        settingRow("thermometer.variable", Theme::orange(), "Temperature unit", QString(), unit_),
        settingRow("waveform.path.ecg", Theme::cyan(), "Update interval",
                   "How often sensors are read and the fan controller runs.", poll_)}));

    // --- Alerts & safety
    alTemp_ = new ToggleSwitch;
    alTemp_->setCheckedSilently(as.alertTempOn());
    auto *thr = new QWidget;
    {
        auto *h = new QHBoxLayout(thr);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(10);
        alThreshold_ = new ProSlider;
        alThreshold_->setRange(60, 105);
        alThreshold_->setStep(1);
        alThreshold_->setGradient(Theme::amber(), Theme::red());
        alThreshold_->setFixedWidth(190);
        alThreshold_->setValue(as.alertTempC());
        alThresholdVal_ = new QLabel;
        alThresholdVal_->setObjectName("accentValue");
        alThresholdVal_->setMinimumWidth(44);
        alThresholdVal_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        h->addWidget(alThreshold_);
        h->addWidget(alThresholdVal_);
    }
    alSource_ = new SourcePicker(m);
    alSource_->setSource(as.alertTempSource());
    alThrottle_ = new ToggleSwitch;
    alThrottle_->setCheckedSilently(as.alertThrottleOn());
    alFan_ = new ToggleSwitch;
    alFan_->setCheckedSilently(as.alertFanOn());
    emergency_ = new ToggleSwitch;
    emergency_->setCheckedSilently(as.emergencyOn());
    crashStatus_ = new QLabel;
    crashStatus_->setObjectName("rowDesc");
    crashStatus_->setWordWrap(true);
    crashBtn_ = new ProButton("Update Helper", "arrow.down.circle.fill", ProButton::Primary);
    crashBtn_->setGradient(Theme::amber(), Theme::orange());
    auto *crashRow = new QWidget;
    {
        auto *h = new QHBoxLayout(crashRow);
        h->setContentsMargins(0, 10, 0, 10);
        h->setSpacing(14);
        h->addWidget(new IconChip("checkmark.shield.fill", Theme::green(), 30), 0, Qt::AlignVCenter);
        auto *vb = new QVBoxLayout;
        vb->setSpacing(2);
        auto *t = new QLabel("Crash protection");
        t->setObjectName("rowTitle");
        vb->addWidget(t);
        vb->addWidget(crashStatus_);
        h->addLayout(vb, 1);
        h->addWidget(crashBtn_, 0, Qt::AlignVCenter);
    }
    auto *testBtn = new ProButton("Send Test", "bell.fill", ProButton::Secondary);
    events_ = new QLabel;
    events_->setObjectName("rowDesc");
    events_->setTextFormat(Qt::RichText);
    events_->setWordWrap(true);
    auto *eventsRow = new QWidget;
    {
        auto *vb = new QVBoxLayout(eventsRow);
        vb->setContentsMargins(0, 10, 0, 10);
        vb->setSpacing(6);
        auto *t = new QLabel("Recent alerts");
        t->setObjectName("rowTitle");
        vb->addWidget(t);
        vb->addWidget(events_);
    }
    v->addWidget(section("Alerts & Safety", {
        settingRow("thermometer.high", Theme::orange(), "High-temperature alert",
                   "Notify me when the sensor below goes above the threshold.", alTemp_),
        settingRow("dial.medium", Theme::amber(), "Alert threshold", QString(), thr),
        settingRow("scope", Theme::violet(), "Sensor to watch", QString(), alSource_),
        settingRow("gauge.with.dots.needle.33percent", Theme::red(), "Throttling alert",
                   "Notify me when macOS reports heavy thermal pressure for more than 10 seconds.", alThrottle_),
        settingRow("fan.fill", Theme::blue(), "Fan failure alert",
                   "Notify me if a fan stops spinning or can't reach the speed it was asked for.", alFan_),
        settingRow("flame.fill", Theme::red(), "Emergency cooling",
                   "In any mode, force the fan to maximum if a sensor reaches 95° or macOS reports heavy thermal pressure. "
                   "Releases below 85°.", emergency_),
        crashRow,
        settingRow("bell.fill", Theme::blue(), "Test notification",
                   "Sends a sample alert. macOS asks for permission the first time.", testBtn),
        eventsRow}));

    // --- Fan control
    helperStatus_ = new QLabel;
    helperStatus_->setObjectName("rowDesc");
    helperStatus_->setWordWrap(true);
    helperBtn_ = new ProButton("Enable", "checkmark.shield.fill", ProButton::Primary);
    auto *restore = new ProButton("Restore Auto", "arrow.triangle.2.circlepath", ProButton::Secondary);
    auto *helperRow = new QWidget;
    {
        auto *h = new QHBoxLayout(helperRow);
        h->setContentsMargins(0, 10, 0, 10);
        h->setSpacing(14);
        h->addWidget(new IconChip("lock.open.fill", Theme::amber(), 30), 0, Qt::AlignVCenter);
        auto *vb = new QVBoxLayout;
        vb->setSpacing(2);
        auto *t = new QLabel("Passwordless fan control");
        t->setObjectName("rowTitle");
        vb->addWidget(t);
        vb->addWidget(helperStatus_);
        h->addLayout(vb, 1);
        h->addWidget(helperBtn_, 0, Qt::AlignVCenter);
    }
    v->addWidget(section("Fan Control", {
        helperRow,
        settingRow("arrow.triangle.2.circlepath", Theme::blue(), "Return all fans to automatic",
                   "Hands every fan back to macOS right now.", restore)}));

    // --- About
    auto *about = card();
    {
        auto *h = new QHBoxLayout(about);
        h->setContentsMargins(20, 18, 20, 18);
        h->setSpacing(16);
        auto *icon = new QLabel;
        QPixmap app(":/icons/appicon.png");
        app.setDevicePixelRatio(2.0);
        icon->setPixmap(app.scaled(112, 112, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        icon->setFixedSize(56, 56);
        icon->setScaledContents(true);
        h->addWidget(icon);
        auto *vb = new QVBoxLayout;
        vb->setSpacing(2);
        auto *t = new QLabel("MacFans " + QCoreApplication::applicationVersion());
        t->setObjectName("rowTitle");
        auto *d = new QLabel("Open-source fan control and thermal monitor for Apple silicon Macs.");
        d->setObjectName("rowDesc");
        vb->addWidget(t);
        vb->addWidget(d);
        h->addLayout(vb, 1);
        auto *gh = new ProButton("GitHub", "link", ProButton::Secondary);
        connect(gh, &QAbstractButton::clicked, this, [] {
            MacNative::openUrl("https://github.com/rohamrajabzadeh/mac-fan-control");
        });
        h->addWidget(gh);
    }
    v->addWidget(about);
    v->addStretch();

    outer->addWidget(pageScroll(content));

    connect(mbTemp_, &QAbstractButton::toggled, this, [](bool on) { AppSettings::instance().setMenuBarShowTemp(on); });
    connect(mbRpm_, &QAbstractButton::toggled, this, [](bool on) { AppSettings::instance().setMenuBarShowRpm(on); });
    connect(mbSource_, &SourcePicker::sourceChanged, this, [](const QString &id) { AppSettings::instance().setMenuBarSource(id); });
    connect(login_, &QAbstractButton::toggled, this, [](bool on) { AppSettings::instance().setLaunchAtLogin(on); });
    connect(showWin_, &QAbstractButton::toggled, this, [](bool on) { AppSettings::instance().setShowWindowAtLaunch(on); });
    connect(unit_, &SegmentedControl::currentChanged, this, [](int i) { AppSettings::instance().setFahrenheit(i == 1); });
    connect(poll_, &SegmentedControl::currentChanged, this, [](int i) {
        AppSettings::instance().setPollMs(i == 0 ? 1000 : i == 1 ? 1500 : 3000);
    });
    connect(helperBtn_, &QAbstractButton::clicked, this, &SettingsPage::enableControlRequested);
    connect(restore, &QAbstractButton::clicked, this, [this] {
        for (const auto &f : m_->fans()) {
            FanControlConfig c = m_->fanConfig(f.index);
            c.mode = FanMode::Auto;
            m_->setFanConfig(f.index, c);
        }
    });
    auto wantNotifications = [](bool on) { if (on) MacNative::requestNotificationPermission(); };
    connect(alTemp_, &QAbstractButton::toggled, this, [wantNotifications](bool on) {
        AppSettings::instance().setAlertTempOn(on);
        wantNotifications(on);
    });
    connect(alThreshold_, &ProSlider::valueChanged, this, [this](double v) {
        alThresholdVal_->setText(AppSettings::instance().formatTemp(v));
    });
    connect(alThreshold_, &ProSlider::valueCommitted, this, [](double v) { AppSettings::instance().setAlertTempC(v); });
    connect(alSource_, &SourcePicker::sourceChanged, this, [](const QString &id) {
        AppSettings::instance().setAlertTempSource(id);
    });
    connect(alThrottle_, &QAbstractButton::toggled, this, [wantNotifications](bool on) {
        AppSettings::instance().setAlertThrottleOn(on);
        wantNotifications(on);
    });
    connect(alFan_, &QAbstractButton::toggled, this, [wantNotifications](bool on) {
        AppSettings::instance().setAlertFanOn(on);
        wantNotifications(on);
    });
    connect(emergency_, &QAbstractButton::toggled, this, [](bool on) { AppSettings::instance().setEmergencyOn(on); });
    connect(crashBtn_, &QAbstractButton::clicked, this, &SettingsPage::enableControlRequested);
    connect(testBtn, &QAbstractButton::clicked, this, [this] { alerts_->sendTest(); });
    connect(alerts_, &AlertManager::eventsChanged, this, &SettingsPage::refreshEvents);
    refreshEvents();
    refresh();
}

void SettingsPage::refreshEvents() {
    const auto &ev = alerts_->events();
    if (ev.isEmpty()) {
        events_->setText("No alerts yet — everything has stayed within limits.");
        return;
    }
    QStringList rows;
    for (int i = 0; i < ev.size() && i < 6; ++i) {
        const AlertEvent &e = ev[i];
        rows << QString("<span style='color:%1;'>&#9679;</span>&nbsp; <b style='color:#d6dbe4;'>%2</b>"
                        "&nbsp;<span style='color:#6e7686;'>%3</span><br/>"
                        "<span style='color:#8b93a3;'>%4</span>")
                    .arg(e.color.name(), e.title.toHtmlEscaped(), e.when.toString("HH:mm"), e.body.toHtmlEscaped());
    }
    events_->setText(rows.join("<br/><br/>"));
}

void SettingsPage::refresh() {
    AppSettings &as = AppSettings::instance();
    mbTemp_->setCheckedSilently(as.menuBarShowTemp());
    mbRpm_->setCheckedSilently(as.menuBarShowRpm());
    mbSource_->setSource(as.menuBarSource());
    login_->setCheckedSilently(as.launchAtLogin());
    showWin_->setCheckedSilently(as.showWindowAtLaunch());
    if (m_->hasRoot()) {
        helperStatus_->setText("Running with administrator rights — fan control is available.");
        helperBtn_->setVisible(false);
    } else if (m_->helperInstalled()) {
        helperStatus_->setText("Enabled. A tiny helper writes only fan keys, so you're never asked for a password again.");
        helperBtn_->setText("Reinstall");
        helperBtn_->setVariant(ProButton::Secondary);
    } else {
        helperStatus_->setText("Not enabled yet. One administrator prompt installs a minimal helper; after that, no more passwords.");
        helperBtn_->setText("Enable");
        helperBtn_->setVariant(ProButton::Primary);
        helperBtn_->setGradient(Theme::amber(), Theme::orange());
    }
    if (m_->helperOutdated()) {
        helperStatus_->setText("Installed — an update is available that adds crash protection.");
        helperBtn_->setText("Update");
        helperBtn_->setVariant(ProButton::Primary);
        helperBtn_->setGradient(Theme::amber(), Theme::orange());
    }

    // Alerts & safety
    alTemp_->setCheckedSilently(as.alertTempOn());
    alThrottle_->setCheckedSilently(as.alertThrottleOn());
    alFan_->setCheckedSilently(as.alertFanOn());
    emergency_->setCheckedSilently(as.emergencyOn());
    if (!alThreshold_->isDragging()) alThreshold_->setValue(as.alertTempC());
    alThresholdVal_->setText(as.formatTemp(alThreshold_->value()));
    alSource_->setSource(as.alertTempSource());
    crashBtn_->setVisible(false);
    if (m_->hasRoot()) {
        crashStatus_->setText("Running with administrator rights; MacFans returns fans to Auto when it quits.");
    } else if (!m_->helperInstalled()) {
        crashStatus_->setText("Enable fan control first. Without it, MacFans never forces the fans.");
    } else if (m_->helperOutdated()) {
        crashStatus_->setText("Not active — your helper predates crash protection. Update it once (one password prompt).");
        crashBtn_->setVisible(true);
    } else if (m_->watchdogActive()) {
        crashStatus_->setText("Active. If MacFans ever crashes or is force-quit, your fans go back to macOS automatically.");
    } else {
        crashStatus_->setText("Ready — becomes active the next time MacFans starts.");
    }
}
