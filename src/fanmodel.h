// fanmodel.h — Sensor/fan discovery, live polling, and the control engine
// (manual + temperature-driven auto curve). Pure Qt, no UI.
#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QTimer>

#include "smc.h"

enum class FanMode { Auto, Manual, Curve, Smart };

// A fan's user-chosen control settings. The curve is a simple, robust
// two-point linear ramp: below tLow -> rpmLow, above tHigh -> rpmHigh, linear
// in between. This covers "when temperature reaches X, raise the fan".
struct FanControlConfig {
    FanMode mode = FanMode::Auto;
    double manualRpm = 0;        // used in Manual mode
    QString curveSensorKey;      // SMC key of the sensor driving the curve
    double tLow = 45.0;          // °C
    double tHigh = 85.0;         // °C
    double rpmLow = 0.0;         // 0 => use fan minimum
    double rpmHigh = 0.0;        // 0 => use fan maximum
};

struct FanInfo {
    int index = 0;
    QString name;
    double actual = 0, minRpm = 0, maxRpm = 0, target = 0;
    QString targetType;          // SMC type of F%dTg (for encoding writes)
    uint32_t targetSize = 0;
    bool hasModeKey = false;     // whether F%dMd exists
    uint32_t modeSize = 0;
    QString modeType;
    FanControlConfig cfg;
    double appliedTarget = -1;   // last value we wrote (to avoid churn)
    FanMode appliedMode = FanMode::Auto;
    bool everApplied = false;
    double smoothedTemp = 0;     // EMA used by Smart mode
};

struct SensorInfo {
    QString key;     // e.g. "TC0P"
    QString label;   // friendly label
    double value = 0;
};

class FanModel : public QObject {
    Q_OBJECT
public:
    explicit FanModel(QObject *parent = nullptr);
    ~FanModel() override;

    bool init();                 // open SMC + discover
    bool hasRoot() const { return hasRoot_; }
    bool smcOpen() const { return smcOpen_; }

    // Writing fan keys is possible either because we run as root, or because
    // the one-time setuid helper has been installed.
    bool controlEnabled() const { return hasRoot_ || helperInstalled(); }
    bool helperInstalled() const;
    static QString helperPath();         // installed location
    double smartTemp() const;            // sensor value Smart mode follows

    const QVector<FanInfo> &fans() const { return fans_; }
    const QVector<SensorInfo> &sensors() const { return sensors_; }

    double maxTemp() const;      // hottest sensor, for the tray summary
    SensorInfo sensorByKey(const QString &key) const;

    void setFanConfig(int fanIndex, const FanControlConfig &cfg);
    FanControlConfig fanConfig(int fanIndex) const;

    void setPollInterval(int ms);
    void start();
    void stop();

    // Put every touched fan back to system/automatic control.
    void restoreAllAuto();

    void saveSettings();
    void loadSettings();

signals:
    void updated();              // new readings available
    void rootStatusChanged(bool haveRoot);

private slots:
    void tick();

private:
    void discoverFans();
    void discoverSensors();
    void applyControl(FanInfo &f);
    bool writeTarget(FanInfo &f, double rpm);
    bool writeMode(FanInfo &f, bool manual);
    static QString friendlyName(const QString &key);

    smc::Connection smc_;
    QVector<FanInfo> fans_;
    QVector<SensorInfo> sensors_;
    QTimer timer_;
    bool hasRoot_ = false;
    bool smcOpen_ = false;
    int pollMs_ = 2000;
};
