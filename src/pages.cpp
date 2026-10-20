#include "pages.h"
#include "appsettings.h"
#include "charts.h"
#include "fancard.h"
#include "fanmodel.h"
#include "gauge.h"
#include "macnative.h"
#include "prowidgets.h"
#include "sensortree.h"
#include "theme.h"

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

DashboardPage::DashboardPage(FanModel *m, QWidget *parent) : QWidget(parent), m_(m) {
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
    for (LineChart *c : {temps_, fan_, cpu_, mem_}) c->setWindow(w);
    temps_->update();
}

void HistoryPage::refresh() {
    const SysUsage &su = m_->sysUsage();
    if (!m_->fans().isEmpty()) fanNow_->setText(Theme::rpm(m_->fans().first().actual));
    cpuNow_->setText(QString("%1%").arg(qRound(su.cpu)));
    memNow_->setText(QString("%1 GB").arg(su.memUsedGB, 0, 'f', 1));
    for (LineChart *c : {temps_, fan_, cpu_, mem_}) c->update();
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

SettingsPage::SettingsPage(FanModel *m, QWidget *parent) : QWidget(parent), m_(m) {
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
        auto *t = new QLabel("MacFans 2.0");
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
    refresh();
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
}
