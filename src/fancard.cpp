#include "fancard.h"
#include "appsettings.h"
#include "charts.h"
#include "curveeditor.h"
#include "gauge.h"
#include "macnative.h"
#include "prowidgets.h"
#include "theme.h"

#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPropertyAnimation>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <cmath>

// ---------------------------------------------------------------- styles

ModeStyle modeStyle(FanMode m) {
    switch (m) {
    case FanMode::Manual: return {"Custom", "slider.horizontal.3", Theme::blue(), Theme::blueLight()};
    case FanMode::Curve:  return {"Curve", "point.topleft.down.curvedto.point.bottomright.up", Theme::amber(), Theme::orange()};
    case FanMode::Smart:  return {"Smart", "sparkles", Theme::violet(), Theme::pink()};
    case FanMode::Target: return {"Target", "scope", Theme::green(), Theme::cyan()};
    case FanMode::Auto:
    default:              return {"Auto", "arrow.triangle.2.circlepath", QColor(0x5f, 0x7a, 0x9e), QColor(0x93, 0xb4, 0xdc)};
    }
}

static QString gradientCss(const QColor &a, const QColor &b) {
    return QString("qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 %1, stop:1 %2)").arg(a.name(), b.name());
}

static QLabel *capLabel(const QString &t) {
    auto *l = new QLabel(t);
    l->setObjectName("capLabel");
    return l;
}

// ---------------------------------------------------------------- SparkWidget

SparkWidget::SparkWidget(const History *h, const QString &series, const QColor &color, QWidget *parent)
    : QWidget(parent), h_(h), series_(series), color_(color), mn_(NAN), mx_(NAN) {
    setMinimumHeight(36);
}

void SparkWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    drawSparkline(p, QRectF(rect()).adjusted(2, 4, -6, -2), *h_, series_, color_, 300, mn_, mx_);
}

// ---------------------------------------------------------------- FanCard

