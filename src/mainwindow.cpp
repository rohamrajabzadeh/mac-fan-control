#include "mainwindow.h"
#include "alerts.h"
#include "appsettings.h"
#include "fanmodel.h"
#include "macnative.h"
#include "pages.h"
#include "procmon.h"
#include "prowidgets.h"
#include "theme.h"
#include "traypopup.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QScreen>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPropertyAnimation>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(FanModel *model, QWidget *parent) : QMainWindow(parent), m_(model) {
    setWindowTitle("MacFans");
    setWindowIcon(QIcon(":/icons/appicon.png"));
    setMinimumSize(1060, 700);
    if (QScreen *scr = QGuiApplication::primaryScreen()) {
        const QRect a = scr->availableGeometry();
        resize(std::min(1260, a.width() - 40), std::min(1060, a.height() - 40));
    }

    procs_ = new ProcessMonitor(this);
    alerts_ = new AlertManager(m_, this);

    auto *central = new QWidget;
    central->setObjectName("root");
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildHeader());

    pages_ = new QStackedWidget;
    dash_ = new DashboardPage(m_, procs_);
    hist_ = new HistoryPage(m_);
    settings_ = new SettingsPage(m_, alerts_);
    pages_->addWidget(dash_);
    pages_->addWidget(hist_);
    pages_->addWidget(settings_);
    root->addWidget(pages_, 1);
    setCentralWidget(central);

    tray_ = new TrayController(m_, procs_, this);
    connect(tray_, &TrayController::openWindow, this, [this](int tab) { showFromTray(tab); });

    connect(dash_, &DashboardPage::enableControlRequested, this, &MainWindow::enableControl);
    connect(settings_, &SettingsPage::enableControlRequested, this, &MainWindow::enableControl);
    connect(m_, &FanModel::updated, this, &MainWindow::refresh);
    connect(m_, &FanModel::configChanged, this, [this](int) { dash_->syncConfig(); });
    connect(&AppSettings::instance(), &AppSettings::changed, this, &MainWindow::applySettings);
    qApp->installEventFilter(this);

    updateStatusPill();
}

QWidget *MainWindow::buildHeader() {
    auto *header = new QWidget;
    header->setObjectName("header");
    auto *h = new QHBoxLayout(header);
    h->setContentsMargins(22, 14, 22, 14);
    h->setSpacing(12);

    auto *icon = new QLabel;
    QPixmap app(":/icons/appicon.png");
    icon->setPixmap(app);
    icon->setFixedSize(40, 40);
    icon->setScaledContents(true);
    h->addWidget(icon);

    auto *tb = new QVBoxLayout;
    tb->setSpacing(0);
    auto *title = new QLabel("MacFans");
    title->setObjectName("title");
    auto *sub = new QLabel("Thermal monitor & fan control");
    sub->setObjectName("subtitle");
    tb->addWidget(title);
    tb->addWidget(sub);
    h->addLayout(tb);
    h->addStretch(1);

    tabs_ = new SegmentedControl;
    tabs_->setItems({{"Dashboard", "square.grid.2x2.fill", "Overview and fan control"},
                     {"History", "chart.xyaxis.line", "Charts of temperatures, fan and load"},
                     {"Settings", "gearshape.fill", "Menu bar, startup and permissions"}});
    tabs_->setFixedWidth(420);
    h->addWidget(tabs_);
    h->addStretch(1);

    pressure_ = new QLabel;
    pressure_->setObjectName("hotPill");
    h->addWidget(pressure_);
    pill_ = new ProButton("Control Enabled", "checkmark.shield.fill", ProButton::Secondary);
    h->addWidget(pill_);
    connect(pill_, &QAbstractButton::clicked, this, [this] {
        if (!m_->controlEnabled()) enableControl();
        else switchTab(2);
    });
    connect(tabs_, &SegmentedControl::currentChanged, this, [this](int i) { switchTab(i); });
    return header;
}

