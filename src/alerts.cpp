#include "alerts.h"
#include "appsettings.h"
#include "fanmodel.h"
#include "macnative.h"
#include "theme.h"

#include <cmath>

AlertManager::AlertManager(FanModel *model, QObject *parent) : QObject(parent), m_(model) {
    connect(model, &FanModel::updated, this, &AlertManager::check);
    connect(model, &FanModel::controlFailed, this, [this](const QString &reason) {
        raise("helper", 900, "Fan control failed", reason, "exclamationmark.triangle.fill", Theme::red(), true);
    });
    connect(model, &FanModel::emergencyChanged, this, [this](int, bool on, const QString &reason) {
        if (on)
            raise("emergency", 300, "Emergency cooling",
                  reason + " — fans forced to maximum until temperatures settle.",
                  "flame.fill", Theme::red(), true);
        else
            raise("emergency-off", 0, "Temperatures back to normal",
                  "Emergency cooling ended; your fan mode is in control again.",
                  "checkmark.shield.fill", Theme::green(), false);
    });
}

void AlertManager::raise(const QString &key, int cooldownSec, const QString &title,
                         const QString &body, const QString &symbol, const QColor &color,
                         bool notifyUser) {
    const QDateTime now = QDateTime::currentDateTime();
    auto it = lastFired_.constFind(key);
    if (cooldownSec > 0 && it != lastFired_.constEnd() && it.value().secsTo(now) < cooldownSec) return;
    lastFired_.insert(key, now);
    events_.prepend({now, title, body, symbol, color});
    while (events_.size() > 30) events_.removeLast();
    if (notifyUser) MacNative::notify(title, body);
    emit eventsChanged();
}

void AlertManager::check() {
    const AppSettings &as = AppSettings::instance();

    // --- High temperature (hysteresis: re-arms 5° below the threshold)
    if (as.alertTempOn()) {
        const QString src = as.alertTempSource();
        const double t = m_->sourceTemp(src);
        if (tempArmed_ && t >= as.alertTempC()) {
            tempArmed_ = false;
            raise("temp", 600, "High temperature",
                  QString("%1 reached %2 (alert at %3).")
                      .arg(m_->sourceLabel(src), as.formatTemp(t), as.formatTemp(as.alertTempC())),
                  "thermometer.high", Theme::orange(), true);
        } else if (!tempArmed_ && t < as.alertTempC() - 5) {
            tempArmed_ = true;
        }
    }

    // --- Throttling: heavy thermal pressure sustained for 10 s
    const int pressure = m_->power().pressure;
    if (pressure >= 2) {
        if (!throttleSince_.isValid()) throttleSince_.start();
        if (as.alertThrottleOn() && throttleSince_.elapsed() > 10000)
            raise("throttle", 900, "Your Mac is throttling",
                  QString("Thermal pressure is %1 — macOS is reducing performance to control heat.")
                      .arg(PowerMonitor::pressureName(pressure)),
                  "gauge.with.dots.needle.33percent", Theme::red(), true);
    } else {
        throttleSince_.invalidate();
    }

    // --- Fan failure: not spinning while it should, or far off its target
    for (const FanInfo &f : m_->fans()) {
        const bool forced = f.cfg.mode != FanMode::Auto && f.commanded > 0;
        // Some Macs (e.g. MacBook Pro) legitimately stop their fans when cool;
        // a Mac mini's fan never drops below its minimum while powered.
        const bool mayIdleStop = f.minRpm < 300;
        const bool stopped = f.actual < 300 && (forced || !mayIdleStop || m_->hottest() > 88);
        const bool offTarget = forced && f.appliedTarget > 0 &&
                               std::abs(f.actual - f.appliedTarget) > 900;
        QElapsedTimer &since = fanBadSince_[f.index];
        if (stopped || offTarget) {
            if (!since.isValid()) since.start();
            if (as.alertFanOn() && since.elapsed() > 20000)
                raise(QString("fan%1").arg(f.index), 1800,
                      stopped ? "Fan not spinning" : "Fan not reaching its target",
                      stopped ? QString("%1 reports %2 while it should be running.")
                                    .arg(f.name, Theme::rpm(f.actual))
                              : QString("%1 runs at %2 but was asked for %3.")
                                    .arg(f.name, Theme::rpm(f.actual), Theme::rpm(f.appliedTarget)),
                      "fan.fill", Theme::red(), true);
        } else {
            since.invalidate();
        }
    }
}

void AlertManager::sendTest() {
    raise("test", 0, "MacFans alerts are on",
          "This is how you'll be told about high temperatures, throttling or fan problems.",
          "bell.fill", Theme::blue(), true);
}