FanCard::FanCard(FanModel *model, int fanIndex, QWidget *parent)
    : QFrame(parent), m_(model), idx_(fanIndex) {
    setObjectName("card");
    const FanInfo *f = fan();
    const double lo = f ? f->minRpm : 0, hi = f ? f->maxRpm : 6000;

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(20, 18, 20, 18);
    v->setSpacing(14);

    // Header
    auto *head = new QHBoxLayout;
    head->setSpacing(10);
    auto *chip = new IconChip("fan.fill", Theme::blue(), 30);
    chip->setGradient(Theme::blue(), Theme::violet());
    head->addWidget(chip);
    auto *titleBox = new QVBoxLayout;
    titleBox->setSpacing(0);
    auto *title = new QLabel(f ? f->name : QStringLiteral("Fan"));
    title->setObjectName("cardTitle");
    auto *sub = new QLabel(QString("%1 – %2 RPM").arg(Theme::thousands(lo), Theme::thousands(hi)));
    sub->setObjectName("cardSub");
    titleBox->addWidget(title);
    titleBox->addWidget(sub);
    head->addLayout(titleBox);
    head->addStretch();
    badge_ = new QLabel;
    badge_->setObjectName("modeBadge");
    head->addWidget(badge_);
    v->addLayout(head);

    // Body: gauge + stats
    auto *body = new QHBoxLayout;
    body->setSpacing(22);
    gauge_ = new Gauge;
    gauge_->setUnit("RPM");
    gauge_->setAccent(Theme::blue());
    gauge_->setRange(0, hi > 0 ? hi : 6000);
    gauge_->setFixedSize(164, 164);
    body->addWidget(gauge_);

    auto *stats = new QGridLayout;
    stats->setHorizontalSpacing(18);
    stats->setVerticalSpacing(4);
    curVal_ = new QLabel("—");
    curVal_->setObjectName("bigValue");
    tgtVal_ = new QLabel("—");
    tgtVal_->setObjectName("midValue");
    rangeVal_ = new QLabel("—");
    rangeVal_->setObjectName("midValue");
    stats->addWidget(capLabel("CURRENT"), 0, 0, 1, 2);
    stats->addWidget(curVal_, 1, 0, 1, 2);
    stats->addWidget(capLabel("TARGET"), 2, 0);
    stats->addWidget(capLabel("SOURCE"), 2, 1);
    stats->addWidget(tgtVal_, 3, 0);
    stats->addWidget(rangeVal_, 3, 1);
    auto *statsBox = new QVBoxLayout;
    statsBox->setSpacing(6);
    statsBox->addStretch();
    statsBox->addLayout(stats);
    spark_ = new SparkWidget(&m_->history(), QString("f:%1").arg(idx_), Theme::blueLight());
    spark_->setFixedRange(lo, hi);
    spark_->setFixedHeight(44);
    statsBox->addWidget(spark_);
    statsBox->addStretch();
    body->addLayout(statsBox, 1);
    v->addLayout(body);

    // Mode selector
    modes_ = new SegmentedControl;
    QVector<SegmentedControl::Item> items;
    for (FanMode fm : {FanMode::Auto, FanMode::Manual, FanMode::Curve, FanMode::Smart, FanMode::Target}) {
        ModeStyle st = modeStyle(fm);
        items.push_back({st.name, st.symbol, QString()});
    }
    items[0].tip = "macOS controls the fan";
    items[1].tip = "Fixed speed you choose";
    items[2].tip = "Your own temperature → speed curve";
    items[3].tip = "Automatic, adaptive control with three sensitivity profiles";
    items[4].tip = "Keep a temperature inside a range you set";
    modes_->setItems(items);
    for (int i = 0; i < 5; ++i) {
        ModeStyle st = modeStyle(FanMode(i));
        modes_->setItemGradient(i, st.a, st.b);
    }
    v->addWidget(modes_);

    // Panels
    panels_ = new PanelStack;
    panels_->addWidget(buildAutoPanel());
    panels_->addWidget(buildCustomPanel());
    panels_->addWidget(buildCurvePanel());
    panels_->addWidget(buildSmartPanel());
    panels_->addWidget(buildTargetPanel());
    v->addWidget(panels_);

    // Status line
    auto *statusRow = new QHBoxLayout;
    statusRow->setSpacing(8);
    statusDot_ = new QWidget;
    statusDot_->setFixedSize(8, 8);
    statusDot_->setObjectName("statusDot");
    status_ = new QLabel;
    status_->setObjectName("statusText");
    statusRow->addWidget(statusDot_, 0, Qt::AlignVCenter);
    statusRow->addWidget(status_, 1);
    v->addLayout(statusRow);

    // Permission banner
    banner_ = new QFrame;
    banner_->setObjectName("banner");
    auto *bl = new QHBoxLayout(banner_);
    bl->setContentsMargins(12, 9, 9, 9);
    bl->setSpacing(10);
    auto *warn = new QLabel;
    warn->setPixmap(MacNative::symbol("lock.open.fill", 16, Theme::amber()));
    bl->addWidget(warn);
    auto *bt = new QLabel("Fan control needs a one-time permission. After that, no more passwords.");
    bt->setObjectName("bannerText");
    bt->setWordWrap(true);
    bl->addWidget(bt, 1);
    auto *enable = new ProButton("Enable", "checkmark.shield.fill", ProButton::Primary);
    enable->setGradient(Theme::amber(), Theme::orange());
    bl->addWidget(enable);
    connect(enable, &QAbstractButton::clicked, this, &FanCard::enableControlRequested);
    v->addWidget(banner_);

    connect(modes_, &SegmentedControl::currentChanged, this, [this](int i) {
        if (syncing_) return;
        FanControlConfig c = m_->fanConfig(idx_);
        c.mode = FanMode(i);
        if (c.mode == FanMode::Manual && c.manualRpm <= 0 && fan()) c.manualRpm = fan()->actual;
        push(c);
    });

    syncFromModel();
    refresh();
}

const FanInfo *FanCard::fan() const {
    for (const auto &f : m_->fans())
        if (f.index == idx_) return &f;
    return nullptr;
}

void FanCard::push(const FanControlConfig &c) { m_->setFanConfig(idx_, c); }

