// mainwindow.h — Main UI. Builds one control card per fan plus a live
// temperature panel, and a menu-bar (tray) summary.
#pragma once

#include <QMainWindow>
#include <QVector>

class FanModel;
class Gauge;
class QLabel;
class QSlider;
class QComboBox;
class QSpinBox;
class QPushButton;
class QWidget;
class QSystemTrayIcon;
class QGridLayout;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(FanModel *model, QWidget *parent = nullptr);

private slots:
    void refresh();
    void enableControl();

private:
    struct FanUi {
        int index = 0;
        Gauge *gauge = nullptr;
        QLabel *info = nullptr;
        QPushButton *btnAuto = nullptr, *btnManual = nullptr, *btnCurve = nullptr,
                    *btnSmart = nullptr;
        QWidget *manualPanel = nullptr, *curvePanel = nullptr, *smartPanel = nullptr;
        QSlider *manualSlider = nullptr;
        QLabel *manualValue = nullptr;
        QComboBox *sensorCombo = nullptr;
        QSpinBox *tLow = nullptr, *tHigh = nullptr, *rpmLow = nullptr, *rpmHigh = nullptr;
    };

    QWidget *buildHeader();
    QWidget *buildFanCard(int fanIndex);
    QWidget *buildTempPanel();
    void applyModeUi(FanUi &ui, int mode);
    void pushConfig(int fanIndex);
    void updateStatusPill();
    void setupTray();

    FanModel *model_;
    QVector<FanUi> fanUis_;
    QVector<QLabel *> tempValueLabels_;
    QVector<QLabel *> tempNameLabels_;
    Gauge *hottestGauge_ = nullptr;
    QLabel *statusPill_ = nullptr;
    QPushButton *enableBtn_ = nullptr;
    QSystemTrayIcon *tray_ = nullptr;
};
