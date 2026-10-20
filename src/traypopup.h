// traypopup.h — menu-bar experience: a native status item (fan icon + live
// temperature/RPM) and a floating, non-activating popup with quick controls.
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QVector>
#include <QWidget>

#include "macnative.h"

class FanModel;
class QLabel;
class QStackedWidget;
class SegmentedControl;
class ProSlider;
class RangeSlider;
class IconChip;

// Compact labelled bar (CPU / Memory / Swap / Disk).
class MiniBar : public QWidget {
public:
    explicit MiniBar(QWidget *parent = nullptr);
    void set(const QString &label, const QString &value, double pct, const QColor &a, const QColor &b);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QString label_, value_;
    double pct_ = 0;
    QColor a_, b_;
};

class TrayPopup : public QWidget {
    Q_OBJECT
public:
    explicit TrayPopup(FanModel *model, QWidget *parent = nullptr);
    void popUpAt(const QRect &anchor);
    void dismiss();
    void refresh();
    void syncConfig();

signals:
    void openWindowRequested(int tab);
    void quitRequested();
    void dismissed();

protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

private:
    void showModePanel(int mode);
    void push();

    FanModel *m_;
    bool syncing_ = false;
    IconChip *chip_ = nullptr;
    QLabel *modeLine_ = nullptr, *rpm_ = nullptr;
    QVector<QLabel *> tempVals_;
    QVector<QString> tempIds_;
    MiniBar *cpu_, *mem_, *swap_, *disk_;
    SegmentedControl *modes_ = nullptr;
    QStackedWidget *panels_ = nullptr;
    ProSlider *slider_ = nullptr;
    QLabel *sliderVal_ = nullptr;
    QLabel *curveLine_ = nullptr;
    SegmentedControl *profiles_ = nullptr;
    QLabel *smartLine_ = nullptr;
    RangeSlider *range_ = nullptr;
    QLabel *targetLine_ = nullptr;
};

class TrayController : public QObject {
    Q_OBJECT
public:
    explicit TrayController(FanModel *model, QObject *parent = nullptr);
    void updateStatusItem();
    TrayPopup *popup() const { return popup_; }
    void showPopupAt(const QRect &anchor);

signals:
    void openWindow(int tab);

private:
    void toggle(const QRect &anchor);
    void hidePopup();

    FanModel *m_;
    MacNative::StatusItem item_;
    TrayPopup *popup_;
    MacNative::OutsideClickMonitor monitor_;
    QElapsedTimer sinceHide_;
};
