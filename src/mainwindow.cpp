#include "mainwindow.h"
#include "fanmodel.h"
#include "gauge.h"

#include <QApplication>
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QWidget>
#include <QLabel>
#include <QSlider>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QButtonGroup>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QFrame>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QPixmap>
#include <QIcon>
#include <QPainter>
#include <QFont>
#include <QMessageBox>

// ---- color helper for temperatures ----
static QColor tempColor(double t) {
    if (t < 50) return QColor("#49c17a");      // green
    if (t < 65) return QColor("#d5b445");      // amber
    if (t < 80) return QColor("#e0893f");      // orange
    return QColor("#e05a4f");                  // red
}

static QColor rpmAccent() { return QColor("#4aa8ff"); }

// Pick one of the bundled Macs Fan Control category icons for a sensor,
// based on its SMC FourCC prefix.
static QString sensorIconPath(const QString &key) {
    if (key.size() < 2) return ":/icons/temp.png";
    QChar c1 = key[1];
    if (c1 == 'C' || c1 == 'p' || c1 == 'P') return ":/icons/cpu.png"; // CPU / SoC
    if (c1 == 'G' || c1 == 'g')              return ":/icons/gpu.png"; // GPU
    if (c1 == 'm' || c1 == 'M')              return ":/icons/ram.png"; // memory
    if (c1 == 'h' || c1 == 'H')              return ":/icons/snow.png";// heatpipe/cooling
    if (c1 == 'B')                            return ":/icons/power.png"; // battery/power
    if (c1 == 'S' || c1 == 's')              return ":/icons/storage.png";
    return ":/icons/temp.png";
}

// A QLabel holding a scaled icon.
static QLabel *iconLabel(const QString &path, int sz) {
    auto *l = new QLabel;
    l->setPixmap(QIcon(path).pixmap(sz, sz));
    l->setFixedSize(sz, sz);
    l->setScaledContents(true);
    return l;
}

MainWindow::MainWindow(FanModel *model, QWidget *parent)
    : QMainWindow(parent), model_(model) {
    setWindowTitle("MacFans");
    setWindowIcon(QIcon(":/icons/appicon.png"));
    setMinimumSize(880, 600);

    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(buildHeader());

    // Body: fans (left) + temperatures (right).
    auto *body = new QWidget;
    body->setObjectName("body");
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(22, 18, 22, 22);
    bodyLayout->setSpacing(18);

    auto *fansCol = new QVBoxLayout;
    fansCol->setSpacing(16);
    for (int i = 0; i < model_->fans().size(); ++i)
        fansCol->addWidget(buildFanCard(i));
    if (model_->fans().isEmpty()) {
        auto *none = new QLabel("No controllable fans were reported by the SMC.");
        none->setObjectName("muted");
        fansCol->addWidget(none);
    }
    fansCol->addStretch();

    auto *fansWrap = new QWidget;
    fansWrap->setLayout(fansCol);
    fansWrap->setMinimumWidth(430);

    bodyLayout->addWidget(fansWrap, 3);
    bodyLayout->addWidget(buildTempPanel(), 2);

    root->addWidget(body, 1);
    setCentralWidget(central);

    setupTray();

    connect(model_, &FanModel::updated, this, &MainWindow::refresh);
    updateStatusPill();
    refresh();
}

QWidget *MainWindow::buildHeader() {
    auto *header = new QWidget;
    header->setObjectName("header");
    auto *h = new QHBoxLayout(header);
    h->setContentsMargins(22, 16, 22, 16);

    auto *titleBox = new QVBoxLayout;
    titleBox->setSpacing(2);
    auto *title = new QLabel("MacFans");
    title->setObjectName("title");
    auto *subtitle = new QLabel("Fan control & thermal monitor");
    subtitle->setObjectName("subtitle");
    titleBox->addWidget(title);
    titleBox->addWidget(subtitle);
    h->addLayout(titleBox);
    h->addStretch();

    enableBtn_ = new QPushButton("Enable Control");
    enableBtn_->setObjectName("enableBtn");
    enableBtn_->setCursor(Qt::PointingHandCursor);
    connect(enableBtn_, &QPushButton::clicked, this, &MainWindow::enableControl);
    h->addWidget(enableBtn_);

    statusPill_ = new QLabel;
    statusPill_->setObjectName("pill");
    h->addWidget(statusPill_);

    return header;
}

