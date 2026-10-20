// procview.h — painted list of the top CPU consumers (grouped by app) with
// app icons, a CPU share bar and an optional two-step "Quit" action.
#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QWidget>

class ProcessMonitor;

class ProcessListView : public QWidget {
    Q_OBJECT
public:
    ProcessListView(ProcessMonitor *pm, int maxRows, bool compact, bool allowQuit,
                    QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

private:
    int rowAt(const QPointF &p) const;
    QRectF quitRect(int row) const;
    qreal rowH() const { return compact_ ? 30 : 42; }

    ProcessMonitor *pm_;
    int maxRows_;
    bool compact_, allowQuit_;
    int hover_ = -1;
    QString armedKey_;
    QElapsedTimer armedAt_;
};
