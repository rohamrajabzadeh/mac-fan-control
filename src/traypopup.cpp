#include "traypopup.h"
#include "fanmodel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QButtonGroup>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QIcon>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>

static QColor popupTempColor(double t) {
    if (t < 50) return QColor("#49c17a");
    if (t < 65) return QColor("#d5b445");
    if (t < 80) return QColor("#e0893f");
    return QColor("#e05a4f");
}

// ---------------- MiniBar ----------------
MiniBar::MiniBar(QWidget *parent) : QWidget(parent) { setFixedHeight(34); }

void MiniBar::set(const QString &label, double pct, const QColor &color) {
    label_ = label;
    pct_ = qBound(0.0, pct, 100.0);
    color_ = color;
    update();
}

void MiniBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double w = width();

    // Label + value line.
    QFont f = font();
    f.setPointSizeF(11.5);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(QColor(190, 197, 208));
    p.drawText(QRectF(0, 0, w, 16), Qt::AlignLeft | Qt::AlignVCenter, label_);
    p.setPen(color_);
    p.drawText(QRectF(0, 0, w, 16), Qt::AlignRight | Qt::AlignVCenter,
               QString("%1%").arg(qRound(pct_)));

    // Track + gradient fill.
    const double by = 22, bh = 6;
    QRectF track(0, by, w, bh);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 28));
    p.drawRoundedRect(track, bh / 2, bh / 2);
    if (pct_ > 0.5) {
        QLinearGradient g(0, 0, w, 0);
        g.setColorAt(0, color_.darker(120));
        g.setColorAt(1, color_.lighter(135));
        QRectF fill(0, by, w * pct_ / 100.0, bh);
        p.setBrush(g);
        p.drawRoundedRect(fill, bh / 2, bh / 2);
    }
}

// Margin around the card for the drawn drop shadow.
static const int kPad = 14;

TrayPopup::TrayPopup(FanModel *model, QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint |
                          Qt::NoDropShadowWindowHint),
      model_(model) {
    setAttribute(Qt::WA_TranslucentBackground);
    setObjectName("trayPopup");
    setFixedWidth(272 + kPad * 2);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(kPad, kPad, kPad, kPad);

    auto *card = new QWidget;
    card->setObjectName("popupCard");
    outer->addWidget(card);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(16, 15, 16, 15);
    v->setSpacing(13);

    // Header: fan icon + live RPM + hottest temp.
    auto *head = new QHBoxLayout;
    head->setSpacing(11);
    auto *icon = new QLabel;
    icon->setPixmap(QIcon(":/icons/fan.png").pixmap(34, 34));
    icon->setFixedSize(34, 34);
    icon->setScaledContents(true);
    head->addWidget(icon);

    auto *headText = new QVBoxLayout;
    headText->setSpacing(1);
    fanRpm_ = new QLabel("— RPM");
    fanRpm_->setObjectName("popupRpm");
    tempLabel_ = new QLabel("—");
    tempLabel_->setObjectName("popupTemp");
    headText->addWidget(fanRpm_);
    headText->addWidget(tempLabel_);
    head->addLayout(headText);
    head->addStretch();
    v->addLayout(head);

    // CPU + Memory mini bars.
    auto *divider1 = new QFrame;
    divider1->setObjectName("popupDivider");
    divider1->setFixedHeight(1);
    v->addWidget(divider1);
    cpuBar_ = new MiniBar;
    ramBar_ = new MiniBar;
    v->addWidget(cpuBar_);
    v->addWidget(ramBar_);

    auto *divider2 = new QFrame;
    divider2->setObjectName("popupDivider");
    divider2->setFixedHeight(1);
    v->addWidget(divider2);

    // Mode segmented control.
    auto *modeRow = new QHBoxLayout;
    modeRow->setSpacing(0);
    btnAuto_ = new QPushButton("Auto");
    btnSmart_ = new QPushButton("Smart");
    btnCustom_ = new QPushButton("Custom");
    auto *grp = new QButtonGroup(this);
    grp->setExclusive(true);
    int id = 0;
    for (auto *b : {btnAuto_, btnSmart_, btnCustom_}) {
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("seg", true);
        grp->addButton(b, id++);
        modeRow->addWidget(b);
    }
    v->addLayout(modeRow);
    connect(grp, &QButtonGroup::idClicked, this, [this](int id) {
        if (!syncing_) setMode(id);
    });

    // Custom slider row (hidden unless Custom is active).
    sliderRow_ = new QWidget;
    auto *sr = new QHBoxLayout(sliderRow_);
    sr->setContentsMargins(0, 0, 0, 0);
    sr->setSpacing(11);
    slider_ = new QSlider(Qt::Horizontal);
    rpmValue_ = new QLabel("—");
    rpmValue_->setObjectName("popupRpmVal");
    rpmValue_->setMinimumWidth(74);
    rpmValue_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sr->addWidget(slider_, 1);
    sr->addWidget(rpmValue_);
    v->addWidget(sliderRow_);
    connect(slider_, &QSlider::valueChanged, this, [this](int val) {
        rpmValue_->setText(QString("%1 RPM").arg(val));
        if (!syncing_) applyCustomRpm(val);
    });

    // Footer actions.
    auto *foot = new QHBoxLayout;
    foot->setSpacing(10);
    auto *openBtn = new QPushButton("Open MacFans");
    openBtn->setObjectName("popupGhost");
    openBtn->setCursor(Qt::PointingHandCursor);
    auto *quitBtn = new QPushButton("Quit");
    quitBtn->setObjectName("popupGhost");
    quitBtn->setCursor(Qt::PointingHandCursor);
    foot->addWidget(openBtn);
    foot->addStretch();
    foot->addWidget(quitBtn);
    v->addLayout(foot);
    connect(openBtn, &QPushButton::clicked, this, [this] {
        hide();
        emit openWindowRequested();
    });
    connect(quitBtn, &QPushButton::clicked, this, [this] { emit quitRequested(); });

    // Configure the slider range from the fan.
    if (!model_->fans().isEmpty()) {
        const FanInfo &f = model_->fans().first();
        slider_->setRange(int(f.minRpm > 0 ? f.minRpm : 0),
                          int(f.maxRpm > 0 ? f.maxRpm : 6000));
    }
}

