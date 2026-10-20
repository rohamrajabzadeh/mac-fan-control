// theme.h — shared colors, fonts and small formatting helpers.
#pragma once

#include <QColor>
#include <QFont>
#include <QString>
#include <QLocale>
#include <algorithm>
#include <cmath>

namespace Theme {

// Surfaces
inline QColor bg()          { return QColor(0x12, 0x14, 0x19); }
inline QColor card()        { return QColor(0x1a, 0x1d, 0x24); }
inline QColor cardHi()      { return QColor(0x1f, 0x23, 0x2b); }
inline QColor border()      { return QColor(0x29, 0x2e, 0x38); }
inline QColor elevated()    { return QColor(0x23, 0x27, 0x30); }
inline QColor elevatedHi()  { return QColor(0x2b, 0x30, 0x3b); }
inline QColor track()       { return QColor(255, 255, 255, 22); }

// Text
inline QColor text()        { return QColor(0xee, 0xf1, 0xf6); }
inline QColor textDim()     { return QColor(0xa3, 0xab, 0xba); }
inline QColor textMuted()   { return QColor(0x6e, 0x76, 0x86); }

// Accents
inline QColor blue()        { return QColor(0x4a, 0xa8, 0xff); }
inline QColor blueLight()   { return QColor(0x7c, 0xd4, 0xff); }
inline QColor violet()      { return QColor(0x8b, 0x7b, 0xff); }
inline QColor violetLight() { return QColor(0xb8, 0x9c, 0xff); }
inline QColor green()       { return QColor(0x3d, 0xd6, 0x8c); }
inline QColor amber()       { return QColor(0xf5, 0xb8, 0x4a); }
inline QColor orange()      { return QColor(0xff, 0x8a, 0x4c); }
inline QColor red()         { return QColor(0xff, 0x5a, 0x5f); }
inline QColor cyan()        { return QColor(0x4f, 0xd8, 0xe8); }
inline QColor pink()        { return QColor(0xff, 0x7a, 0xb6); }

inline QColor mix(const QColor &a, const QColor &b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

// Smooth temperature color: green → amber → orange → red.
inline QColor tempColor(double c) {
    if (c <= 45) return green();
    if (c <= 62) return mix(green(), amber(), (c - 45) / 17.0);
    if (c <= 78) return mix(amber(), orange(), (c - 62) / 16.0);
    if (c <= 92) return mix(orange(), red(), (c - 78) / 14.0);
    return red();
}

// Load color for CPU/memory percentages.
inline QColor loadColor(double pct, const QColor &base) {
    if (pct < 70) return base;
    if (pct < 90) return mix(base, orange(), (pct - 70) / 20.0);
    return mix(orange(), red(), std::min(1.0, (pct - 90) / 10.0));
}

// Category color used for sensor groups and chart series.
inline QColor categoryColor(const QString &icon) {
    if (icon == "cpu")     return blue();
    if (icon == "gpu")     return green();
    if (icon == "soc")     return violet();
    if (icon == "power")   return amber();
    if (icon == "system")  return pink();
    if (icon == "storage") return cyan();
    return blue();
}

inline QFont font(double pt, QFont::Weight w = QFont::Normal, bool tabular = false) {
    QFont f;
    f.setPointSizeF(pt);
    f.setWeight(w);
    if (tabular) f.setFeature("tnum", 1);
    return f;
}

// Restyle a label's text color only when it actually changes.
template <typename L>
inline void setLabelColor(L *label, const QColor &c) {
    const QString css = QStringLiteral("color:%1;").arg(c.name());
    if (label->styleSheet() != css) label->setStyleSheet(css);
}

inline QString thousands(double v) {
    return QLocale(QLocale::English).toString(qRound64(v));
}

inline QString rpm(double v) { return thousands(v) + " RPM"; }

// Compact RPM for the menu bar: 2140 -> "2.1k".
inline QString rpmCompact(double v) {
    if (v >= 1000) return QString::number(v / 1000.0, 'f', 1) + "k";
    return QString::number(qRound(v));
}

} // namespace Theme
