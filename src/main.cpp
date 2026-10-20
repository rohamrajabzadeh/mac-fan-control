#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QStyleFactory>
#include <QTimer>
#include <algorithm>

#include "appsettings.h"
#include "fanmodel.h"
#include "macnative.h"
#include "mainwindow.h"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QCoreApplication::setOrganizationName("Personal");
    QCoreApplication::setOrganizationDomain("macfans.local");
    QCoreApplication::setApplicationName("MacFans");
    QCoreApplication::setApplicationVersion("2.0.0");
    // Closing the window must not quit: MacFans lives on in the menu bar.
    app.setQuitOnLastWindowClosed(false);

    QFile qss(":/style.qss");
    if (qss.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    FanModel model;
    if (!model.init()) {
        QMessageBox::critical(nullptr, "MacFans",
                              "Could not open the AppleSMC service.\nThis app only runs on Mac hardware.");
        return 1;
    }
    model.start();

    MainWindow w(&model);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&model] { model.restoreAllAuto(); });

    // Dev helper: MACFANS_SHOT_DIR=<dir> [MACFANS_SHOT_DELAY=<s>] captures every
    // page plus the menu-bar popup to PNGs, then quits.
    const QString shotDir = qEnvironmentVariable("MACFANS_SHOT_DIR");
    const bool background = app.arguments().contains("--background") ||
                            !AppSettings::instance().showWindowAtLaunch();
    if (!shotDir.isEmpty()) {
        const int delay = qEnvironmentVariableIntValue("MACFANS_SHOT_DELAY");
        QTimer::singleShot(std::max(1500, delay * 1000), &w, [&w, shotDir] { w.devCapture(shotDir); });
    } else if (!background) {
        w.showFromTray(0);
    } else {
        MacNative::setDockIconVisible(false);
    }
    return app.exec();
}
