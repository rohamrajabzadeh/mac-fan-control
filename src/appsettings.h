// appsettings.h — app-wide preferences with change notification.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class AppSettings : public QObject {
    Q_OBJECT
public:
    static AppSettings &instance();

    // Display
    bool fahrenheit() const { return fahrenheit_; }
    void setFahrenheit(bool on);
    double displayTemp(double celsius) const;   // °C -> display unit
    QString tempUnit() const { return fahrenheit_ ? "°F" : "°C"; }
    QString formatTemp(double celsius, int decimals = 0) const; // "52°"

    // Menu bar
    bool menuBarShowTemp() const { return mbShowTemp_; }
    void setMenuBarShowTemp(bool on);
    bool menuBarShowRpm() const { return mbShowRpm_; }
    void setMenuBarShowRpm(bool on);
    QString menuBarSource() const { return mbSource_; } // node id or "__hottest__"
    void setMenuBarSource(const QString &id);

    // General
    bool showWindowAtLaunch() const { return showWindow_; }
    void setShowWindowAtLaunch(bool on);
    int pollMs() const { return pollMs_; }
    void setPollMs(int ms);
    bool launchAtLogin() const;           // reads LaunchAgent presence
    bool setLaunchAtLogin(bool on);       // writes/removes LaunchAgent

    // UI state
    QString gaugeSource() const { return gaugeSource_; }
    void setGaugeSource(const QString &id);
    QStringList expandedNodes() const { return expanded_; }
    void setExpandedNodes(const QStringList &ids);
    int historyRange() const { return historyRange_; } // seconds
    void setHistoryRange(int s);
    QStringList hiddenSeries() const { return hiddenSeries_; }
    void setHiddenSeries(const QStringList &ids);

signals:
    void changed();

private:
    AppSettings();
    void save();

    bool fahrenheit_ = false;
    bool mbShowTemp_ = true;
    bool mbShowRpm_ = false;
    QString mbSource_ = "cpu";
    bool showWindow_ = true;
    int pollMs_ = 1500;
    QString gaugeSource_ = "__hottest__";
    QStringList expanded_;
    int historyRange_ = 300;
    QStringList hiddenSeries_;
};
