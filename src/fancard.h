// fancard.h — per-fan control card: RPM gauge + stats, mode selector and a
// panel per mode (Custom slider, Curve editor, Smart profiles, Target range).
#pragma once

#include <QFrame>
#include <QString>

#include "fanmodel.h"

class Gauge;
class QLabel;
class QStackedWidget;
class SegmentedControl;
class ProSlider;
class RangeSlider;
class SourcePicker;
class CurveEditor;
class IconChip;
class SparkWidget;

class FanCard : public QFrame {
    Q_OBJECT
public:
    FanCard(FanModel *model, int fanIndex, QWidget *parent = nullptr);
    void refresh();        // live readings
    void syncFromModel();  // config -> widgets, without feedback

signals:
    void enableControlRequested();

private:
    QWidget *buildAutoPanel();
    QWidget *buildCustomPanel();
    QWidget *buildCurvePanel();
    QWidget *buildSmartPanel();
    QWidget *buildTargetPanel();
    QWidget *sourceRow(SourcePicker *&picker, const QString &caption);
    void showPanel(int mode);
    void push(const FanControlConfig &c);
    const FanInfo *fan() const;

    FanModel *m_;
    int idx_;
    bool syncing_ = false;
    bool panelsInit_ = false;

    Gauge *gauge_ = nullptr;
    QLabel *curVal_ = nullptr, *tgtVal_ = nullptr, *rangeVal_ = nullptr;
    QLabel *badge_ = nullptr, *status_ = nullptr;
    QWidget *statusDot_ = nullptr;
    SparkWidget *spark_ = nullptr;
    SegmentedControl *modes_ = nullptr;
    QStackedWidget *panels_ = nullptr;

    ProSlider *manual_ = nullptr;
    QLabel *manualVal_ = nullptr;
    SourcePicker *curveSrc_ = nullptr, *smartSrc_ = nullptr, *targetSrc_ = nullptr;
    CurveEditor *curve_ = nullptr;
    SegmentedControl *profiles_ = nullptr;
    QLabel *profileDesc_ = nullptr;
    RangeSlider *range_ = nullptr;
    QLabel *rangeLabel_ = nullptr;
    QWidget *banner_ = nullptr;
};

// Small widget that paints a live sparkline for one history series.
class SparkWidget : public QWidget {
public:
    SparkWidget(const History *h, const QString &series, const QColor &color, QWidget *parent = nullptr);
    void setFixedRange(double mn, double mx) { mn_ = mn; mx_ = mx; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    const History *h_;
    QString series_;
    QColor color_;
    double mn_, mx_;
};

// Mode colors shared by the card, badge and menu-bar popup.
struct ModeStyle { QString name; QString symbol; QColor a, b; };
ModeStyle modeStyle(FanMode m);
