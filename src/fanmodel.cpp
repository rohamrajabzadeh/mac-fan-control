#include "fanmodel.h"

#include <QSettings>
#include <QHash>
#include <QFileInfo>
#include <QProcess>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <sys/stat.h>

FanModel::FanModel(QObject *parent) : QObject(parent) {
    connect(&timer_, &QTimer::timeout, this, &FanModel::tick);
}

FanModel::~FanModel() {
    // Leave the machine in a safe state.
    restoreAllAuto();
}

bool FanModel::init() {
    smcOpen_ = smc_.open();
    hasRoot_ = smc::Connection::haveRoot();
    if (!smcOpen_) return false;
    discoverFans();
    discoverSensors();
    loadSettings();
    tick();
    return true;
}

static QString keyFor(const char *fmt, int i) {
    return QString::asprintf(fmt, i);
}

QString FanModel::helperPath() {
    return QStringLiteral("/Library/Application Support/MacFans/macfans-helper");
}

bool FanModel::helperInstalled() const {
    struct stat st;
    if (stat(helperPath().toUtf8().constData(), &st) != 0) return false;
    // Must be owned by root and carry the setuid bit to be usable.
    return st.st_uid == 0 && (st.st_mode & S_ISUID);
}

// Run the installed setuid helper synchronously. Returns true on exit code 0.
static bool runHelper(const QStringList &args) {
    QProcess p;
    p.start(FanModel::helperPath(), args);
    if (!p.waitForStarted(1500)) return false;
    if (!p.waitForFinished(3000)) { p.kill(); return false; }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

void FanModel::discoverFans() {
    fans_.clear();
    smc::Value v;
    if (!smc_.read("FNum", v)) return;
    int n = (int)std::lround(v.toDouble());
    if (n <= 0 || n > 16) return;

    for (int i = 0; i < n; ++i) {
        FanInfo f;
        f.index = i;
        f.name = QStringLiteral("Fan %1").arg(i + 1);

        smc::Value tg;
        if (smc_.read(keyFor("F%dTg", i).toStdString(), tg)) {
            f.target = tg.toDouble();
            f.targetType = QString::fromStdString(tg.type).trimmed();
            f.targetSize = tg.dataSize;
        }
        smc::Value mn, mx, ac;
        if (smc_.read(keyFor("F%dMn", i).toStdString(), mn)) f.minRpm = mn.toDouble();
        if (smc_.read(keyFor("F%dMx", i).toStdString(), mx)) f.maxRpm = mx.toDouble();
        if (smc_.read(keyFor("F%dAc", i).toStdString(), ac)) f.actual = ac.toDouble();

        smc::Value md;
        if (smc_.read(keyFor("F%dMd", i).toStdString(), md)) {
            f.hasModeKey = true;
            f.modeType = QString::fromStdString(md.type).trimmed();
            f.modeSize = md.dataSize;
        }
        fans_.push_back(f);
    }
}

// Map an Apple HID thermal sensor name (e.g. "pACC MTR Temp Sensor2") to a
// friendly group label + category icon. Returns false to ignore the sensor.
// Members are tagged "H:<name>" (HID) so tick() reads the right source.
static bool groupForHid(const QString &name, QString &label, QString &icon) {
    if (name.startsWith("pACC"))        { label = "CPU Performance Cores"; icon = "cpu"; }
    else if (name.startsWith("eACC"))   { label = "CPU Efficiency Cores";  icon = "cpu"; }
    else if (name.startsWith("GPU"))    { label = "GPU";                   icon = "gpu"; }
    else if (name.startsWith("ANE"))    { label = "Neural Engine";         icon = "cpu"; }
    else if (name.startsWith("ISP"))    { label = "Image Signal Processor"; icon = "cpu"; }
    else if (name.startsWith("SOC MTR")) {
        // SOC MTR Temp Sensor0/1/2 -> "SOC Sensor 1/2/3".
        QString n = name.right(1);
        bool ok = false; int idx = n.toInt(&ok);
        label = ok ? QString("SOC Sensor %1").arg(idx + 1) : "SOC Sensor";
        icon = "cpu";
    }
    else if (name.startsWith("PMGR SOC Die")) { label = "Power Manager SOC Die"; icon = "power"; }
    else if (name.startsWith("PMU tdie") || name.startsWith("PMU2 tdie"))
                                        { label = "Power Manager Die";     icon = "power"; }
    else if (name.startsWith("NAND"))   { label = "NAND Storage";          icon = "storage"; }
    else return false; // ignore calibration / tdev / misc noise
    return true;
}

// Fallback grouping for Intel Macs (no HID thermal sensors) from SMC keys.
static void groupForKey(const QString &key, QString &label, QString &icon) {
    const QString p = key.left(2);
    if (p == "TC")                 { label = "CPU";       icon = "cpu"; }
    else if (p == "TG")            { label = "GPU";       icon = "gpu"; }
    else if (p == "TM")            { label = "Memory";    icon = "ram"; }
    else if (p == "Ta" || p == "TA") { label = "Ambient"; icon = "snow"; }
    else                           { label = "System";    icon = "temp"; }
}

QString FanModel::friendlyName(const QString &key) {
    QString label, icon;
    groupForKey(key, label, icon);
    return label;
}

void FanModel::discoverSensors() {
    sensors_.clear();
    QHash<QString, SensorInfo> groups; // label -> aggregated entry

    auto addMember = [&](const QString &label, const QString &icon,
                         const QString &member) {
        SensorInfo &g = groups[label];
        g.key = label;
        g.label = label;
        g.iconKey = icon;
        g.memberKeys.push_back(member);
    };

    // --- Apple Silicon: rich named sensors from the HID thermal system. ---
    QHash<QString, double> hid = hid_.readAll();
    for (auto it = hid.constBegin(); it != hid.constEnd(); ++it) {
        double v = it.value();
        if (v < 5.0 || v > 120.0) continue; // skip bogus / calibration values
        QString label, icon;
        if (groupForHid(it.key(), label, icon))
            addMember(label, icon, "H:" + it.key());
    }

    // --- A few well-known SMC proximity sensors (same on AS + Intel). ---
    struct Prox { const char *key; const char *label; const char *icon; };
    static const Prox prox[] = {
        {"TW0P", "Airport Proximity", "temp"},
        {"TPSP", "Power Supply Proximity", "power"},
        {"TH0x", "Heatpipe", "snow"},
    };
    for (const auto &p : prox) {
        smc::Value v;
        if (smc_.read(p.key, v) && v.toDouble() > 5 && v.toDouble() < 120)
            addMember(p.label, p.icon, QString("S:") + p.key);
    }

    // --- Intel fallback: if HID gave nothing, group raw SMC T* keys. ---
    if (hid.isEmpty()) {
        uint32_t count = smc_.keyCount();
        for (uint32_t i = 0; i < count && count <= 4096; ++i) {
            std::string k = smc_.keyAtIndex(i);
            if (k.size() < 4 || k[0] != 'T') continue;
            smc::Value v;
            if (!smc_.read(k, v)) continue;
            QString type = QString::fromStdString(v.type).trimmed();
            if (type != "flt" && type != "sp78" && type != "fp88") continue;
            double val = v.toDouble();
            if (val < 20.0 || val > 120.0) continue;
            QString label, icon;
            groupForKey(QString::fromStdString(k), label, icon);
            addMember(label, icon, "S:" + QString::fromStdString(k));
        }
    }

    // Fixed display order mirroring Macs Fan Control.
    const QStringList order = {
        "Airport Proximity", "CPU Efficiency Cores", "CPU Performance Cores",
        "GPU", "Image Signal Processor", "Neural Engine",
        "SOC Sensor 1", "SOC Sensor 2", "SOC Sensor 3",
        "Power Manager Die", "Power Manager SOC Die", "Power Supply Proximity",
        "Heatpipe", "NAND Storage",
        // Intel-fallback labels:
        "CPU", "Memory", "Ambient", "System"};
    for (const QString &name : order)
        if (groups.contains(name))
            sensors_.push_back(groups.take(name));
    for (auto &g : groups)
        sensors_.push_back(g);
}

SensorInfo FanModel::sensorByKey(const QString &key) const {
    for (const auto &s : sensors_)
        if (s.key == key) return s;
    return SensorInfo{};
}

double FanModel::maxTemp() const {
    double m = 0;
    for (const auto &s : sensors_) m = std::max(m, s.value);
    return m;
}

// The temperature the Smart preset reacts to: hottest CPU/GPU group if any are
// identifiable, otherwise the hottest group overall.
double FanModel::smartTemp() const {
    double core = 0, any = 0;
    for (const auto &s : sensors_) {
        any = std::max(any, s.value);
        if (s.iconKey == "cpu" || s.iconKey == "gpu")
            core = std::max(core, s.value);
    }
    return core > 0 ? core : any;
}

void FanModel::setFanConfig(int fanIndex, const FanControlConfig &cfg) {
    for (auto &f : fans_) {
        if (f.index == fanIndex) {
            f.cfg = cfg;
            f.appliedTarget = -1;      // force re-apply
            applyControl(f);
            break;
        }
    }
    saveSettings();
}

FanControlConfig FanModel::fanConfig(int fanIndex) const {
    for (const auto &f : fans_)
        if (f.index == fanIndex) return f.cfg;
    return FanControlConfig{};
}

void FanModel::setPollInterval(int ms) {
    pollMs_ = qBound(500, ms, 10000);
    if (timer_.isActive()) timer_.start(pollMs_);
}

void FanModel::start() { timer_.start(pollMs_); }
void FanModel::stop() { timer_.stop(); }

bool FanModel::writeMode(FanInfo &f, bool manual) {
    if (!f.hasModeKey) return false;
    QString key = keyFor("F%dMd", f.index);
    uint8_t b = manual ? 1 : 0;
    if (hasRoot_) return smc_.write(key.toStdString(), &b, 1);
    if (helperInstalled())
        return runHelper({"mode", key, manual ? "1" : "0"});
    return false;
}

bool FanModel::writeTarget(FanInfo &f, double rpm) {
    if (f.targetSize == 0) return false;
    if (f.minRpm > 0) rpm = std::max(rpm, f.minRpm);
    if (f.maxRpm > 0) rpm = std::min(rpm, f.maxRpm);
    uint8_t buf[32];
    uint32_t outSize = 0;
    if (!smc::encodeForType(f.targetType.toStdString(), f.targetSize, rpm, buf,
                            outSize))
        return false;
    QString key = keyFor("F%dTg", f.index);
    if (hasRoot_) return smc_.write(key.toStdString(), buf, outSize);
    if (helperInstalled()) {
        QString hex;
        for (uint32_t i = 0; i < outSize; ++i)
            hex += QString::asprintf("%02X", buf[i]);
        return runHelper({"write", key, hex});
    }
    return false;
}

void FanModel::applyControl(FanInfo &f) {
    if (!controlEnabled()) return;

    if (f.cfg.mode == FanMode::Auto) {
        if (!f.everApplied || f.appliedMode != FanMode::Auto) {
            writeMode(f, false);
            f.appliedMode = FanMode::Auto;
            f.appliedTarget = -1;
            f.everApplied = true;
        }
        return;
    }

    double rpm = 0;
    if (f.cfg.mode == FanMode::Manual) {
        rpm = f.cfg.manualRpm;
    } else if (f.cfg.mode == FanMode::Smart) {
        // Fully automatic preset: follow the hottest core sensor with a
        // smoothed, sensible ramp and a small idle floor. No configuration.
        double t = f.smoothedTemp > 0 ? f.smoothedTemp : smartTemp();
        const double tl = 52.0, th = 88.0;      // start ramping / full tilt
        double lo = f.minRpm;
        double hi = f.maxRpm > 0 ? f.maxRpm : 6000;
        if (t <= tl) rpm = lo;
        else if (t >= th) rpm = hi;
        else {
            // Ease-in curve so it stays quiet at moderate loads, then climbs.
            double x = (t - tl) / (th - tl);
            rpm = lo + (hi - lo) * (x * x);
        }
    } else { // Curve
        double t = sensorByKey(f.cfg.curveSensorKey).value;
        double lo = f.cfg.rpmLow > 0 ? f.cfg.rpmLow : f.minRpm;
        double hi = f.cfg.rpmHigh > 0 ? f.cfg.rpmHigh : f.maxRpm;
        double tl = f.cfg.tLow, th = f.cfg.tHigh;
        if (th <= tl) th = tl + 1;
        if (t <= tl) rpm = lo;
        else if (t >= th) rpm = hi;
        else rpm = lo + (hi - lo) * (t - tl) / (th - tl);
    }

    if (f.appliedMode != f.cfg.mode) {
        writeMode(f, true);
        f.appliedMode = f.cfg.mode;
    }
    // Only write when the target moved meaningfully (reduce SMC traffic).
    if (!f.everApplied || std::abs(rpm - f.appliedTarget) >= 15.0) {
        if (writeTarget(f, rpm)) {
            f.appliedTarget = rpm;
            f.everApplied = true;
        }
    }
}

void FanModel::tick() {
    // Refresh live readings.
    for (auto &f : fans_) {
        smc::Value ac, tg;
        if (smc_.read(keyFor("F%dAc", f.index).toStdString(), ac))
            f.actual = ac.toDouble();
        if (smc_.read(keyFor("F%dTg", f.index).toStdString(), tg))
            f.target = tg.toDouble();
    }
    QHash<QString, double> hid = hid_.readAll();
    for (auto &s : sensors_) {
        double sum = 0; int n = 0;
        for (const QString &mk : s.memberKeys) {
            if (mk.startsWith("H:")) {
                auto it = hid.constFind(mk.mid(2));
                if (it != hid.constEnd()) { sum += it.value(); ++n; }
            } else { // "S:<key>"
                smc::Value v;
                QString key = mk.startsWith("S:") ? mk.mid(2) : mk;
                if (smc_.read(key.toStdString(), v)) { sum += v.toDouble(); ++n; }
            }
        }
        if (n > 0) s.value = sum / n;
    }
    // Smooth the Smart-mode driving temperature (EMA) to avoid fan hunting.
    double st = smartTemp();
    for (auto &f : fans_) {
        if (f.smoothedTemp <= 0) f.smoothedTemp = st;
        else f.smoothedTemp = f.smoothedTemp * 0.7 + st * 0.3;
    }
    // Apply control decisions.
    for (auto &f : fans_) applyControl(f);

    emit updated();
}

void FanModel::restoreAllAuto() {
    if (!controlEnabled()) return;
    for (auto &f : fans_) {
        if (f.everApplied && f.appliedMode != FanMode::Auto) {
            writeMode(f, false);
            f.appliedMode = FanMode::Auto;
        }
    }
}

void FanModel::saveSettings() {
    QSettings s;
    s.beginWriteArray("fans");
    for (int i = 0; i < fans_.size(); ++i) {
        s.setArrayIndex(i);
        const auto &c = fans_[i].cfg;
        s.setValue("mode", (int)c.mode);
        s.setValue("manualRpm", c.manualRpm);
        s.setValue("sensor", c.curveSensorKey);
        s.setValue("tLow", c.tLow);
        s.setValue("tHigh", c.tHigh);
        s.setValue("rpmLow", c.rpmLow);
        s.setValue("rpmHigh", c.rpmHigh);
    }
    s.endArray();
    s.setValue("pollMs", pollMs_);
}

void FanModel::loadSettings() {
    QSettings s;
    int n = s.beginReadArray("fans");
    for (int i = 0; i < n && i < fans_.size(); ++i) {
        s.setArrayIndex(i);
        FanControlConfig c;
        c.mode = (FanMode)s.value("mode", 0).toInt();
        c.manualRpm = s.value("manualRpm", 0).toDouble();
        c.curveSensorKey = s.value("sensor").toString();
        c.tLow = s.value("tLow", 45).toDouble();
        c.tHigh = s.value("tHigh", 85).toDouble();
        c.rpmLow = s.value("rpmLow", 0).toDouble();
        c.rpmHigh = s.value("rpmHigh", 0).toDouble();
        fans_[i].cfg = c;
    }
    s.endArray();
    pollMs_ = s.value("pollMs", 2000).toInt();
}
