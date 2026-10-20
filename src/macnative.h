// macnative.h — thin Objective-C++ bridge to AppKit for things Qt can't do:
// SF Symbols icons, a real NSStatusItem (icon + live text), menu-bar-only
// mode (no Dock icon), non-activating popup panels and outside-click dismissal.
#pragma once

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <functional>

class QWidget;

namespace MacNative {

// Render an SF Symbol as a black alpha mask fitted into a px×px box (@2x).
// Returns a null image if the symbol does not exist.
QImage symbolMask(const QString &name, int px, int weight = 6 /* 1..9 */);

// Tinted SF Symbol pixmap (solid color or two-color diagonal gradient).
QPixmap symbol(const QString &name, int px, const QColor &color, int weight = 6);
QPixmap symbolGradient(const QString &name, int px, const QColor &a,
                       const QColor &b, int weight = 6);

// Show/hide the Dock icon (Regular vs Accessory activation policy).
void setDockIconVisible(bool visible);
void activateApp();

// Turn a Qt top-level (Qt::Tool) into a floating, non-activating panel that
// works over full-screen apps and does not bring other windows forward.
void configurePopupPanel(QWidget *w);

void openUrl(const QString &url);

// Finder icon / display name of an app bundle or file (cached).
QPixmap fileIcon(const QString &path, int px);
QString displayName(const QString &path);
// Ask a running app (by bundle path) to quit normally. Never targets MacFans.
bool terminateApp(const QString &bundlePath);

// macOS notifications (UNUserNotificationCenter). The first call asks for
// permission; when notifications are unavailable it falls back silently.
void requestNotificationPermission();
void notify(const QString &title, const QString &body);

// A native menu-bar status item: template SF Symbol + optional text.
class StatusItem {
public:
    StatusItem();
    ~StatusItem();
    void setSymbol(const QString &name);
    void setTitle(const QString &title);       // empty -> icon only
    void setToolTip(const QString &tip);
    void setHighlighted(bool on);
    QRect buttonRect() const;                  // Qt global coordinates
    bool ownsWindowOf(const void *nsEvent) const;
    std::function<void(QRect)> onClick;
private:
    void *d_ = nullptr;
};

// Global + local mouse-down monitor used to dismiss the popup when the user
// clicks anywhere else (other apps, the desktop, our own main window).
class OutsideClickMonitor {
public:
    ~OutsideClickMonitor();
    void start(QWidget *popup, const StatusItem *item, std::function<void()> onOutside);
    void stop();
    bool active() const { return global_ || local_; }
private:
    void *global_ = nullptr;
    void *local_ = nullptr;
};

} // namespace MacNative