QWidget *MainWindow::buildFanCard(int fanIndex) {
    const FanInfo &f = model_->fans()[fanIndex];
    FanUi ui;
    ui.index = fanIndex;

    auto *card = new QFrame;
    card->setObjectName("card");
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(18, 16, 18, 18);
    v->setSpacing(14);

    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(8);
    titleRow->addWidget(iconLabel(":/icons/fan.png", 22));
    auto *name = new QLabel(f.name);
    name->setObjectName("cardTitle");
    titleRow->addWidget(name);
    titleRow->addStretch();
    v->addLayout(titleRow);

    // Gauge + live info.
    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(16);
    ui.gauge = new Gauge;
    ui.gauge->setUnit("RPM");
    ui.gauge->setAccent(rpmAccent());
    ui.gauge->setRange(0, f.maxRpm > 0 ? f.maxRpm : 6000);
    ui.gauge->setFixedSize(150, 150);
    topRow->addWidget(ui.gauge);

    ui.info = new QLabel;
    ui.info->setObjectName("muted");
    ui.info->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    topRow->addWidget(ui.info, 1);
    v->addLayout(topRow);

    // Mode segmented control.
    auto *modeRow = new QHBoxLayout;
    modeRow->setSpacing(0);
    ui.btnAuto = new QPushButton("Auto");
    ui.btnManual = new QPushButton("Manual");
    ui.btnCurve = new QPushButton("Curve");
    ui.btnSmart = new QPushButton("Smart");
    for (auto *b : {ui.btnAuto, ui.btnManual, ui.btnCurve, ui.btnSmart}) {
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("seg", true);
        modeRow->addWidget(b);
    }
    ui.btnSmart->setProperty("smart", true);
    auto *group = new QButtonGroup(card);
    group->setExclusive(true);
    group->addButton(ui.btnAuto, 0);
    group->addButton(ui.btnManual, 1);
    group->addButton(ui.btnCurve, 2);
    group->addButton(ui.btnSmart, 3);
    v->addLayout(modeRow);

    // Smart description (shown only in Smart mode).
    ui.smartPanel = new QWidget;
    auto *sp = new QHBoxLayout(ui.smartPanel);
    sp->setContentsMargins(2, 4, 2, 0);
    sp->setSpacing(10);
    sp->addWidget(iconLabel(":/icons/snow.png", 30));
    auto *smartDesc = new QLabel(
        "Automatic. Follows the hottest CPU/GPU sensor with a smoothed "
        "ramp — quiet when idle, spins up under load. No setup needed.");
    smartDesc->setObjectName("muted");
    smartDesc->setWordWrap(true);
    sp->addWidget(smartDesc, 1);
    v->addWidget(ui.smartPanel);

    // Manual panel.
    ui.manualPanel = new QWidget;
    auto *mp = new QHBoxLayout(ui.manualPanel);
    mp->setContentsMargins(0, 2, 0, 0);
    mp->setSpacing(12);
    ui.manualSlider = new QSlider(Qt::Horizontal);
    ui.manualSlider->setMinimum(int(f.minRpm > 0 ? f.minRpm : 0));
    ui.manualSlider->setMaximum(int(f.maxRpm > 0 ? f.maxRpm : 6000));
    ui.manualValue = new QLabel;
    ui.manualValue->setObjectName("rpmValue");
    ui.manualValue->setMinimumWidth(80);
    ui.manualValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    mp->addWidget(new QLabel("Speed"));
    mp->addWidget(ui.manualSlider, 1);
    mp->addWidget(ui.manualValue);
    v->addWidget(ui.manualPanel);

    // Curve panel.
    ui.curvePanel = new QWidget;
    auto *cg = new QGridLayout(ui.curvePanel);
    cg->setContentsMargins(0, 2, 0, 0);
    cg->setHorizontalSpacing(10);
    cg->setVerticalSpacing(10);
    ui.sensorCombo = new QComboBox;
    for (const auto &s : model_->sensors())
        ui.sensorCombo->addItem(QString("%1  (%2)").arg(s.label, s.key), s.key);
    ui.tLow = new QSpinBox;  ui.tLow->setRange(0, 120);  ui.tLow->setSuffix(" °C");
    ui.tHigh = new QSpinBox; ui.tHigh->setRange(1, 120); ui.tHigh->setSuffix(" °C");
    ui.rpmLow = new QSpinBox;  ui.rpmLow->setRange(0, 10000);  ui.rpmLow->setSingleStep(50); ui.rpmLow->setSuffix(" rpm");
    ui.rpmHigh = new QSpinBox; ui.rpmHigh->setRange(0, 10000); ui.rpmHigh->setSingleStep(50); ui.rpmHigh->setSuffix(" rpm");
    ui.rpmLow->setSpecialValueText("min");
    ui.rpmHigh->setSpecialValueText("max");

    cg->addWidget(new QLabel("Sensor"), 0, 0);
    cg->addWidget(ui.sensorCombo, 0, 1, 1, 3);
    cg->addWidget(new QLabel("From"), 1, 0);
    cg->addWidget(ui.tLow, 1, 1);
    cg->addWidget(new QLabel("→ fan"), 1, 2);
    cg->addWidget(ui.rpmLow, 1, 3);
    cg->addWidget(new QLabel("To"), 2, 0);
    cg->addWidget(ui.tHigh, 2, 1);
    cg->addWidget(new QLabel("→ fan"), 2, 2);
    cg->addWidget(ui.rpmHigh, 2, 3);
    v->addWidget(ui.curvePanel);

    // Load current config into widgets.
    FanControlConfig c = model_->fanConfig(fanIndex);
    ui.manualSlider->setValue(int(c.manualRpm > 0 ? c.manualRpm
                                                  : (f.minRpm > 0 ? f.minRpm : 0)));
    ui.manualValue->setText(QString("%1 RPM").arg(ui.manualSlider->value()));
    ui.tLow->setValue(int(c.tLow));
    ui.tHigh->setValue(int(c.tHigh));
    ui.rpmLow->setValue(int(c.rpmLow));
    ui.rpmHigh->setValue(int(c.rpmHigh));
    if (!c.curveSensorKey.isEmpty()) {
        int idx = ui.sensorCombo->findData(c.curveSensorKey);
        if (idx >= 0) ui.sensorCombo->setCurrentIndex(idx);
    }

    fanUis_.push_back(ui);
    FanUi &stored = fanUis_.last();

    // Wire interactions.
    connect(group, &QButtonGroup::idClicked, this, [this, fanIndex](int id) {
        for (auto &u : fanUis_)
            if (u.index == fanIndex) { applyModeUi(u, id); break; }
        pushConfig(fanIndex);
    });
    connect(stored.manualSlider, &QSlider::valueChanged, this,
            [this, fanIndex](int val) {
                for (auto &u : fanUis_)
                    if (u.index == fanIndex) {
                        u.manualValue->setText(QString("%1 RPM").arg(val));
                        break;
                    }
                pushConfig(fanIndex);
            });
    auto spinChanged = [this, fanIndex]() { pushConfig(fanIndex); };
    connect(stored.sensorCombo, &QComboBox::currentIndexChanged, this, spinChanged);
    connect(stored.tLow, &QSpinBox::valueChanged, this, spinChanged);
    connect(stored.tHigh, &QSpinBox::valueChanged, this, spinChanged);
    connect(stored.rpmLow, &QSpinBox::valueChanged, this, spinChanged);
    connect(stored.rpmHigh, &QSpinBox::valueChanged, this, spinChanged);

    // Initial mode selection.
    int mode = (int)c.mode;
    group->button(mode)->setChecked(true);
    applyModeUi(stored, mode);

    return card;
}

