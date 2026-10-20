// pages.h — Dashboard, History and Settings pages of the main window.
#pragma once

#include <QVector>
#include <QWidget>

class FanModel;
class FanCard;
class StatTile;
class Gauge;
class SourcePicker;
class SensorTreeView;
class QLabel;
class SegmentedControl;
class LineChart;
class LegendChip;
class ToggleSwitch;
class ProButton;
class ProcessMonitor;
class AlertManager;
class MiniBar;
class SparkWidget;
class ProSlider;

class DashboardPage : public QWidget {
    Q_OBJECT
public:
    DashboardPage(FanModel *m, ProcessMonitor *pm, QWidget *parent = nullptr);
    void refresh();
    void syncConfig();
signals:
    void enableControlRequested();
private:
    FanModel *m_;
    QVector<FanCard *> cards_;
    StatTile *cpu_, *mem_, *swap_, *disk_;
    Gauge *hero_;
    SourcePicker *heroSrc_;
    QLabel *hotPill_, *sensorCount_;
    QVector<QLabel *> quickVals_;
    QVector<QString> quickIds_;
    SensorTreeView *tree_;
    // Performance & power
    QLabel *watts_, *wattsSub_, *pressurePill_, *pressureText_;
    SparkWidget *powerSpark_;
    MiniBar *pBar_, *eBar_, *gBar_;
};

class HistoryPage : public QWidget {
    Q_OBJECT
public:
    explicit HistoryPage(FanModel *m, QWidget *parent = nullptr);
    void refresh();
    void applySettings();
private:
    FanModel *m_;
    SegmentedControl *range_;
    LineChart *temps_, *fan_, *cpu_, *mem_, *power_, *clocks_;
    QVector<LegendChip *> chips_;
    QLabel *fanNow_, *cpuNow_, *memNow_, *powerNow_, *clockNow_;
};

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    SettingsPage(FanModel *m, AlertManager *alerts, QWidget *parent = nullptr);
    void refresh();
signals:
    void enableControlRequested();
private:
    FanModel *m_;
    ToggleSwitch *mbTemp_, *mbRpm_, *login_, *showWin_;
    SourcePicker *mbSource_;
    SegmentedControl *unit_, *poll_;
    QLabel *helperStatus_;
    ProButton *helperBtn_;
    // Alerts & safety
    AlertManager *alerts_;
    ToggleSwitch *alTemp_, *alThrottle_, *alFan_, *emergency_;
    ProSlider *alThreshold_;
    QLabel *alThresholdVal_, *crashStatus_, *events_;
    SourcePicker *alSource_;
    ProButton *crashBtn_;
    void refreshEvents();
};
