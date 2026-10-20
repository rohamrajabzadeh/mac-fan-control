// fanmodel.h — sensors (as a tree), fans, history and the control engine.
// Pure Qt + IOKit; no UI.
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include "hidsensors.h"
#include "history.h"
#include "smc.h"
#include "sysmon.h"

class QProcess;

enum class FanMode { Auto = 0, Manual = 1, Curve = 2, Smart = 3, Target = 4 };
enum class SmartProfile { Quiet = 0, Balanced = 1, Cool = 2 };

// Source ids accepted wherever a temperature drives the fan:
//   "__auto__"    hottest of CPU and GPU (default)
//   "__hottest__" hottest sensor of all
//   any SensorNode::id, e.g. "gpu", "cpu/p", "soc/ane"
struct FanControlConfig {
    FanMode mode = FanMode::Auto;
    double manualRpm = 0;
    QString source = "__auto__";
    QVector<QPointF> curve;                 // (°C, RPM), sorted by °C
    SmartProfile smart = SmartProfile::Balanced;
    double targetLow = 55, targetHigh = 70; // °C
};

struct FanInfo {
    int index = 0;
    QString name;
    double actual = 0, minRpm = 0, maxRpm = 0, target = 0;
    QString targetType;
    uint32_t targetSize = 0;
    bool hasModeKey = false;
    QString modeType;
    uint32_t modeSize = 0;
    FanControlConfig cfg;

    // Controller runtime state.
    double commanded = -1;    // controller output (RPM), -1 when Auto
    double appliedTarget = -1;
    FanMode appliedMode = FanMode::Auto;
    bool everApplied = false;
    double sourceTemp = 0;    // raw °C of the driving source
    double ema = 0, emaPrev = 0, slope = 0; // smoothed °C and °C/s
    double piErrPrev = 0;
    QString status;           // human-readable controller status
};

enum class Agg { Avg, Max };

// One row of the temperature tree. Nodes are stored depth-first, so every
// parent precedes its children.
struct SensorNode {
    QString id;          // "cpu", "cpu/p", "cpu/p/3"
    QString label;       // "Performance Cores"
    QString category;    // color key: cpu gpu soc power system storage
    QString symbol;      // SF Symbol name
    int parent = -1;
    int depth = 0;
    QVector<int> children;
    QStringList members; // raw sources: "H:<hid name>" or "S:<smc key>"
    Agg agg = Agg::Avg;
    double value = 0;
    bool valid = false;
};

class FanModel : public QObject {
    Q_OBJECT
public:
    explicit FanModel(QObject *parent = nullptr);
    ~FanModel() override;

    bool init();
    bool hasRoot() const { return hasRoot_; }
    bool smcOpen() const { return smcOpen_; }
    bool controlEnabled() const { return hasRoot_ || helperInstalled(); }
    bool helperInstalled() const;
    static QString helperPath();

    const QVector<FanInfo> &fans() const { return fans_; }
    const QVector<SensorNode> &nodes() const { return nodes_; }
    QVector<int> topLevelNodes() const;
    int nodeIndex(const QString &id) const;
    double nodeValue(const QString &id, bool *ok = nullptr) const;
    double hottest() const;
    QString hottestLabel() const;

    double sourceTemp(const QString &source) const;
    QString sourceLabel(const QString &source) const;
    QString sourceSymbol(const QString &source) const;

    const SysUsage &sysUsage() const { return sys_; }
    const History &history() const { return history_; }

    void setFanConfig(int fan, const FanControlConfig &cfg);
    FanControlConfig fanConfig(int fan) const;
    static QVector<QPointF> defaultCurve(double minRpm, double maxRpm);

    void setPollInterval(int ms);
    int pollInterval() const { return pollMs_; }
    void start();
    void stop();
    void restoreAllAuto();   // synchronous; safe to call on quit

    void saveSettings();
    void loadSettings();

signals:
    void updated();
    void configChanged(int fan);

private slots:
    void tick();

private:
    void discoverFans();
    void discoverSensors();
    void readSensors();
    void recordHistory();
    void track(FanInfo &f, double dt);
    void applyControl(FanInfo &f, double dt);
    bool writeTarget(FanInfo &f, double rpm, bool sync = false);
    bool writeMode(FanInfo &f, bool manual, bool sync = false);
    void enqueueHelper(const QStringList &args);
    void pumpHelper();
    int addNode(const QString &id, const QString &label, const QString &category,
                const QString &symbol, int parent, Agg agg,
                const QStringList &members = {});

    smc::Connection smc_;
    HidThermal hid_;
    SysMonitor sysmon_;
    SysUsage sys_;
    History history_{3600};
    QVector<FanInfo> fans_;
    QVector<SensorNode> nodes_;
    QStringList smcTempKeys_;
    QTimer timer_;
    QElapsedTimer clock_;
    bool hasRoot_ = false;
    bool smcOpen_ = false;
    int pollMs_ = 1500;

    QList<QStringList> helperQueue_;
    QProcess *helperProc_ = nullptr;
};