void MainWindow::applyModeUi(FanUi &ui, int mode) {
    ui.manualPanel->setVisible(mode == 1);
    ui.curvePanel->setVisible(mode == 2);
    ui.smartPanel->setVisible(mode == 3);
}

void MainWindow::pushConfig(int fanIndex) {
    FanUi *ui = nullptr;
    for (auto &u : fanUis_)
        if (u.index == fanIndex) { ui = &u; break; }
    if (!ui) return;

    FanControlConfig c = model_->fanConfig(fanIndex);
    if (ui->btnManual->isChecked()) c.mode = FanMode::Manual;
    else if (ui->btnCurve->isChecked()) c.mode = FanMode::Curve;
    else if (ui->btnSmart->isChecked()) c.mode = FanMode::Smart;
    else c.mode = FanMode::Auto;

    c.manualRpm = ui->manualSlider->value();
    c.curveSensorKey = ui->sensorCombo->currentData().toString();
    c.tLow = ui->tLow->value();
    c.tHigh = ui->tHigh->value();
    c.rpmLow = ui->rpmLow->value();
    c.rpmHigh = ui->rpmHigh->value();

    if (!model_->controlEnabled() && c.mode != FanMode::Auto) {
        // Control needs privilege; reflect that but still store the preference.
        statusPill_->setText("● enable control first");
        statusPill_->setStyleSheet("color:#d5b445;");
    }
    model_->setFanConfig(fanIndex, c);
}