QWidget *FanCard::sourceRow(SourcePicker *&picker, const QString &caption) {
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);
    auto *lbl = new QLabel(caption);
    lbl->setObjectName("rowLabel");
    h->addWidget(lbl);
    picker = new SourcePicker(m_);
    picker->setAllowHottest(true);
    h->addWidget(picker);
    h->addStretch();
    connect(picker, &SourcePicker::sourceChanged, this, [this](const QString &id) {
        if (syncing_) return;
        FanControlConfig c = m_->fanConfig(idx_);
        c.source = id;
        push(c);
    });
    return w;
}

QWidget *FanCard::buildAutoPanel() {
    auto *w = new QFrame;
    w->setObjectName("infoPanel");
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(14, 12, 14, 12);
    h->setSpacing(12);
    auto *ic = new IconChip("macmini", QColor(0x93, 0xb4, 0xdc), 32);
    h->addWidget(ic);
    auto *t = new QLabel("macOS is managing this fan. Choose a mode above to take control — "
                         "Smart is the easiest place to start.");
    t->setObjectName("panelText");
    t->setWordWrap(true);
    h->addWidget(t, 1);
    return w;
}

QWidget *FanCard::buildCustomPanel() {
    const FanInfo *f = fan();
    const double lo = f ? f->minRpm : 0, hi = f ? f->maxRpm : 6000;
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 2, 0, 0);
    v->setSpacing(10);

    auto *top = new QHBoxLayout;
    auto *lbl = new QLabel("Fan speed");
    lbl->setObjectName("rowLabel");
    top->addWidget(lbl);
    top->addStretch();
    manualVal_ = new QLabel("—");
    manualVal_->setObjectName("accentValue");
    top->addWidget(manualVal_);
    v->addLayout(top);

    manual_ = new ProSlider;
    manual_->setRange(lo, hi);
    manual_->setStep(50);
    manual_->setGradient(Theme::blue(), Theme::blueLight());
    v->addWidget(manual_);

    auto *presets = new QHBoxLayout;
    presets->setSpacing(8);
    const QVector<QPair<QString, double>> ps = {{"Min", 0}, {"25%", 0.25}, {"50%", 0.5}, {"75%", 0.75}, {"Max", 1.0}};
    for (const auto &pr : ps) {
        auto *b = new ProButton(pr.first, QString(), ProButton::Secondary);
        b->setFixedHeight(28);
        double frac = pr.second;
        connect(b, &QAbstractButton::clicked, this, [this, lo, hi, frac] {
            double rpm = std::round((lo + (hi - lo) * frac) / 50.0) * 50.0;
            manual_->setValue(rpm);
            manualVal_->setText(Theme::rpm(rpm));
            FanControlConfig c = m_->fanConfig(idx_);
            c.mode = FanMode::Manual;
            c.manualRpm = rpm;
            push(c);
        });
        presets->addWidget(b);
    }
    presets->addStretch();
    v->addLayout(presets);

    connect(manual_, &ProSlider::valueChanged, this, [this](double val) {
        manualVal_->setText(Theme::rpm(val));
        if (syncing_) return;
        FanControlConfig c = m_->fanConfig(idx_);
        c.mode = FanMode::Manual;
        c.manualRpm = val;
        push(c);
    });
    return w;
}

QWidget *FanCard::buildCurvePanel() {
    const FanInfo *f = fan();
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 2, 0, 0);
    v->setSpacing(10);
    v->addWidget(sourceRow(curveSrc_, "Based on"));
    curve_ = new CurveEditor;
    curve_->setRanges(30, 100, f ? f->minRpm : 0, f ? f->maxRpm : 6000);
    curve_->setTempDisplay([](double c) { return AppSettings::instance().displayTemp(c); });
    curve_->setFixedHeight(210);
    v->addWidget(curve_);
    connect(curve_, &CurveEditor::pointsEdited, this, [this](const QVector<QPointF> &pts) {
        if (syncing_) return;
        FanControlConfig c = m_->fanConfig(idx_);
        c.curve = pts;
        push(c);
    });
    return w;
}

