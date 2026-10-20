// alerts.h — watches the model and raises macOS notifications (plus an
// in-app event log) for high temperature, throttling, fan failure, helper
// failure and emergency cooling. Each alert has hysteresis and a cooldown so
// it never spams.
#pragma once

#include <QColor>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class FanModel;

struct AlertEvent {
    QDateTime when;
    QString title;
    QString body;
    QString symbol;
    QColor color;
};

class AlertManager : public QObject {
    Q_OBJECT
public:
    explicit AlertManager(FanModel *model, QObject *parent = nullptr);
    const QList<AlertEvent> &events() const { return events_; }
    void sendTest();

signals:
    void eventsChanged();

private:
    void check();
    void raise(const QString &key, int cooldownSec, const QString &title, const QString &body,
               const QString &symbol, const QColor &color, bool notifyUser);

    FanModel *m_;
    QHash<QString, QDateTime> lastFired_;
    bool tempArmed_ = true;
    QElapsedTimer throttleSince_;
    QHash<int, QElapsedTimer> fanBadSince_;
    QList<AlertEvent> events_;
};