void MainWindow::switchTab(int i, bool animate) {
    if (tabs_->currentIndex() != i) tabs_->setCurrentIndex(i, animate);
    if (pages_->currentIndex() == i) return;
    pages_->setCurrentIndex(i);
    refresh();
    if (!animate) return;
    QWidget *page = pages_->currentWidget();
    auto *fx = new QGraphicsOpacityEffect(page);
    page->setGraphicsEffect(fx);
    auto *a = new QPropertyAnimation(fx, "opacity", page);
    a->setDuration(220);
    a->setStartValue(0.0);
    a->setEndValue(1.0);
    a->setEasingCurve(QEasingCurve::OutCubic);
    connect(a, &QPropertyAnimation::finished, page, [page] { page->setGraphicsEffect(nullptr); });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::refresh() {
    if (!isVisible()) return; // no UI work while living in the menu bar only
    switch (pages_->currentIndex()) {
    case 0: dash_->refresh(); break;
    case 1: hist_->refresh(); break;
    case 2: settings_->refresh(); break;
    }
    updateStatusPill();
}

void MainWindow::applySettings() {
    m_->setPollInterval(AppSettings::instance().pollMs());
    hist_->applySettings();
    dash_->syncConfig();
    refresh();
}

void MainWindow::updateStatusPill() {
    const PowerSample &pw = m_->power();
    const QColor pc = Theme::pressureColor(pw.pressure);
    pressure_->setText(QString::fromUtf8("\u25CF  ") + PowerMonitor::pressureName(pw.pressure));
    pressure_->setToolTip("Thermal pressure: " + PowerMonitor::pressureDescription(pw.pressure));
    const QString css = QString("color:%1; border:1px solid rgba(%2,%3,%4,90); background:rgba(%2,%3,%4,26);"
                                "padding:6px 12px; border-radius:12px;")
                            .arg(pc.name()).arg(pc.red()).arg(pc.green()).arg(pc.blue());
    if (pressure_->styleSheet() != css) pressure_->setStyleSheet(css);

    if (m_->controlEnabled()) {
        pill_->setText("Control Enabled");
        pill_->setSymbol("checkmark.shield.fill");
        pill_->setVariant(ProButton::Secondary);
        pill_->setToolTip("Passwordless fan control is active");
    } else {
        pill_->setText("Enable Control");
        pill_->setSymbol("lock.open.fill");
        pill_->setVariant(ProButton::Primary);
        pill_->setGradient(Theme::amber(), Theme::orange());
        pill_->setToolTip("One-time permission to control the fans");
    }
}

void MainWindow::showFromTray(int tab) {
    if (tab >= 0) switchTab(tab, false);
    MacNative::setDockIconVisible(true);
    showNormal();
    raise();
    activateWindow();
    MacNative::activateApp();
    dash_->syncConfig();
    refresh();
}

bool MainWindow::eventFilter(QObject *o, QEvent *e) {
    // Qt 6 aborts quit() if a window refuses to close. Remember that a real
    // quit (⌘Q, popup Quit, logout) is in progress so closeEvent lets it go.
    if (o == qApp && e->type() == QEvent::Quit) quitting_ = true;
    return QMainWindow::eventFilter(o, e);
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (quitting_) {
        e->accept();
        return;
    }
    // Closing the window keeps MacFans running in the menu bar (no Dock icon).
    e->ignore();
    hide();
    MacNative::setDockIconVisible(false);
}

void MainWindow::enableControl() {
    const QString helperSrc = QApplication::applicationDirPath() + "/macfans-helper";
    if (!QFile::exists(helperSrc)) {
        QMessageBox::warning(this, "MacFans", "Helper binary not found next to the app.");
        return;
    }
    const QString dst = FanModel::helperPath();
    const QString dir = QFileInfo(dst).absolutePath();
    const QString sh = QString("mkdir -p '%1' && cp '%2' '%3' && chown root:wheel '%3' && chmod 4755 '%3'")
                           .arg(dir, helperSrc, dst);
    const QString script = QString("do shell script \"%1\" with administrator privileges")
                               .arg(QString(sh).replace("\"", "\\\""));
    QProcess p;
    p.start("/usr/bin/osascript", {QStringLiteral("-e"), script});
    p.waitForFinished(120000);

    if (m_->helperInstalled()) {
        m_->refreshHelperState();
        m_->startWatchdog();
        updateStatusPill();
        settings_->refresh();
        refresh();
    } else {
        QMessageBox::warning(this, "MacFans", "Could not install the helper (authorization was cancelled or failed).");
    }
}

void MainWindow::devCapture(const QString &dir) {
    QDir().mkpath(dir);
    showFromTray(0);
    const char *names[] = {"dashboard", "history", "settings"};
    int step = 0;
    auto *t = new QTimer(this);
    t->setInterval(700);
    connect(t, &QTimer::timeout, this, [=]() mutable {
        if (step < 3) {
            switchTab(step, false);
            QTimer::singleShot(450, this, [=] { grab().save(dir + "/" + names[step] + ".png"); });
            ++step;
            return;
        }
        if (step == 3) {
            QRect anchor(width() / 2 + x() - 12, 2, 24, 22);
            tray_->showPopupAt(anchor);
            ++step;
            return;
        }
        tray_->popup()->grab().save(dir + "/popup.png");
        t->stop();
        qApp->quit();
    });
    t->start();
}