static QString profileText(int i) {
    switch (i) {
    case 0: return "Quiet — prioritizes silence. Lets temperatures run warmer and ramps the fan late and gently.";
    case 2: return "Cool — keeps the machine as cool as possible. Reacts early and strongly to any rise, with a higher idle speed.";
    default: return "Balanced — a sensible trade-off. Anticipates rising load from the temperature trend and ramps smoothly.";
    }
}

QWidget *FanCard::buildSmartPanel() {
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 2, 0, 0);
    v->setSpacing(10);
    v->addWidget(sourceRow(smartSrc_, "Based on"));

    auto *row = new QHBoxLayout;
    auto *lbl = new QLabel("Sensitivity");
    lbl->setObjectName("rowLabel");
    row->addWidget(lbl);
    profiles_ = new SegmentedControl;
    profiles_->setCompact(true);
    profiles_->setItems({{"Quiet", "moon.fill", "Quietest, runs warmer"},
                         {"Balanced", "dial.medium", "Balanced noise and temperature"},
                         {"Cool", "snowflake", "Coolest, reacts strongly"}});
    profiles_->setItemGradient(0, QColor(0x6b, 0x6f, 0xd8), Theme::violet());
    profiles_->setItemGradient(1, Theme::violet(), Theme::pink());
    profiles_->setItemGradient(2, Theme::cyan(), Theme::blue());
    row->addWidget(profiles_, 1);
    v->addLayout(row);

    auto *desc = new QFrame;
    desc->setObjectName("infoPanel");
    auto *dh = new QHBoxLayout(desc);
    dh->setContentsMargins(12, 10, 12, 10);
    dh->setSpacing(10);
    auto *ic = new IconChip("sparkles", Theme::violet(), 28);
    ic->setGradient(Theme::violet(), Theme::pink());
    dh->addWidget(ic, 0, Qt::AlignTop);
    profileDesc_ = new QLabel(profileText(1));
    profileDesc_->setObjectName("panelText");
    profileDesc_->setWordWrap(true);
    dh->addWidget(profileDesc_, 1);
    v->addWidget(desc);

    connect(profiles_, &SegmentedControl::currentChanged, this, [this](int i) {
        profileDesc_->setText(profileText(i));
        if (syncing_) return;
        FanControlConfig c = m_->fanConfig(idx_);
        c.smart = SmartProfile(i);
        push(c);
    });
    return w;
}

QWidget *FanCard::buildTargetPanel() {
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 2, 0, 0);
    v->setSpacing(8);
    v->addWidget(sourceRow(targetSrc_, "Based on"));

    auto *top = new QHBoxLayout;
    auto *lbl = new QLabel("Keep temperature between");
    lbl->setObjectName("rowLabel");
    top->addWidget(lbl);
    top->addStretch();
    rangeLabel_ = new QLabel;
    rangeLabel_->setObjectName("accentValue");
    top->addWidget(rangeLabel_);
    v->addLayout(top);

    range_ = new RangeSlider;
    range_->setRange(30, 95);
    range_->setFormatter([](double c) { return AppSettings::instance().formatTemp(c); });
    v->addWidget(range_);

    auto *hint = new QLabel("MacFans adjusts the fan continuously to hold the temperature inside this range — "
                            "faster when it climbs above, slower when it falls below.");
    hint->setObjectName("hintText");
    hint->setWordWrap(true);
    v->addWidget(hint);

    auto setLabel = [this](double lo, double hi) {
        const AppSettings &as = AppSettings::instance();
        rangeLabel_->setText(QString("%1 – %2").arg(as.formatTemp(lo), as.formatTemp(hi)));
    };
    connect(range_, &RangeSlider::rangeChanged, this, setLabel);
    connect(range_, &RangeSlider::rangeCommitted, this, [this, setLabel](double lo, double hi) {
        setLabel(lo, hi);
        if (syncing_) return;
        FanControlConfig c = m_->fanConfig(idx_);
        c.targetLow = lo;
        c.targetHigh = hi;
        push(c);
    });
    return w;
}

