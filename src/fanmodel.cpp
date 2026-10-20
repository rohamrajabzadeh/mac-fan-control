#include "fanmodel.h"
#include "appsettings.h"
#include "theme.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <sys/stat.h>

// ------------------------------------------------------------------ helpers

static QString keyFor(const char *fmt, int i) { return QString::asprintf(fmt, i); }

static int trailingNumber(const QString &s) {
    static const QRegularExpression re("(\\d+)\\s*$");
    auto m = re.match(s);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

static bool plausible(double c) { return c > 5.0 && c < 125.0; }

struct SmartParams {
    double start, full, expo, alpha, lookahead, floor, up, down;
    const char *name;
};

static SmartParams smartParams(SmartProfile p) {
    switch (p) {
    case SmartProfile::Quiet:
        return {60, 92, 2.0, 0.12, 4, 0.00, 0.06, 0.025, "Quiet"};
    case SmartProfile::Cool:
        return {42, 76, 1.0, 0.45, 14, 0.15, 0.25, 0.05, "Cool"};
    case SmartProfile::Balanced:
    default:
        return {52, 86, 1.5, 0.25, 8, 0.05, 0.12, 0.04, "Balanced"};
    }
}

static double interpolateCurve(const QVector<QPointF> &pts, double t, double fallback) {
    if (pts.isEmpty()) return fallback;
    if (t <= pts.first().x()) return pts.first().y();
    if (t >= pts.last().x()) return pts.last().y();
    for (int i = 0; i + 1 < pts.size(); ++i) {
        const QPointF &a = pts[i], &b = pts[i + 1];
        if (t <= b.x()) {
            double span = b.x() - a.x();
            double f = span > 0 ? (t - a.x()) / span : 0;
            return a.y() + (b.y() - a.y()) * f;
        }
    }
    return pts.last().y();
}

static QString trendArrow(double slope) {
    if (slope > 0.05) return QString::fromUtf8("↗");   // ↗
    if (slope < -0.05) return QString::fromUtf8("↘");  // ↘
    return QString::fromUtf8("→");                     // →
}

// ------------------------------------------------------------------ lifecycle

FanModel::FanModel(QObject *parent) : QObject(parent) {
    connect(&timer_, &QTimer::timeout, this, &FanModel::tick);
    pollMs_ = AppSettings::instance().pollMs();
}

FanModel::~FanModel() { restoreAllAuto(); }

bool FanModel::init() {
    smcOpen_ = smc_.open();
    hasRoot_ = smc::Connection::haveRoot();
    if (!smcOpen_) return false;
    discoverFans();
    discoverSensors();
    loadSettings();
    sys_ = sysmon_.sample(); // prime the CPU delta baseline
    pw_ = power_.sample();
    refreshHelperState();
    startWatchdog();
    clock_.start();
    tick();
    return true;
}

void FanModel::refreshHelperState() {
    helperVersion_ = 0;
    if (!helperInstalled()) return;
    QProcess p;
    p.start(helperPath(), {"version"});
    if (p.waitForFinished(2000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0)
        helperVersion_ = QString::fromUtf8(p.readAllStandardOutput()).trimmed().toInt();
    else
        helperVersion_ = 1; // pre-watchdog helper (no "version" command)
}

void FanModel::startWatchdog() {
    // A detached root helper waits for this process to exit — normally or by
    // crash / force-quit — and then hands every fan back to macOS.
    if (watchdog_ || !helperInstalled() || helperVersion_ < kHelperVersion) return;
    watchdog_ = QProcess::startDetached(
        helperPath(), {"watch", QString::number(QCoreApplication::applicationPid())});
}

void FanModel::start() { timer_.start(pollMs_); }
void FanModel::stop() { timer_.stop(); }

void FanModel::setPollInterval(int ms) {
    pollMs_ = qBound(500, ms, 10000);
    if (timer_.isActive()) timer_.start(pollMs_);
}

QString FanModel::helperPath() {
    return QStringLiteral("/Library/Application Support/MacFans/macfans-helper");
}

bool FanModel::helperInstalled() const {
    struct stat st;
    if (stat(helperPath().toUtf8().constData(), &st) != 0) return false;
    return st.st_uid == 0 && (st.st_mode & S_ISUID);
}

// ------------------------------------------------------------------ discovery

void FanModel::discoverFans() {
    fans_.clear();
    smc::Value v;
    if (!smc_.read("FNum", v)) return;
    int n = (int)std::lround(v.toDouble());
    if (n <= 0 || n > 16) return;

    for (int i = 0; i < n; ++i) {
        FanInfo f;
        f.index = i;
        f.name = n == 1 ? QStringLiteral("Fan") : QStringLiteral("Fan %1").arg(i + 1);
        smc::Value tg, mn, mx, ac, md;
        if (smc_.read(keyFor("F%dTg", i).toStdString(), tg)) {
            f.target = tg.toDouble();
            f.targetType = QString::fromStdString(tg.type).trimmed();
            f.targetSize = tg.dataSize;
        }
        if (smc_.read(keyFor("F%dMn", i).toStdString(), mn)) f.minRpm = mn.toDouble();
        if (smc_.read(keyFor("F%dMx", i).toStdString(), mx)) f.maxRpm = mx.toDouble();
        if (smc_.read(keyFor("F%dAc", i).toStdString(), ac)) f.actual = ac.toDouble();
        if (smc_.read(keyFor("F%dMd", i).toStdString(), md)) {
            f.hasModeKey = true;
            f.modeType = QString::fromStdString(md.type).trimmed();
            f.modeSize = md.dataSize;
        }
        f.cfg.curve = defaultCurve(f.minRpm, f.maxRpm);
        fans_.push_back(f);
    }
}

int FanModel::addNode(const QString &id, const QString &label, const QString &category,
                      const QString &symbol, int parent, Agg agg, const QStringList &members) {
    SensorNode n;
    n.id = id;
    n.label = label;
    n.category = category;
    n.symbol = symbol;
    n.parent = parent;
    n.depth = parent >= 0 ? nodes_[parent].depth + 1 : 0;
    n.agg = agg;
    n.members = members;
    nodes_.push_back(n);
    int idx = nodes_.size() - 1;
    if (parent >= 0) nodes_[parent].children.push_back(idx);
    return idx;
}

void FanModel::discoverSensors() {
    nodes_.clear();
    smcTempKeys_.clear();

    // Bucket the Apple HID thermal sensors by family, sorted by number.
    QHash<QString, double> hid = hid_.readAll();
    auto bucket = [&](const QString &prefix) {
        QVector<QPair<int, QString>> out;
        for (auto it = hid.constBegin(); it != hid.constEnd(); ++it)
            if (it.key().startsWith(prefix) && plausible(it.value()))
                out.push_back({trailingNumber(it.key()), it.key()});
        std::sort(out.begin(), out.end());
        return out;
    };
    auto pacc = bucket("pACC");
    auto eacc = bucket("eACC");
    auto gpu  = bucket("GPU");
    auto ane  = bucket("ANE");
    auto isp  = bucket("ISP");
    auto soc  = bucket("SOC MTR");
    auto pmgr = bucket("PMGR SOC Die");
    auto pmu  = bucket("PMU tdie");
    auto pmu2 = bucket("PMU2 tdie");
    auto nand = bucket("NAND");
    pmu += pmu2;

    auto smcOk = [&](const char *key) {
        smc::Value v;
        return smc_.read(key, v) && plausible(v.toDouble());
    };
    auto hidMembers = [](const QVector<QPair<int, QString>> &b) {
        QStringList m;
        for (const auto &p : b) m << "H:" + p.second;
        return m;
    };

    if (!hid.isEmpty()) {
        // ---- CPU
        if (!pacc.isEmpty() || !eacc.isEmpty()) {
            int cpu = addNode("cpu", "CPU", "cpu", "cpu", -1, Agg::Max);
            if (!pacc.isEmpty()) {
                int p = addNode("cpu/p", "Performance Cores", "cpu",
                                "gauge.with.dots.needle.67percent", cpu, Agg::Avg);
                for (int k = 0; k < pacc.size(); ++k)
                    addNode(QString("cpu/p/%1").arg(k + 1), QString("P-Core %1").arg(k + 1),
                            "cpu", "cpu", p, Agg::Avg, {"H:" + pacc[k].second});
            }
            if (!eacc.isEmpty()) {
                int e = addNode("cpu/e", "Efficiency Cores", "cpu", "leaf.fill", cpu, Agg::Avg);
                for (int k = 0; k < eacc.size(); ++k)
                    addNode(QString("cpu/e/%1").arg(k + 1), QString("E-Core %1").arg(k + 1),
                            "cpu", "cpu", e, Agg::Avg, {"H:" + eacc[k].second});
            }
        }
        // ---- GPU
        if (!gpu.isEmpty()) {
            int g = addNode("gpu", "GPU", "gpu", "cube.transparent", -1, Agg::Max);
            for (int k = 0; k < gpu.size(); ++k)
                addNode(QString("gpu/%1").arg(k + 1), QString("GPU Cluster %1").arg(k + 1),
                        "gpu", "cube.transparent", g, Agg::Avg, {"H:" + gpu[k].second});
        }
        // ---- SoC
        if (!soc.isEmpty() || !ane.isEmpty() || !isp.isEmpty()) {
            int s = addNode("soc", "SoC", "soc", "square.stack.3d.up.fill", -1, Agg::Max);
            for (int k = 0; k < soc.size(); ++k)
                addNode(QString("soc/s%1").arg(k + 1), QString("SoC Sensor %1").arg(k + 1),
                        "soc", "square.grid.2x2", s, Agg::Avg, {"H:" + soc[k].second});
            if (!ane.isEmpty())
                addNode("soc/ane", "Neural Engine", "soc", "brain", s, Agg::Avg, hidMembers(ane));
            if (!isp.isEmpty())
                addNode("soc/isp", "Image Signal Processor", "soc", "camera.aperture", s,
                        Agg::Avg, hidMembers(isp));
        }
        // ---- Power
        bool psu = smcOk("TPSP");
        if (!pmu.isEmpty() || !pmgr.isEmpty() || psu) {
            int p = addNode("power", "Power", "power", "bolt.fill", -1, Agg::Max);
            if (!pmu.isEmpty())
                addNode("power/pmu", "Power Manager Die", "power", "bolt.fill", p, Agg::Avg,
                        hidMembers(pmu));
            if (!pmgr.isEmpty())
                addNode("power/pmgr", "Power Manager SoC Die", "power", "bolt.fill", p,
                        Agg::Avg, hidMembers(pmgr));
            if (psu)
                addNode("power/psu", "Power Supply", "power", "powerplug.fill", p, Agg::Avg,
                        {"S:TPSP"});
        }
        // ---- System (wireless)
        if (smcOk("TW0P")) {
            int s = addNode("system", "System", "system", "wifi", -1, Agg::Max);
            addNode("system/wifi", "Wi-Fi / Bluetooth", "system", "wifi", s, Agg::Avg, {"S:TW0P"});
        }
        // ---- Storage
        bool ssdProx = smcOk("TH0x");
        if (!nand.isEmpty() || ssdProx) {
            int st = addNode("storage", "Storage", "storage", "internaldrive.fill", -1, Agg::Max);
            if (!nand.isEmpty())
                addNode("storage/nand", "NAND Flash", "storage", "internaldrive", st, Agg::Avg,
                        hidMembers(nand));
            if (ssdProx)
                addNode("storage/prox", "SSD Proximity", "storage", "internaldrive", st,
                        Agg::Avg, {"S:TH0x"});
        }
    } else {
        // ---- Intel fallback: group raw SMC T* keys by their family.
        struct Fam { QString prefix, id, label, category, symbol; };
        const QVector<Fam> fams = {
            {"TC", "cpu", "CPU", "cpu", "cpu"},
            {"TG", "gpu", "GPU", "gpu", "cube.transparent"},
            {"TM", "memory", "Memory", "soc", "memorychip"},
            {"TB", "battery", "Battery", "power", "bolt.fill"},
            {"TH", "storage", "Storage", "storage", "internaldrive.fill"},
            {"Ta", "ambient", "Ambient", "system", "wind"},
            {"TA", "ambient", "Ambient", "system", "wind"},
            {"", "other", "Other", "system", "thermometer.medium"}};
        QHash<QString, QStringList> byFam;
        uint32_t count = smc_.keyCount();
        for (uint32_t i = 0; i < count && count <= 4096; ++i) {
            std::string k = smc_.keyAtIndex(i);
            if (k.size() < 4 || k[0] != 'T') continue;
            smc::Value v;
            if (!smc_.read(k, v)) continue;
            QString type = QString::fromStdString(v.type).trimmed();
            if (type != "flt" && type != "sp78" && type != "fp88") continue;
            if (v.toDouble() < 20 || v.toDouble() > 120) continue;
            QString key = QString::fromStdString(k);
            QString famId = "other";
            for (const auto &f : fams)
                if (!f.prefix.isEmpty() && key.startsWith(f.prefix)) { famId = f.id; break; }
            byFam[famId] << key;
        }
        QSet<QString> done;
        for (const auto &f : fams) {
            if (done.contains(f.id) || !byFam.contains(f.id)) continue;
            done.insert(f.id);
            int p = addNode(f.id, f.label, f.category, f.symbol, -1, Agg::Max);
            for (const QString &k : byFam[f.id])
                addNode(f.id + "/" + k, k, f.category, f.symbol, p, Agg::Avg, {"S:" + k});
        }
    }

    for (const auto &n : nodes_)
        for (const QString &m : n.members)
            if (m.startsWith("S:") && !smcTempKeys_.contains(m.mid(2)))
                smcTempKeys_ << m.mid(2);

    readSensors();
}

// ------------------------------------------------------------------ queries

QVector<int> FanModel::topLevelNodes() const {
    QVector<int> out;
    for (int i = 0; i < nodes_.size(); ++i)
        if (nodes_[i].parent < 0) out << i;
    return out;
}

int FanModel::nodeIndex(const QString &id) const {
    for (int i = 0; i < nodes_.size(); ++i)
        if (nodes_[i].id == id) return i;
    return -1;
}

double FanModel::nodeValue(const QString &id, bool *ok) const {
    int i = nodeIndex(id);
    bool valid = i >= 0 && nodes_[i].valid;
    if (ok) *ok = valid;
    return valid ? nodes_[i].value : 0.0;
}

double FanModel::hottest() const {
    double m = 0;
    for (const auto &n : nodes_)
        if (n.children.isEmpty() && n.valid) m = std::max(m, n.value);
    return m;
}

QString FanModel::hottestLabel() const {
    double m = -1;
    QString label;
    for (const auto &n : nodes_)
        if (n.children.isEmpty() && n.valid && n.value > m) {
            m = n.value;
            label = n.label;
        }
    return label;
}

double FanModel::sourceTemp(const QString &source) const {
    if (source == "__hottest__") return hottest();
    if (source != "__auto__") {
        bool ok = false;
        double v = nodeValue(source, &ok);
        if (ok) return v;
    }
    bool okC = false, okG = false;
    double c = nodeValue("cpu", &okC), g = nodeValue("gpu", &okG);
    if (okC || okG) return std::max(okC ? c : 0.0, okG ? g : 0.0);
    return hottest();
}

QString FanModel::sourceLabel(const QString &source) const {
    if (source == "__auto__") return QStringLiteral("CPU / GPU");
    if (source == "__hottest__") return QStringLiteral("Hottest Sensor");
    int i = nodeIndex(source);
    if (i < 0) return QStringLiteral("CPU / GPU");
    // Qualify children with their category ("GPU Cluster 1" is clear; "P-Core 2" -> "CPU · P-Core 2").
    const SensorNode &n = nodes_[i];
    if (n.depth >= 2) return nodes_[n.parent].label + " · " + n.label;
    return n.label;
}

QString FanModel::sourceSymbol(const QString &source) const {
    if (source == "__auto__") return QStringLiteral("sparkles");
    if (source == "__hottest__") return QStringLiteral("flame.fill");
    int i = nodeIndex(source);
    return i >= 0 ? nodes_[i].symbol : QStringLiteral("thermometer.medium");
}

// ------------------------------------------------------------------ sampling

void FanModel::readSensors() {
    QHash<QString, double> hid = hid_.readAll();
    QHash<QString, double> smcVals;
    for (const QString &k : smcTempKeys_) {
        smc::Value v;
        if (smc_.read(k.toStdString(), v)) smcVals.insert(k, v.toDouble());
    }

    // Children follow parents, so walk backwards to aggregate bottom-up.
    for (int i = nodes_.size() - 1; i >= 0; --i) {
        SensorNode &n = nodes_[i];
        double acc = 0;
        int cnt = 0;
        double mx = -1;
        if (!n.children.isEmpty()) {
            for (int c : n.children) {
                if (!nodes_[c].valid) continue;
                acc += nodes_[c].value;
                mx = std::max(mx, nodes_[c].value);
                ++cnt;
            }
        } else {
            for (const QString &m : n.members) {
                double v = 0;
                bool have = false;
                if (m.startsWith("H:")) {
                    auto it = hid.constFind(m.mid(2));
                    if (it != hid.constEnd()) { v = it.value(); have = true; }
                } else if (m.startsWith("S:")) {
                    auto it = smcVals.constFind(m.mid(2));
                    if (it != smcVals.constEnd()) { v = it.value(); have = true; }
                }
                if (have && plausible(v)) { acc += v; mx = std::max(mx, v); ++cnt; }
            }
        }
        n.valid = cnt > 0;
        if (n.valid) n.value = (n.agg == Agg::Max) ? mx : acc / cnt;
    }
}

void FanModel::recordHistory() {
    QHash<QString, float> h;
    for (const auto &n : nodes_)
        if (n.valid) h.insert("t:" + n.id, float(n.value));
    for (const auto &f : fans_)
        h.insert(QString("f:%1").arg(f.index), float(f.actual));
    h.insert("s:cpu", float(sys_.cpu));
    h.insert("s:mem", float(sys_.memPercent));
    h.insert("s:memgb", float(sys_.memUsedGB));
    h.insert("s:swapgb", float(sys_.swapUsedGB));
    h.insert("p:pressure", float(pw_.pressure));
    if (pw_.valid) {
        h.insert("p:cpu", float(pw_.cpuW));
        h.insert("p:gpu", float(pw_.gpuW));
        h.insert("p:total", float(pw_.totalW));
        h.insert("c:p", float(pw_.pFreqMHz));
        h.insert("c:e", float(pw_.eFreqMHz));
        h.insert("c:g", float(pw_.gpuFreqMHz));
    }
    history_.push(QDateTime::currentMSecsSinceEpoch(), h);
}

void FanModel::tick() {
    double dt = clock_.isValid() ? clock_.restart() / 1000.0 : pollMs_ / 1000.0;
    if (dt <= 0 || dt > 30) dt = pollMs_ / 1000.0;

    for (auto &f : fans_) {
        smc::Value ac, tg;
        if (smc_.read(keyFor("F%dAc", f.index).toStdString(), ac)) f.actual = ac.toDouble();
        if (smc_.read(keyFor("F%dTg", f.index).toStdString(), tg)) f.target = tg.toDouble();
    }
    readSensors();
    sys_ = sysmon_.sample();
    pw_ = power_.sample();

    for (auto &f : fans_) {
        track(f, dt);
        applyControl(f, dt);
    }
    recordHistory();
    emit updated();
}

// ------------------------------------------------------------------ control

QVector<QPointF> FanModel::defaultCurve(double lo, double hi) {
    if (hi <= lo) hi = lo + 4000;
    double r = hi - lo;
    return {QPointF(40, lo), QPointF(55, lo + r * 0.20), QPointF(70, lo + r * 0.60),
            QPointF(85, hi)};
}

void FanModel::track(FanInfo &f, double dt) {
    f.sourceTemp = sourceTemp(f.cfg.source);
    double alpha = 0.3;
    if (f.cfg.mode == FanMode::Smart) alpha = smartParams(f.cfg.smart).alpha;
    if (f.ema <= 0) { f.ema = f.sourceTemp; f.emaPrev = f.ema; }
    f.emaPrev = f.ema;
    f.ema += alpha * (f.sourceTemp - f.ema);
    double inst = dt > 0 ? (f.ema - f.emaPrev) / dt : 0;
    f.slope = f.slope * 0.7 + inst * 0.3;
}

void FanModel::applyControl(FanInfo &f, double dt) {
    const double lo = f.minRpm > 0 ? f.minRpm : 0;
    const double hi = f.maxRpm > lo ? f.maxRpm : lo + 4000;
    const double range = hi - lo;
    const FanMode mode = f.cfg.mode;
    const AppSettings &as = AppSettings::instance();
    const QString srcName = sourceLabel(f.cfg.source);

    if (mode == FanMode::Auto) {
        if (!f.everApplied || f.appliedMode != FanMode::Auto) {
            if (controlEnabled()) writeMode(f, false);
            f.appliedMode = FanMode::Auto;
            f.appliedTarget = -1;
            f.everApplied = true;
        }
        f.commanded = -1;
        f.status = QStringLiteral("Controlled by macOS");
        if (f.emergency) { // macOS is in charge again; nothing to override
            f.emergency = false;
            emit emergencyChanged(f.index, false, QString());
        }
        return;
    }

    if (f.commanded < 0) f.commanded = f.actual > 0 ? std::clamp(f.actual, lo, hi) : lo;

    double desired = f.commanded;
    double upRate = range * 0.15, downRate = range * 0.05; // RPM per second

    switch (mode) {
    case FanMode::Manual:
        desired = f.cfg.manualRpm > 0 ? f.cfg.manualRpm : lo;
        f.status = QStringLiteral("Fixed at %1").arg(Theme::rpm(std::clamp(desired, lo, hi)));
        break;

    case FanMode::Curve:
        desired = interpolateCurve(f.cfg.curve, f.ema, lo);
        upRate = range * 0.20;
        downRate = range * 0.06;
        f.status = QStringLiteral("%1 %2 → %3").arg(srcName, as.formatTemp(f.ema),
                                                    Theme::rpm(std::clamp(desired, lo, hi)));
        break;

    case FanMode::Smart: {
        const SmartParams p = smartParams(f.cfg.smart);
        double predicted = f.ema + std::max(0.0, f.slope) * p.lookahead;
        double x = std::clamp((predicted - p.start) / (p.full - p.start), 0.0, 1.0);
        desired = lo + range * (p.floor + (1.0 - p.floor) * std::pow(x, p.expo));
        upRate = range * p.up;
        downRate = range * p.down;
        f.status = QStringLiteral("%1 · %2 %3 %4 → %5")
                       .arg(p.name, srcName, as.formatTemp(f.ema), trendArrow(f.slope),
                            Theme::rpm(std::clamp(desired, lo, hi)));
        break;
    }

    case FanMode::Target: {
        const double low = std::min(f.cfg.targetLow, f.cfg.targetHigh);
        const double high = std::max(f.cfg.targetLow, f.cfg.targetHigh);
        const double mid = (low + high) / 2.0, half = (high - low) / 2.0;
        const double e = f.ema - mid;
        const double eff = std::abs(e) <= half ? 0.0 : e - std::copysign(half, e);
        // Velocity-form PI with a dead band: holds steady inside the range,
        // pushes harder the further outside it the temperature drifts.
        const double kp = range / 14.0;  // RPM per °C
        const double ki = range / 140.0; // RPM per °C·s
        if (dt > 0) { // only advance the controller on real ticks
            const double du = kp * (eff - f.piErrPrev) + ki * eff * dt;
            f.piErrPrev = eff;
            desired = f.commanded + du;
        }
        upRate = range * 0.30;
        downRate = range * 0.10;

        const double out = std::clamp(desired, lo, hi);
        if (eff > 0 && out >= hi - 1)
            f.status = QStringLiteral("Above range at full speed — can't cool further");
        else if (eff > 0)
            f.status = QStringLiteral("Above range (%1) — speeding up").arg(as.formatTemp(f.ema));
        else if (eff < 0 && out <= lo + 1)
            f.status = QStringLiteral("Below range — fan at minimum");
        else if (eff < 0)
            f.status = QStringLiteral("Below range (%1) — slowing down").arg(as.formatTemp(f.ema));
        else
            f.status = QStringLiteral("In range at %1 — holding %2")
                           .arg(as.formatTemp(f.ema), Theme::rpm(out));
        break;
    }
    default:
        break;
    }

    // Safety override: whatever the user's mode, critical heat forces the fan
    // to maximum until things settle (hysteresis: on at 95°, off below 85°).
    if (as.emergencyOn()) {
        const double hot = hottest();
        if (!f.emergency && (hot >= 95.0 || pw_.pressure >= 2)) {
            f.emergency = true;
            emit emergencyChanged(f.index, true,
                                  hot >= 95.0 ? QStringLiteral("%1 reached %2").arg(hottestLabel(), as.formatTemp(hot))
                                              : QStringLiteral("macOS reports %1 thermal pressure")
                                                    .arg(PowerMonitor::pressureName(pw_.pressure).toLower()));
        } else if (f.emergency && hot < 85.0 && pw_.pressure < 2) {
            f.emergency = false;
            emit emergencyChanged(f.index, false, QString());
        }
    } else if (f.emergency) {
        f.emergency = false;
        emit emergencyChanged(f.index, false, QString());
    }
    if (f.emergency) {
        desired = hi;
        upRate = range;
        f.status = QStringLiteral("Emergency cooling — fan forced to maximum");
    }

    desired = std::clamp(desired, lo, hi);
    if (mode == FanMode::Manual || dt <= 0) {
        if (mode == FanMode::Manual) f.commanded = desired;
    } else {
        double maxUp = upRate * dt, maxDown = downRate * dt;
        if (desired > f.commanded) f.commanded = std::min(desired, f.commanded + maxUp);
        else f.commanded = std::max(desired, f.commanded - maxDown);
    }

    if (!controlEnabled()) {
        f.status = QStringLiteral("Needs permission — click Enable Control");
        return;
    }
    if (!f.everApplied || f.appliedMode == FanMode::Auto) writeMode(f, true);
    f.appliedMode = mode;

    // Re-send when our output moved, or if the SMC target drifted away
    // (e.g. a write was dropped or macOS took the fan back).
    bool drifted = f.appliedTarget >= 0 && std::abs(f.target - f.appliedTarget) > 40 &&
                   std::abs(f.target - f.commanded) > 40;
    if (!f.everApplied || std::abs(f.commanded - f.appliedTarget) >= 12 || drifted) {
        if (drifted) writeMode(f, true);
        if (writeTarget(f, f.commanded)) {
            f.appliedTarget = f.commanded;
            f.everApplied = true;
        }
    }
}

void FanModel::setFanConfig(int fanIndex, const FanControlConfig &cfg) {
    for (auto &f : fans_) {
        if (f.index != fanIndex) continue;
        const bool modeChanged = f.cfg.mode != cfg.mode;
        const bool sourceChanged = f.cfg.source != cfg.source;
        f.cfg = cfg;
        std::sort(f.cfg.curve.begin(), f.cfg.curve.end(),
                  [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
        if (modeChanged) {
            f.commanded = f.actual > 0 ? f.actual : f.minRpm;
            f.piErrPrev = 0;
            f.appliedTarget = -1;
        }
        if (sourceChanged || modeChanged) {
            f.ema = 0;
            f.slope = 0;
            track(f, 0);
        }
        applyControl(f, 0);
        saveSettings();
        emit configChanged(fanIndex);
        emit updated();
        return;
    }
}

FanControlConfig FanModel::fanConfig(int fanIndex) const {
    for (const auto &f : fans_)
        if (f.index == fanIndex) return f.cfg;
    return FanControlConfig{};
}

// ------------------------------------------------------------------ writes

// Synchronous helper run (used at quit when there is no event loop left).
static bool runHelperSync(const QStringList &args) {
    QProcess p;
    p.start(FanModel::helperPath(), args);
    if (!p.waitForStarted(1500)) return false;
    if (!p.waitForFinished(3000)) { p.kill(); return false; }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

void FanModel::enqueueHelper(const QStringList &args) {
    // Coalesce: keep only the newest pending write per (command, key).
    for (auto &q : helperQueue_) {
        if (q.size() >= 2 && args.size() >= 2 && q[0] == args[0] && q[1] == args[1]) {
            q = args;
            pumpHelper();
            return;
        }
    }
    helperQueue_.append(args);
    pumpHelper();
}

void FanModel::pumpHelper() {
    if (helperProc_ || helperQueue_.isEmpty()) return;
    const QStringList args = helperQueue_.takeFirst();
    helperProc_ = new QProcess(this);
    auto done = [this](bool ok) {
        if (!helperProc_) return;
        helperProc_->deleteLater();
        helperProc_ = nullptr;
        // Repeated helper failures mean we are not really controlling the fan.
        if (ok) helperFailures_ = 0;
        else if (++helperFailures_ == 3)
            emit controlFailed(QStringLiteral("The fan-control helper failed repeatedly. "
                                              "Try Settings → Reinstall, or switch the fan to Auto."));
        pumpHelper();
    };
    connect(helperProc_, &QProcess::finished, this, [this, done](int code, QProcess::ExitStatus st) {
        done(st == QProcess::NormalExit && code == 0);
    });
    connect(helperProc_, &QProcess::errorOccurred, this, [done](QProcess::ProcessError) { done(false); });
    helperProc_->start(helperPath(), args);
}

bool FanModel::writeMode(FanInfo &f, bool manual, bool sync) {
    if (!f.hasModeKey) return false;
    const QString key = keyFor("F%dMd", f.index);
    uint8_t b = manual ? 1 : 0;
    if (hasRoot_) return smc_.write(key.toStdString(), &b, 1);
    if (!helperInstalled()) return false;
    const QStringList args{"mode", key, manual ? "1" : "0"};
    if (sync) return runHelperSync(args);
    enqueueHelper(args);
    return true;
}

bool FanModel::writeTarget(FanInfo &f, double rpm, bool sync) {
    if (f.targetSize == 0) return false;
    if (f.minRpm > 0) rpm = std::max(rpm, f.minRpm);
    if (f.maxRpm > 0) rpm = std::min(rpm, f.maxRpm);
    uint8_t buf[32];
    uint32_t outSize = 0;
    if (!smc::encodeForType(f.targetType.toStdString(), f.targetSize, rpm, buf, outSize))
        return false;
    const QString key = keyFor("F%dTg", f.index);
    if (hasRoot_) return smc_.write(key.toStdString(), buf, outSize);
    if (!helperInstalled()) return false;
    QString hex;
    for (uint32_t i = 0; i < outSize; ++i) hex += QString::asprintf("%02X", buf[i]);
    const QStringList args{"write", key, hex};
    if (sync) return runHelperSync(args);
    enqueueHelper(args);
    return true;
}

void FanModel::restoreAllAuto() {
    if (!controlEnabled()) return;
    for (auto &f : fans_) {
        if (f.everApplied && f.appliedMode != FanMode::Auto) {
            writeMode(f, false, /*sync=*/true);
            f.appliedMode = FanMode::Auto;
        }
    }
}

// ------------------------------------------------------------------ persistence

static QString encodeCurve(const QVector<QPointF> &pts) {
    QStringList parts;
    for (const auto &p : pts) parts << QString("%1:%2").arg(p.x(), 0, 'f', 1).arg(p.y(), 0, 'f', 0);
    return parts.join(';');
}

static QVector<QPointF> decodeCurve(const QString &s) {
    QVector<QPointF> out;
    for (const QString &part : s.split(';', Qt::SkipEmptyParts)) {
        auto xy = part.split(':');
        if (xy.size() == 2) out << QPointF(xy[0].toDouble(), xy[1].toDouble());
    }
    std::sort(out.begin(), out.end(), [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    return out;
}

void FanModel::saveSettings() {
    QSettings s;
    s.beginWriteArray("fans");
    for (int i = 0; i < fans_.size(); ++i) {
        s.setArrayIndex(i);
        const auto &c = fans_[i].cfg;
        s.setValue("mode", (int)c.mode);
        s.setValue("manualRpm", c.manualRpm);
        s.setValue("source", c.source);
        s.setValue("curve", encodeCurve(c.curve));
        s.setValue("smart", (int)c.smart);
        s.setValue("targetLow", c.targetLow);
        s.setValue("targetHigh", c.targetHigh);
    }
    s.endArray();
}

void FanModel::loadSettings() {
    QSettings s;
    int n = s.beginReadArray("fans");
    for (int i = 0; i < n && i < fans_.size(); ++i) {
        s.setArrayIndex(i);
        FanInfo &f = fans_[i];
        FanControlConfig c;
        int mode = s.value("mode", 0).toInt();
        c.mode = (mode >= 0 && mode <= 4) ? (FanMode)mode : FanMode::Auto;
        c.manualRpm = s.value("manualRpm", 0).toDouble();
        c.source = s.value("source", "__auto__").toString();
        if (c.source != "__auto__" && c.source != "__hottest__" && nodeIndex(c.source) < 0)
            c.source = "__auto__";
        c.curve = decodeCurve(s.value("curve").toString());
        if (c.curve.size() < 2) {
            // Migrate the old two-point curve if present.
            if (s.contains("tLow")) {
                double rl = s.value("rpmLow", 0).toDouble(), rh = s.value("rpmHigh", 0).toDouble();
                c.curve = {QPointF(s.value("tLow", 45).toDouble(), rl > 0 ? rl : f.minRpm),
                           QPointF(s.value("tHigh", 85).toDouble(), rh > 0 ? rh : f.maxRpm)};
            } else {
                c.curve = defaultCurve(f.minRpm, f.maxRpm);
            }
        }
        int sp = s.value("smart", 1).toInt();
        c.smart = (sp >= 0 && sp <= 2) ? (SmartProfile)sp : SmartProfile::Balanced;
        c.targetLow = s.value("targetLow", 55).toDouble();
        c.targetHigh = s.value("targetHigh", 70).toDouble();
        f.cfg = c;
    }
    s.endArray();
}
