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

QString FanModel::friendlyName(const QString &key) {
    static const QHash<QString, QString> names = {
        {"TC0P", "CPU Proximity"}, {"TC0D", "CPU Die"},   {"TC0E", "CPU"},
        {"TC0F", "CPU"},           {"TG0P", "GPU Proximity"}, {"TG0D", "GPU Die"},
        {"TCGC", "GPU Cores"},     {"TCSA", "CPU System Agent"},
        {"Th0H", "Heatpipe"},      {"Ts0P", "Palm Rest"}, {"TB0T", "Battery"},
        {"TA0P", "Ambient"},       {"TM0P", "Memory"},    {"Tp0C", "Power Supply"},
        {"TaLP", "Airflow Left"},  {"TaRF", "Airflow Right"},
        {"Tp09", "CPU Perf Core 1"}, {"Tp0T", "CPU Perf Cluster"},
        {"Te05", "CPU Efficiency"}, {"Tg05", "GPU"},
    };
    auto it = names.find(key);
    if (it != names.end()) return it.value();
    return key; // show the raw FourCC when unknown
}

void FanModel::discoverSensors() {
    sensors_.clear();
    uint32_t count = smc_.keyCount();
    if (count == 0 || count > 4096) return;

    for (uint32_t i = 0; i < count; ++i) {
        std::string k = smc_.keyAtIndex(i);
        if (k.size() < 4) continue;
        if (k[0] != 'T') continue;              // temperature keys start with T
        smc::Value v;
        if (!smc_.read(k, v)) continue;
        QString type = QString::fromStdString(v.type).trimmed();
        if (type != "flt" && type != "sp78" && type != "fp88") continue;
        double val = v.toDouble();
        if (val < 1.0 || val > 150.0) continue; // drop implausible / idle keys
        SensorInfo s;
        s.key = QString::fromStdString(k);
        s.label = friendlyName(s.key);
        s.value = val;
        sensors_.push_back(s);
    }
    std::sort(sensors_.begin(), sensors_.end(),
              [](const SensorInfo &a, const SensorInfo &b) {
                  return a.label < b.label;
              });
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

// The temperature the Smart preset reacts to: hottest CPU/GPU/SoC-class
// sensor if any are identifiable, otherwise the hottest sensor overall.
double FanModel::smartTemp() const {
    double core = 0, any = 0;
    for (const auto &s : sensors_) {
        any = std::max(any, s.value);
        if (s.key.size() >= 2) {
            QChar c = s.key[1];
            if (c == 'C' || c == 'c' || c == 'G' || c == 'g' || c == 'p' ||
                c == 'P')
                core = std::max(core, s.value);
        }
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
    for (auto &s : sensors_) {
        smc::Value v;
        if (smc_.read(s.key.toStdString(), v)) s.value = v.toDouble();
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