QWidget *MainWindow::buildTempPanel() {
    auto *card = new QFrame;
    card->setObjectName("card");
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(18, 16, 18, 18);
    v->setSpacing(12);

    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(8);
    titleRow->addWidget(iconLabel(":/icons/temp.png", 22));
    auto *title = new QLabel("Temperatures");
    title->setObjectName("cardTitle");
    titleRow->addWidget(title);
    titleRow->addStretch();
    v->addLayout(titleRow);

    hottestGauge_ = new Gauge;
    hottestGauge_->setUnit("°C");
    hottestGauge_->setCaption("Hottest sensor");
    hottestGauge_->setRange(20, 100);
    hottestGauge_->setAccent(QColor("#e0893f"));
    hottestGauge_->setFixedHeight(134);
    v->addWidget(hottestGauge_, 0, Qt::AlignHCenter);

    // Compact two-column grid of sensor chips — fits many sensors without
    // forcing the window to grow. Scrolls only if there are a great many.
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName("tempScroll");
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *gridWrap = new QWidget;
    auto *grid = new QGridLayout(gridWrap);
    grid->setContentsMargins(0, 2, 6, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(7);

    const auto &sensors = model_->sensors();
    const int cols = 2;
    for (int i = 0; i < sensors.size(); ++i) {
        const auto &s = sensors[i];
        auto *chip = new QWidget;
        chip->setObjectName("tempRow");
        auto *rl = new QHBoxLayout(chip);
        rl->setContentsMargins(9, 5, 10, 5);
        rl->setSpacing(7);
        rl->addWidget(iconLabel(sensorIconPath(s.key), 16));
        auto *nameL = new QLabel(s.label);
        nameL->setObjectName("tempName");
        nameL->setToolTip(s.key);
        nameL->setMinimumWidth(0);
        auto *valL = new QLabel;
        valL->setObjectName("tempVal");
        valL->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valL->setMinimumWidth(48);
        rl->addWidget(nameL, 1);
        rl->addWidget(valL);
        grid->addWidget(chip, i / cols, i % cols);
        tempNameLabels_.push_back(nameL);
        tempValueLabels_.push_back(valL);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(grid->rowCount(), 1);
    scroll->setWidget(gridWrap);
    v->addWidget(scroll, 1);

    if (sensors.isEmpty()) {
        auto *none = new QLabel("No temperature sensors found.");
        none->setObjectName("muted");
        v->addWidget(none);
    }
    return card;
}

void MainWindow::refresh() {
    const auto &fans = model_->fans();
    for (auto &ui : fanUis_) {
        if (ui.index >= fans.size()) continue;
        const FanInfo &f = fans[ui.index];
        ui.gauge->setValue(f.actual);
        QString modeStr = f.cfg.mode == FanMode::Auto ? "Auto"
                          : f.cfg.mode == FanMode::Manual ? "Manual"
                          : f.cfg.mode == FanMode::Smart ? "Smart" : "Curve";
        ui.info->setText(QString("Current:  %1 RPM\nTarget:   %2 RPM\n"
                                 "Range:    %3 – %4 RPM\nMode:     %5")
                             .arg(qRound(f.actual))
                             .arg(qRound(f.target))
                             .arg(qRound(f.minRpm))
                             .arg(qRound(f.maxRpm))
                             .arg(modeStr));
    }

    const auto &sensors = model_->sensors();
    for (int i = 0; i < sensors.size() && i < tempValueLabels_.size(); ++i) {
        double t = sensors[i].value;
        QColor c = tempColor(t);
        tempValueLabels_[i]->setText(QString("%1°").arg(t, 0, 'f', 1));
        tempValueLabels_[i]->setStyleSheet(
            QString("color:%1; font-weight:600;").arg(c.name()));
    }

    double mx = model_->maxTemp();
    if (hottestGauge_) {
        hottestGauge_->setAccent(tempColor(mx));
        hottestGauge_->setValue(mx);
    }

    if (tray_) {
        tray_->setToolTip(QString("MacFans — hottest %1°C").arg(mx, 0, 'f', 0));
    }
}

void MainWindow::updateStatusPill() {
    if (!statusPill_) return;
    if (model_->controlEnabled()) {
        statusPill_->setText("● control enabled");
        statusPill_->setStyleSheet("color:#49c17a;");
        if (enableBtn_) enableBtn_->setVisible(false);
    } else {
        statusPill_->setText("● read-only");
        statusPill_->setStyleSheet("color:#d5b445;");
        if (enableBtn_) enableBtn_->setVisible(true);
    }
}

void MainWindow::enableControl() {
    // Install a tiny setuid-root helper ONCE. After this single password
    // prompt, the app changes fan speed with no further prompts — ever.
    const QString helperSrc =
        QApplication::applicationDirPath() + "/macfans-helper";
    if (!QFile::exists(helperSrc)) {
        QMessageBox::warning(this, "MacFans",
                             "Helper binary not found next to the app.");
        return;
    }
    const QString dst = FanModel::helperPath();
    const QString dir = QFileInfo(dst).absolutePath();

    // One privileged shell invocation: copy + chown root + setuid bit.
    const QString sh = QString(
        "mkdir -p '%1' && cp '%2' '%3' && chown root:wheel '%3' && "
        "chmod 4755 '%3'").arg(dir, helperSrc, dst);
    const QString script =
        QString("do shell script \"%1\" with administrator privileges")
            .arg(QString(sh).replace("\"", "\\\""));

    QProcess p;
    p.start("/usr/bin/osascript", {QStringLiteral("-e"), script});
    p.waitForFinished(60000);

    if (model_->helperInstalled()) {
        QMessageBox::information(this, "MacFans",
            "Control enabled. Fan changes will no longer ask for a password.");
        updateStatusPill();
    } else {
        QMessageBox::warning(this, "MacFans",
            "Could not install the helper (authorization cancelled or failed).");
    }
}

void MainWindow::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    tray_ = new QSystemTrayIcon(QIcon(":/icons/fan.png"), this);
    auto *menu = new QMenu(this);
    menu->addAction("Show MacFans", this, [this] { showNormal(); raise(); activateWindow(); });
    menu->addAction("Restore Fans to Auto", this, [this] { model_->restoreAllAuto(); });
    menu->addSeparator();
    menu->addAction("Quit", qApp, &QApplication::quit);
    tray_->setContextMenu(menu);
    tray_->show();

    connect(tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason r) {
                if (r == QSystemTrayIcon::Trigger) {
                    showNormal(); raise(); activateWindow();
                }
            });
}
