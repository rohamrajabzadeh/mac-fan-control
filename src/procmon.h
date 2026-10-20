// procmon.h — "what is heating my Mac": top CPU consumers, grouped by app.
// Uses ps (which can see every process, including system daemons) and folds
// helper processes into their parent .app bundle. Polls only while a view that
// shows it is visible.
#pragma once

#include <QObject>
#include <QPixmap>
#include <QString>
#include <QTimer>
#include <QVector>

class QProcess;

struct ProcGroup {
    QString key;          // bundle path or executable path
    QString name;
    QString bundlePath;   // empty for plain processes
    QString iconPath;     // file whose Finder icon represents the group
    double cpu = 0;       // % of one core (can exceed 100)
    double memMB = 0;
    int count = 0;
    bool self = false;    // MacFans itself
    bool isApp() const { return !bundlePath.isEmpty(); }
};

class ProcessMonitor : public QObject {
    Q_OBJECT
public:
    explicit ProcessMonitor(QObject *parent = nullptr);
    void retain();   // a view became visible
    void release();  // a view was hidden
    const QVector<ProcGroup> &top() const { return top_; }
    bool hasData() const { return hasData_; }
    static bool quitApp(const ProcGroup &g);

signals:
    void updated();

private:
    void poll();
    void parse(const QByteArray &out);

    QTimer timer_;
    QProcess *proc_ = nullptr;
    int users_ = 0;
    bool hasData_ = false;
    QVector<ProcGroup> top_;
    QString selfBundle_;
};
