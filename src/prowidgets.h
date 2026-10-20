// prowidgets.h — custom-painted controls with smooth animations:
// SegmentedControl, ProSlider, RangeSlider, ToggleSwitch, ProButton,
// SourcePicker, LegendChip and IconChip.
#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QPropertyAnimation>
#include <QString>
#include <QVector>
#include <QWidget>
#include <functional>

class FanModel;

// ---------------------------------------------------------------- IconChip
// SF Symbol inside a softly tinted rounded square.
class IconChip : public QWidget {
public:
    IconChip(const QString &symbol, const QColor &color, int size = 28, QWidget *parent = nullptr);
    void set(const QString &symbol, const QColor &color);
    void setGradient(const QColor &a, const QColor &b);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QString symbol_;
    QColor color_, color2_;
    bool gradient_ = false;
    int size_;
};

// ---------------------------------------------------------------- Segmented
class SegmentedControl : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal pos READ pos WRITE setPos)
public:
    struct Item { QString text; QString symbol; QString tip; };
    explicit SegmentedControl(QWidget *parent = nullptr);

    void setItems(const QVector<Item> &items);
    void setCurrentIndex(int i, bool animate = true); // does not emit
    int currentIndex() const { return current_; }
    void setItemGradient(int i, const QColor &a, const QColor &b);
    void setCompact(bool compact);
    void setShowText(bool on) { showText_ = on; updateGeometry(); update(); }

    qreal pos() const { return pos_; }
    void setPos(qreal p) { pos_ = p; update(); }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QRectF segRect(int i) const;
    int indexAt(const QPointF &p) const;
    QPair<QColor, QColor> gradAt(qreal pos) const;

    QVector<Item> items_;
    QVector<QPair<QColor, QColor>> grads_;
    int current_ = 0;
    int hover_ = -1;
    qreal pos_ = 0;
    bool compact_ = false;
    bool showText_ = true;
    QPropertyAnimation *anim_;
};

// ---------------------------------------------------------------- ProSlider
class ProSlider : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal knob READ knob WRITE setKnob)
public:
    explicit ProSlider(QWidget *parent = nullptr);
    void setRange(double mn, double mx);
    void setStep(double s) { step_ = s; }
    void setValue(double v);          // programmatic, no signal
    double value() const { return value_; }
    void setGradient(const QColor &a, const QColor &b) { a_ = a; b_ = b; update(); }
    void setMarker(double v, bool show) { marker_ = v; showMarker_ = show; update(); }
    bool isDragging() const { return dragging_; }

    qreal knob() const { return knob_; }
    void setKnob(qreal k) { knob_ = k; update(); }
    QSize sizeHint() const override { return QSize(220, 30); }

signals:
    void valueChanged(double v);      // user drag
    void valueCommitted(double v);    // user release

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QRectF track() const;
    double toValue(double x) const;
    double toX(double v) const;
    void animateKnob(qreal to);
    void setFromUser(double v);

    double min_ = 0, max_ = 100, value_ = 0, step_ = 1;
    double marker_ = 0;
    bool showMarker_ = false;
    QColor a_, b_;
    bool dragging_ = false, hover_ = false;
    qreal knob_ = 1.0;
    QPropertyAnimation *knobAnim_;
};

// ---------------------------------------------------------------- RangeSlider
class RangeSlider : public QWidget {
    Q_OBJECT
public:
    explicit RangeSlider(QWidget *parent = nullptr);
    void setRange(double mn, double mx);
    void setValues(double lo, double hi);  // programmatic, no signal
    double low() const { return lo_; }
    double high() const { return hi_; }
    void setMinGap(double g) { gap_ = g; }
    void setMarker(double v, bool show) { marker_ = v; showMarker_ = show; update(); }
    void setFormatter(std::function<QString(double)> f) { fmt_ = std::move(f); update(); }
    bool isDragging() const { return active_ >= 0; }
    QSize sizeHint() const override { return QSize(260, 54); }

signals:
    void rangeChanged(double lo, double hi);
    void rangeCommitted(double lo, double hi);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    QRectF track() const;
    double toValue(double x) const;
    double toX(double v) const;

    double min_ = 30, max_ = 100, lo_ = 55, hi_ = 70, gap_ = 3;
    double marker_ = 0;
    bool showMarker_ = false;
    int active_ = -1; // 0 low, 1 high
    std::function<QString(double)> fmt_;
};

// ---------------------------------------------------------------- ToggleSwitch
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal offset READ offset WRITE setOffset)
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);
    void setCheckedSilently(bool on);
    qreal offset() const { return offset_; }
    void setOffset(qreal o) { offset_ = o; update(); }
    QSize sizeHint() const override { return QSize(44, 26); }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    qreal offset_ = 0;
    QPropertyAnimation *anim_;
};

// ---------------------------------------------------------------- ProButton
class ProButton : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal hover READ hover WRITE setHover)
public:
    enum Variant { Primary, Secondary, Ghost, Danger, IconOnly };
    ProButton(const QString &text, const QString &symbol = {}, Variant v = Secondary,
              QWidget *parent = nullptr);
    void setVariant(Variant v) { variant_ = v; updateGeometry(); update(); }
    void setSymbol(const QString &s) { symbol_ = s; update(); }
    void setGradient(const QColor &a, const QColor &b) { a_ = a; b_ = b; update(); }
    qreal hover() const { return hover_; }
    void setHover(qreal h) { hover_ = h; update(); }
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
private:
    QString symbol_;
    Variant variant_;
    QColor a_, b_;
    qreal hover_ = 0;
    QPropertyAnimation *anim_;
};

// ---------------------------------------------------------------- SourcePicker
// Button + hierarchical menu for choosing which temperature drives something.
class SourcePicker : public ProButton {
    Q_OBJECT
public:
    SourcePicker(FanModel *model, QWidget *parent = nullptr);
    void setAllowAuto(bool on) { allowAuto_ = on; }
    void setAllowHottest(bool on) { allowHottest_ = on; }
    void setSource(const QString &id);
    QString source() const { return source_; }
    void refreshText();
signals:
    void sourceChanged(const QString &id);
private:
    void openMenu();
    FanModel *model_;
    QString source_ = "__auto__";
    bool allowAuto_ = true, allowHottest_ = true;
};

// ---------------------------------------------------------------- LegendChip
class LegendChip : public QAbstractButton {
public:
    LegendChip(const QString &text, const QColor &color, QWidget *parent = nullptr);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QColor color_;
};