void FanCard::showPanel(int mode) {
    if (panelsInit_ && panels_->currentIndex() == mode) return;
    const bool animate = panelsInit_ && isVisible();
    panelsInit_ = true;
    // Size the stack to the active page only.
    panels_->setCurrentIndex(mode);
    if (!animate) return;
    QWidget *page = panels_->currentWidget();
    auto *fx = new QGraphicsOpacityEffect(page);
    page->setGraphicsEffect(fx);
    auto *a = new QPropertyAnimation(fx, "opacity", page);
    a->setDuration(200);
    a->setStartValue(0.0);
    a->setEndValue(1.0);
    a->setEasingCurve(QEasingCurve::OutCubic);
    connect(a, &QPropertyAnimation::finished, page, [page] { page->setGraphicsEffect(nullptr); });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void FanCard::syncFromModel() {
    const FanInfo *f = fan();
    if (!f) return;
    const FanControlConfig &c = f->cfg;
    syncing_ = true;
    const int mode = int(c.mode);
    if (modes_->currentIndex() != mode) modes_->setCurrentIndex(mode, isVisible());
    showPanel(mode);

    if (!manual_->isDragging()) {
        double r = c.manualRpm > 0 ? c.manualRpm : f->actual;
        manual_->setValue(r);
        manualVal_->setText(Theme::rpm(manual_->value()));
    }
    curve_->setPoints(c.curve);
    for (SourcePicker *sp : {curveSrc_, smartSrc_, targetSrc_}) sp->setSource(c.source);
    profiles_->setCurrentIndex(int(c.smart), isVisible());
    profileDesc_->setText(profileText(int(c.smart)));
    if (!range_->isDragging()) {
        range_->setValues(c.targetLow, c.targetHigh);
        const AppSettings &as = AppSettings::instance();
        rangeLabel_->setText(QString("%1 – %2").arg(as.formatTemp(c.targetLow), as.formatTemp(c.targetHigh)));
    }

    // Badge
    ModeStyle st = modeStyle(c.mode);
    QString text = st.name.toUpper();
    if (c.mode == FanMode::Smart) {
        static const char *names[] = {"QUIET", "BALANCED", "COOL"};
        text += QString(" · %1").arg(names[int(c.smart)]);
    } else if (c.mode == FanMode::Target) {
        text += QString(" · %1–%2").arg(qRound(AppSettings::instance().displayTemp(c.targetLow)))
                    .arg(QString::number(qRound(AppSettings::instance().displayTemp(c.targetHigh))) + "°");
    }
    badge_->setText(text);
    badge_->setStyleSheet(QString("background:%1;").arg(gradientCss(st.a, st.b)));
    statusDot_->setStyleSheet(QString("background:%1; border-radius:4px;").arg(st.b.name()));
    syncing_ = false;
}

void FanCard::refresh() {
    const FanInfo *f = fan();
    if (!f) return;
    gauge_->setValue(f->actual);
    gauge_->setAccent(f->cfg.mode == FanMode::Auto ? Theme::blue() : modeStyle(f->cfg.mode).a);
    curVal_->setText(Theme::rpm(f->actual));
    const bool controlled = f->cfg.mode != FanMode::Auto && f->commanded >= 0;
    tgtVal_->setText(Theme::rpm(controlled ? f->commanded : f->target));
    const AppSettings &as = AppSettings::instance();
    rangeVal_->setText(f->cfg.mode == FanMode::Auto || f->cfg.mode == FanMode::Manual
                           ? QStringLiteral("—")
                           : QString("%1 %2").arg(m_->sourceLabel(f->cfg.source), as.formatTemp(f->sourceTemp)));
    spark_->update();
    status_->setText(f->status);

    curve_->setLive(f->ema > 0 ? f->ema : f->sourceTemp, 0, f->cfg.mode == FanMode::Curve);
    range_->setMarker(f->ema > 0 ? f->ema : f->sourceTemp, f->cfg.mode == FanMode::Target);
    manual_->setMarker(f->actual, f->cfg.mode == FanMode::Manual);
    for (SourcePicker *sp : {curveSrc_, smartSrc_, targetSrc_}) sp->refreshText();

    banner_->setVisible(!m_->controlEnabled() && f->cfg.mode != FanMode::Auto);
}
