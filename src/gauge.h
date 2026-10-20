// gauge.h — A minimal circular gauge drawn with QPainter, used for both
// temperatures and fan RPM. Animated needle/arc, theme-friendly colors.
#pragma once

#include <QWidget>
#include <QColor>
#include <QPropertyAnimation>

class Gauge : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double displayValue READ displayValue WRITE setDisplayValue)
public:
    explicit Gauge(QWidget *parent = nullptr);

    void setRange(double min, double max);
    void setValue(double v);               // animates toward v
    void setUnit(const QString &u) { unit_ = u; update(); }
    void setCaption(const QString &c) { caption_ = c; update(); }
    void setAccent(const QColor &c) { accent_ = c; update(); }

    double displayValue() const { return display_; }
    void setDisplayValue(double v) { display_ = v; update(); }

protected:
    void paintEvent(QPaintEvent *) override;
    QSize sizeHint() const override { return QSize(150, 150); }

private:
    double min_ = 0, max_ = 100, target_ = 0, display_ = 0;
    QString unit_, caption_;
    QColor accent_ = QColor("#4aa8ff");
    QPropertyAnimation anim_;
};
