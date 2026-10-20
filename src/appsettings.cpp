#include "appsettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

static const char *kAgentLabel = "local.macfans.app";

static QString agentPath() {
    return QDir::homePath() + "/Library/LaunchAgents/" + kAgentLabel + ".plist";
}

AppSettings &AppSettings::instance() {
    static AppSettings s;
    return s;
}

AppSettings::AppSettings() {
    QSettings s;
    fahrenheit_   = s.value("ui/fahrenheit", false).toBool();
    mbShowTemp_   = s.value("menubar/showTemp", true).toBool();
    mbShowRpm_    = s.value("menubar/showRpm", false).toBool();
    mbSource_     = s.value("menubar/source", "cpu").toString();
    showWindow_   = s.value("general/showWindowAtLaunch", true).toBool();
    pollMs_       = s.value("general/pollMs", 1500).toInt();
    gaugeSource_  = s.value("ui/gaugeSource", "__hottest__").toString();
    expanded_     = s.value("ui/expanded").toStringList();
    historyRange_ = s.value("ui/historyRange", 300).toInt();
    hiddenSeries_ = s.value("ui/hiddenSeries").toStringList();
    alertTempOn_     = s.value("alerts/tempOn", true).toBool();
    alertTempC_      = s.value("alerts/tempC", 90).toDouble();
    alertTempSource_ = s.value("alerts/tempSource", "__hottest__").toString();
    alertThrottleOn_ = s.value("alerts/throttleOn", true).toBool();
    alertFanOn_      = s.value("alerts/fanOn", true).toBool();
    emergencyOn_     = s.value("safety/emergencyOn", true).toBool();
    if (pollMs_ < 500 || pollMs_ > 10000) pollMs_ = 1500;
}

void AppSettings::save() {
    QSettings s;
    s.setValue("ui/fahrenheit", fahrenheit_);
    s.setValue("menubar/showTemp", mbShowTemp_);
    s.setValue("menubar/showRpm", mbShowRpm_);
    s.setValue("menubar/source", mbSource_);
    s.setValue("general/showWindowAtLaunch", showWindow_);
    s.setValue("general/pollMs", pollMs_);
    s.setValue("ui/gaugeSource", gaugeSource_);
    s.setValue("ui/expanded", expanded_);
    s.setValue("ui/historyRange", historyRange_);
    s.setValue("ui/hiddenSeries", hiddenSeries_);
    s.setValue("alerts/tempOn", alertTempOn_);
    s.setValue("alerts/tempC", alertTempC_);
    s.setValue("alerts/tempSource", alertTempSource_);
    s.setValue("alerts/throttleOn", alertThrottleOn_);
    s.setValue("alerts/fanOn", alertFanOn_);
    s.setValue("safety/emergencyOn", emergencyOn_);
}

#define SETTER(field, value) \
    if (field == value) return; \
    field = value; save(); emit changed();

void AppSettings::setFahrenheit(bool on)            { SETTER(fahrenheit_, on) }
void AppSettings::setMenuBarShowTemp(bool on)       { SETTER(mbShowTemp_, on) }
void AppSettings::setMenuBarShowRpm(bool on)        { SETTER(mbShowRpm_, on) }
void AppSettings::setMenuBarSource(const QString &id) { SETTER(mbSource_, id) }
void AppSettings::setShowWindowAtLaunch(bool on)    { SETTER(showWindow_, on) }
void AppSettings::setGaugeSource(const QString &id) { SETTER(gaugeSource_, id) }
void AppSettings::setHistoryRange(int s)            { SETTER(historyRange_, s) }
void AppSettings::setHiddenSeries(const QStringList &ids) { SETTER(hiddenSeries_, ids) }
void AppSettings::setAlertTempOn(bool on)           { SETTER(alertTempOn_, on) }
void AppSettings::setAlertTempC(double c)           { SETTER(alertTempC_, c) }
void AppSettings::setAlertTempSource(const QString &id) { SETTER(alertTempSource_, id) }
void AppSettings::setAlertThrottleOn(bool on)       { SETTER(alertThrottleOn_, on) }
void AppSettings::setAlertFanOn(bool on)            { SETTER(alertFanOn_, on) }
void AppSettings::setEmergencyOn(bool on)           { SETTER(emergencyOn_, on) }

void AppSettings::setPollMs(int ms) {
    ms = qBound(500, ms, 10000);
    SETTER(pollMs_, ms)
}

// Expanded state changes often and doesn't need to notify listeners.
void AppSettings::setExpandedNodes(const QStringList &ids) {
    expanded_ = ids;
    QSettings().setValue("ui/expanded", expanded_);
}

#undef SETTER

double AppSettings::displayTemp(double c) const {
    return fahrenheit_ ? c * 9.0 / 5.0 + 32.0 : c;
}

QString AppSettings::formatTemp(double c, int decimals) const {
    return QString::number(displayTemp(c), 'f', decimals) + "°";
}

bool AppSettings::launchAtLogin() const {
    return QFile::exists(agentPath());
}

bool AppSettings::setLaunchAtLogin(bool on) {
    const QString path = agentPath();
    if (!on) {
        bool ok = !QFile::exists(path) || QFile::remove(path);
        emit changed();
        return ok;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString bin = QCoreApplication::applicationFilePath().toHtmlEscaped();
    const QString plist = QString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n<dict>\n"
        "    <key>Label</key><string>%1</string>\n"
        "    <key>ProgramArguments</key>\n"
        "    <array><string>%2</string><string>--background</string></array>\n"
        "    <key>RunAtLoad</key><true/>\n"
        "    <key>ProcessType</key><string>Interactive</string>\n"
        "    <key>LimitLoadToSessionType</key><string>Aqua</string>\n"
        "</dict>\n</plist>\n").arg(kAgentLabel, bin);
    QFile f(path);
    bool ok = f.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
              f.write(plist.toUtf8()) > 0;
    f.close();
    emit changed();
    return ok;
}
