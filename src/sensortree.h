// sensortree.h — collapsible temperature tree (categories → groups → sensors)
// with animated expand/collapse, per-row sparklines and a context menu.
#pragma once

#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>

class FanModel;
class QVariantAnimation;

class SensorTreeView : public QWidget {
    Q_OBJECT
public:
    explicit SensorTreeView(FanModel *model, QWidget *parent = nullptr);
    void rebuild();

signals:
    void showOnGauge(const QString &id);
    void showInMenuBar(const QString &id);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    struct Row { int node; qreal y; qreal h; qreal f; };
    struct Block { qreal top; qreal bottom; };
    void layoutRows();
    void addRows(int node, qreal factor, qreal &y);
    void toggle(int node);
    int rowAt(const QPointF &p) const;
    bool expanded(int node) const;

    FanModel *m_;
    QSet<QString> expanded_;
    QVector<Row> rows_;
    QVector<Block> blocks_;
    int hover_ = -1;
    QString animNode_;
    bool animExpanding_ = true;
    qreal animT_ = 1.0;
    QVariantAnimation *anim_;
};

// A compact metric card: icon, title, big value, subtitle and either a
// sparkline (history series) or a progress bar.
class History;
class StatTile : public QWidget {
public:
    StatTile(const History *h, QWidget *parent = nullptr);
    void setup(const QString &title, const QString &symbol, const QColor &a, const QColor &b,
               const QString &seriesId = {});
    void setValues(const QString &big, const QString &sub, double pct);
    QSize sizeHint() const override { return QSize(200, 124); }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    const History *h_;
    QString title_, symbol_, series_, big_, sub_;
    QColor a_, b_;
    double pct_ = 0;
};
