#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QStyleFactory>
#include <QTimer>

#include "fanmodel.h"
#include "mainwindow.h"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    QCoreApplication::setOrganizationName("Personal");
    QCoreApplication::setOrganizationDomain("macfans.local");
    QCoreApplication::setApplicationName("MacFans");

    // Load the embedded stylesheet.
    QFile qss(":/style.qss");
    if (qss.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
        qss.close();
    }

    auto *model = new FanModel(&app);
    if (!model->init()) {
        QMessageBox::critical(nullptr, "MacFans",
                              "Could not open the AppleSMC service.\n"
                              "This app only runs on Mac hardware.");
        return 1;
    }
    model->start();

    MainWindow w(model);
    w.show();

    // Dev helper: MACFANS_SHOT=<path> grabs the window to a PNG then quits.
    if (qEnvironmentVariableIsSet("MACFANS_SHOT")) {
        const QString path = qEnvironmentVariable("MACFANS_SHOT");
        QTimer::singleShot(1800, [&w, path] {
            w.grab().save(path);
            qApp->quit();
        });
    }

    if (qEnvironmentVariableIsSet("MACFANS_SHOT_POPUP")) {
        const QString pp = qEnvironmentVariable("MACFANS_SHOT_POPUP");
        QTimer::singleShot(1500, [&w, pp] { w.devCapturePopup(pp); });
    }

    int rc = app.exec();
    model->restoreAllAuto(); // safety net on exit
    return rc;
}
