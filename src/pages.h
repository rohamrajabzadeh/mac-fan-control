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

class DashboardPage : public QWidget {
    Q_OBJECT
public:
    explicit DashboardPage(FanModel *m, QWidget *parent = nullptr);
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
    LineChart *temps_, *fan_, *cpu_, *mem_;
    QVector<LegendChip *> chips_;
    QLabel *fanNow_, *cpuNow_, *memNow_;
};

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(FanModel *m, QWidget *parent = nullptr);
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
};
