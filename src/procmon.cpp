#include "procmon.h"
#include "macnative.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <algorithm>

static QString outermostBundle(const QString &path) {
    const int i = path.indexOf(".app/");
    if (i < 0) return path.endsWith(".app") ? path : QString();
    return path.left(i + 4);
}

ProcessMonitor::ProcessMonitor(QObject *parent) : QObject(parent) {
    timer_.setInterval(2500);
    connect(&timer_, &QTimer::timeout, this, &ProcessMonitor::poll);
    selfBundle_ = outermostBundle(QCoreApplication::applicationFilePath());
}

void ProcessMonitor::retain() {
    if (users_++ == 0) {
        poll();
        timer_.start();
    }
}

void ProcessMonitor::release() {
    if (users_ > 0 && --users_ == 0) timer_.stop();
}

void ProcessMonitor::poll() {
    if (proc_) return; // previous run still going
    proc_ = new QProcess(this);
    connect(proc_, &QProcess::finished, this, [this] {
        parse(proc_->readAllStandardOutput());
        proc_->deleteLater();
        proc_ = nullptr;
    });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!proc_) return;
        proc_->deleteLater();
        proc_ = nullptr;
    });
    // ps is setuid on macOS, so it sees every process (WindowServer, daemons…).
    proc_->start("/bin/ps", {"-Aeo", "pid=,pcpu=,rss=,comm="});
}

void ProcessMonitor::parse(const QByteArray &out) {
    static const QRegularExpression re("^\\s*(\\d+)\\s+([\\d.]+)\\s+(\\d+)\\s+(.+)$");
    QHash<QString, ProcGroup> groups;
    const qint64 selfPid = QCoreApplication::applicationPid();
    for (const QByteArray &rawLine : out.split('\n')) {
        auto m = re.match(QString::fromUtf8(rawLine));
        if (!m.hasMatch()) continue;
        const qint64 pid = m.captured(1).toLongLong();
        const double cpu = m.captured(2).toDouble();
        const double memMB = m.captured(3).toDouble() / 1024.0;
        const QString path = m.captured(4).trimmed();
        if (pid == selfPid && cpu < 0.05) continue;

        const QString bundle = outermostBundle(path);
        const QString key = bundle.isEmpty() ? path : bundle;
        ProcGroup &g = groups[key];
        if (g.count == 0) {
            g.key = key;
            g.bundlePath = bundle;
            g.iconPath = bundle.isEmpty() ? (path.startsWith('/') ? path : QString()) : bundle;
            g.name = bundle.isEmpty() ? QFileInfo(path).fileName() : MacNative::displayName(bundle);
            g.self = !bundle.isEmpty() && bundle == selfBundle_;
        }
        g.cpu += cpu;
        g.memMB += memMB;
        ++g.count;
    }
    QVector<ProcGroup> all;
    all.reserve(groups.size());
    for (auto &g : groups) all.push_back(g);
    std::sort(all.begin(), all.end(), [](const ProcGroup &a, const ProcGroup &b) { return a.cpu > b.cpu; });
    if (all.size() > 8) all.resize(8);
    top_ = all;
    hasData_ = true;
    emit updated();
}

bool ProcessMonitor::quitApp(const ProcGroup &g) {
    if (!g.isApp() || g.self) return false;
    return MacNative::terminateApp(g.bundlePath);
}