void TrayPopup::setMode(int id) {
    if (model_->fans().isEmpty()) return;
    FanControlConfig c = model_->fanConfig(0);
    if (id == 0) c.mode = FanMode::Auto;
    else if (id == 1) c.mode = FanMode::Smart;
    else { c.mode = FanMode::Manual; c.manualRpm = slider_->value(); }
    model_->setFanConfig(0, c);
    sliderRow_->setVisible(id == 2);
    adjustSize();
    refresh();
}

void TrayPopup::applyCustomRpm(int rpm) {
    if (model_->fans().isEmpty()) return;
    FanControlConfig c = model_->fanConfig(0);
    c.mode = FanMode::Manual;
    c.manualRpm = rpm;
    model_->setFanConfig(0, c);
}

void TrayPopup::refresh() {
    if (model_->fans().isEmpty()) return;
    const FanInfo &f = model_->fans().first();
    fanRpm_->setText(QString("%1 RPM").arg(qRound(f.actual)));

    double t = model_->maxTemp();
    tempLabel_->setText(QString("Hottest  %1°C").arg(t, 0, 'f', 0));
    tempLabel_->setStyleSheet(QString("color:%1;").arg(popupTempColor(t).name()));

    const SysUsage &su = model_->sysUsage();
    if (cpuBar_)
        cpuBar_->set("CPU", su.cpu,
                     su.cpu < 60 ? QColor("#4aa8ff")
                     : su.cpu < 85 ? QColor("#e0893f") : QColor("#e05a4f"));
    if (ramBar_)
        ramBar_->set(QString("Memory  %1/%2 GB")
                         .arg(su.memUsedGB, 0, 'f', 1).arg(su.memTotalGB, 0, 'f', 0),
                     su.memPercent,
                     su.memPercent < 80 ? QColor("#9a7bff") : QColor("#e05a4f"));

    syncing_ = true;
    int id = f.cfg.mode == FanMode::Auto ? 0
             : f.cfg.mode == FanMode::Smart ? 1
             : f.cfg.mode == FanMode::Manual ? 2 : -1;
    if (id == 0) btnAuto_->setChecked(true);
    else if (id == 1) btnSmart_->setChecked(true);
    else if (id == 2) btnCustom_->setChecked(true);
    else { // Curve mode set elsewhere — none of the three is checked
        btnAuto_->setChecked(false);
        btnSmart_->setChecked(false);
        btnCustom_->setChecked(false);
    }
    sliderRow_->setVisible(id == 2);
    if (id == 2 && !slider_->isSliderDown())
        slider_->setValue(int(f.cfg.manualRpm > 0 ? f.cfg.manualRpm : f.actual));
    rpmValue_->setText(QString("%1 RPM").arg(slider_->value()));
    syncing_ = false;
}

void TrayPopup::popUpAt(const QRect &iconRect) {
    refresh();
    adjustSize();
    QSize s = sizeHint();
    QPoint pos;
    if (!iconRect.isEmpty()) {
        pos = QPoint(iconRect.center().x() - s.width() / 2,
                     iconRect.bottom() - kPad + 2);
    } else {
        QPoint c = QCursor::pos();
        pos = QPoint(c.x() - s.width() / 2, c.y() + 4);
    }
    // Keep on-screen.
    QScreen *scr = QGuiApplication::screenAt(QCursor::pos());
    if (!scr) scr = QGuiApplication::primaryScreen();
    if (scr) {
        QRect g = scr->availableGeometry();
        int x = qBound(g.left() + 4, pos.x(), g.right() - s.width() - 4);
        pos.setX(x);
    }
    move(pos);
    show();
    raise();
    activateWindow();
}

void TrayPopup::paintEvent(QPaintEvent *) {
    // Soft drop shadow behind the rounded card.
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QRectF card = rect().adjusted(kPad, kPad, -kPad, -kPad);
    for (int i = kPad; i > 0; --i) {
        QColor sh(0, 0, 0, 7);
        p.setPen(Qt::NoPen);
        p.setBrush(sh);
        QRectF r = card.adjusted(-i, -i + 2, i, i + 2);
        p.drawRoundedRect(r, 16 + i, 16 + i);
    }
}
