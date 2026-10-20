#include "macnative.h"

#import <AppKit/AppKit.h>
#import <UserNotifications/UserNotifications.h>

#include <QHash>
#include <QPainter>
#include <QLinearGradient>
#include <QWidget>
#include <algorithm>

using namespace MacNative;

// ---------------------------------------------------------------- symbols

QImage MacNative::symbolMask(const QString &name, int px, int weight) {
    @autoreleasepool {
        NSImage *base = [NSImage imageWithSystemSymbolName:name.toNSString()
                                  accessibilityDescription:nil];
        if (!base || px <= 0) return QImage();

        static const NSFontWeight weights[] = {
            NSFontWeightUltraLight, NSFontWeightThin, NSFontWeightLight,
            NSFontWeightRegular, NSFontWeightMedium, NSFontWeightSemibold,
            NSFontWeightBold, NSFontWeightHeavy, NSFontWeightBlack};
        NSFontWeight fw = weights[std::clamp(weight - 1, 0, 8)];
        NSImageSymbolConfiguration *cfg =
            [NSImageSymbolConfiguration configurationWithPointSize:px * 0.80 weight:fw];
        NSImage *img = [base imageWithSymbolConfiguration:cfg] ?: base;

        const int scale = 2;
        const int W = px * scale;
        QImage out(W, W, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);

        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(
            out.bits(), W, W, 8, out.bytesPerLine(), cs,
            kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        if (!ctx) { CGColorSpaceRelease(cs); return QImage(); }

        NSSize s = img.size;
        double k = std::min({1.0, px / std::max(1.0, (double)s.width),
                             px / std::max(1.0, (double)s.height)});
        double dw = s.width * k * scale, dh = s.height * k * scale;
        NSRect dst = NSMakeRect((W - dw) / 2.0, (W - dh) / 2.0, dw, dh);

        NSGraphicsContext *gc = [NSGraphicsContext graphicsContextWithCGContext:ctx flipped:NO];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:gc];
        [img drawInRect:dst fromRect:NSZeroRect
              operation:NSCompositingOperationSourceOver fraction:1.0];
        [NSGraphicsContext restoreGraphicsState];

        CGContextRelease(ctx);
        CGColorSpaceRelease(cs);
        out.setDevicePixelRatio(scale);
        return out;
    }
}

static QHash<QString, QPixmap> &symbolCache() {
    static QHash<QString, QPixmap> c;
    return c;
}

QPixmap MacNative::symbol(const QString &name, int px, const QColor &color, int weight) {
    const QString key = QString("%1|%2|%3|%4").arg(name).arg(px).arg(color.name(QColor::HexArgb)).arg(weight);
    auto it = symbolCache().constFind(key);
    if (it != symbolCache().constEnd()) return it.value();

    QImage m = symbolMask(name, px, weight);
    if (m.isNull()) return QPixmap();
    QPainter p(&m);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(QRectF(0, 0, px, px), color);
    p.end();
    QPixmap pm = QPixmap::fromImage(m);
    symbolCache().insert(key, pm);
    return pm;
}

QPixmap MacNative::symbolGradient(const QString &name, int px, const QColor &a,
                                  const QColor &b, int weight) {
    const QString key = QString("G%1|%2|%3|%4|%5").arg(name).arg(px)
                            .arg(a.name(QColor::HexArgb), b.name(QColor::HexArgb)).arg(weight);
    auto it = symbolCache().constFind(key);
    if (it != symbolCache().constEnd()) return it.value();

    QImage m = symbolMask(name, px, weight);
    if (m.isNull()) return QPixmap();
    QPainter p(&m);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    QLinearGradient g(0, 0, px, px);
    g.setColorAt(0, a);
    g.setColorAt(1, b);
    p.fillRect(QRectF(0, 0, px, px), g);
    p.end();
    QPixmap pm = QPixmap::fromImage(m);
    symbolCache().insert(key, pm);
    return pm;
}

// ---------------------------------------------------------------- app

void MacNative::activateApp() {
    if (@available(macOS 14.0, *)) [NSApp activate];
    else [NSApp activateIgnoringOtherApps:YES];
}

void MacNative::setDockIconVisible(bool visible) {
    [NSApp setActivationPolicy:visible ? NSApplicationActivationPolicyRegular
                                       : NSApplicationActivationPolicyAccessory];
    if (visible) {
        activateApp();
        // The menu bar sometimes needs a second nudge after a policy switch.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(80 * NSEC_PER_MSEC)),
                       dispatch_get_main_queue(), ^{ MacNative::activateApp(); });
    }
}

void MacNative::configurePopupPanel(QWidget *w) {
    if (!w) return;
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(w->winId());
    NSWindow *win = view.window;
    if (!win) return;
    if ([win isKindOfClass:[NSPanel class]]) {
        NSPanel *p = (NSPanel *)win;
        p.styleMask = p.styleMask | NSWindowStyleMaskNonactivatingPanel;
        p.becomesKeyOnlyIfNeeded = YES;
        p.floatingPanel = YES;
        p.worksWhenModal = YES;
    }
    win.hidesOnDeactivate = NO;
    win.level = NSPopUpMenuWindowLevel;
    win.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                             NSWindowCollectionBehaviorFullScreenAuxiliary |
                             NSWindowCollectionBehaviorIgnoresCycle |
                             NSWindowCollectionBehaviorStationary;
    win.hasShadow = NO;
    win.opaque = NO;
    win.backgroundColor = NSColor.clearColor;
}

void MacNative::openUrl(const QString &url) {
    NSURL *u = [NSURL URLWithString:url.toNSString()];
    if (u) [[NSWorkspace sharedWorkspace] openURL:u];
}

// ---------------------------------------------------------------- file icons

static QImage renderImage(NSImage *img, int px) {
    const int scale = 2, W = px * scale;
    QImage out(W, W, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(out.bits(), W, W, 8, out.bytesPerLine(), cs,
                                             kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    if (ctx) {
        NSGraphicsContext *gc = [NSGraphicsContext graphicsContextWithCGContext:ctx flipped:NO];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:gc];
        [img drawInRect:NSMakeRect(0, 0, W, W) fromRect:NSZeroRect
              operation:NSCompositingOperationSourceOver fraction:1.0];
        [NSGraphicsContext restoreGraphicsState];
        CGContextRelease(ctx);
    }
    CGColorSpaceRelease(cs);
    out.setDevicePixelRatio(scale);
    return out;
}

QPixmap MacNative::fileIcon(const QString &path, int px) {
    static QHash<QString, QPixmap> cache;
    const QString key = path + "|" + QString::number(px);
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();
    QPixmap pm;
    @autoreleasepool {
        NSImage *img = [[NSWorkspace sharedWorkspace] iconForFile:path.toNSString()];
        if (img) pm = QPixmap::fromImage(renderImage(img, px));
    }
    cache.insert(key, pm);
    return pm;
}

QString MacNative::displayName(const QString &path) {
    @autoreleasepool {
        NSString *n = [[NSFileManager defaultManager] displayNameAtPath:path.toNSString()];
        return n ? QString::fromNSString(n) : path;
    }
}

bool MacNative::terminateApp(const QString &bundlePath) {
    @autoreleasepool {
        NSBundle *b = [NSBundle bundleWithPath:bundlePath.toNSString()];
        NSString *bid = b.bundleIdentifier;
        if (!bid || [bid isEqualToString:NSBundle.mainBundle.bundleIdentifier]) return false;
        bool any = false;
        for (NSRunningApplication *a in [NSRunningApplication runningApplicationsWithBundleIdentifier:bid])
            any |= [a terminate];
        return any;
    }
}

// ---------------------------------------------------------------- notifications

@interface MFNotifyDelegate : NSObject <UNUserNotificationCenterDelegate>
@end
@implementation MFNotifyDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))handler {
    (void)center; (void)notification;
    // Show banners even while MacFans is the active app.
    handler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList |
            UNNotificationPresentationOptionSound);
}
@end

static UNUserNotificationCenter *notifyCenter() {
    if (!NSBundle.mainBundle.bundleIdentifier) return nil; // not running from a bundle
    static MFNotifyDelegate *delegate = nil;
    UNUserNotificationCenter *c = [UNUserNotificationCenter currentNotificationCenter];
    if (!delegate) {
        delegate = [MFNotifyDelegate new];
        c.delegate = delegate;
    }
    return c;
}

void MacNative::requestNotificationPermission() {
    UNUserNotificationCenter *c = notifyCenter();
    if (!c) return;
    [c requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                     completionHandler:^(BOOL, NSError *) {}];
}

void MacNative::notify(const QString &title, const QString &body) {
    UNUserNotificationCenter *c = notifyCenter();
    if (!c) return;
    UNMutableNotificationContent *content = [UNMutableNotificationContent new];
    content.title = title.toNSString();
    content.body = body.toNSString();
    content.sound = UNNotificationSound.defaultSound;
    UNNotificationRequest *req = [UNNotificationRequest requestWithIdentifier:NSUUID.UUID.UUIDString
                                                                      content:content
                                                                      trigger:nil];
    NSString *t = title.toNSString(), *b = body.toNSString();
    [c requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                     completionHandler:^(BOOL granted, NSError *error) {
        if (granted) {
            [c addNotificationRequest:req withCompletionHandler:nil];
        } else if (error) {
            // The notification system refused this (ad-hoc signed) app outright —
            // not a user "Don't Allow". Fall back to an AppleScript notification.
            NSString *(^esc)(NSString *) = ^NSString *(NSString *s) {
                return [s stringByReplacingOccurrencesOfString:@"\"" withString:@"'"];
            };
            NSString *script = [NSString stringWithFormat:@"display notification \"%@\" with title \"%@\"",
                                                          esc(b), esc(t)];
            NSTask *task = [NSTask new];
            task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/osascript"];
            task.arguments = @[@"-e", script];
            [task launchAndReturnError:nil];
        }
    }];
}

// ---------------------------------------------------------------- status item

@interface MFStatusTarget : NSObject
@property (nonatomic, assign) MacNative::StatusItem *owner;
- (void)clicked:(id)sender;
@end

@implementation MFStatusTarget
- (void)clicked:(id)sender {
    (void)sender;
    MacNative::StatusItem *o = self.owner;
    if (o && o->onClick) o->onClick(o->buttonRect());
}
@end

struct StatusItemPrivate {
    NSStatusItem *item = nil;
    MFStatusTarget *target = nil;
};

static StatusItemPrivate *P(void *d) { return static_cast<StatusItemPrivate *>(d); }

StatusItem::StatusItem() {
    auto *p = new StatusItemPrivate;
    p->item = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    p->target = [MFStatusTarget new];
    p->target.owner = this;
    NSStatusBarButton *b = p->item.button;
    b.target = p->target;
    b.action = @selector(clicked:);
    [b sendActionOn:NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown];
    b.imagePosition = NSImageOnly;
    b.imageHugsTitle = YES;
    b.font = [NSFont monospacedDigitSystemFontOfSize:13 weight:NSFontWeightMedium];
    d_ = p;
}

StatusItem::~StatusItem() {
    auto *p = P(d_);
    if (!p) return;
    p->target.owner = nullptr;
    [[NSStatusBar systemStatusBar] removeStatusItem:p->item];
    delete p;
}

void StatusItem::setSymbol(const QString &name) {
    NSImage *img = [NSImage imageWithSystemSymbolName:name.toNSString()
                                accessibilityDescription:@"MacFans"];
    if (!img) return;
    NSImageSymbolConfiguration *cfg =
        [NSImageSymbolConfiguration configurationWithPointSize:14 weight:NSFontWeightMedium];
    img = [img imageWithSymbolConfiguration:cfg] ?: img;
    [img setTemplate:YES]; // ("template" is a C++ keyword in ObjC++)
    P(d_)->item.button.image = img;
}

void StatusItem::setTitle(const QString &title) {
    NSStatusBarButton *b = P(d_)->item.button;
    b.title = title.toNSString();
    b.imagePosition = title.isEmpty() ? NSImageOnly : NSImageLeading;
}

void StatusItem::setToolTip(const QString &tip) {
    P(d_)->item.button.toolTip = tip.toNSString();
}

void StatusItem::setHighlighted(bool on) {
    [P(d_)->item.button highlight:on];
}

QRect StatusItem::buttonRect() const {
    NSStatusBarButton *b = P(d_)->item.button;
    NSWindow *w = b.window;
    if (!w) return QRect();
    NSRect inWin = [b convertRect:b.bounds toView:nil];
    NSRect scr = [w convertRectToScreen:inWin];
    CGFloat primaryH = NSScreen.screens.firstObject.frame.size.height;
    return QRect(qRound(scr.origin.x),
                 qRound(primaryH - scr.origin.y - scr.size.height),
                 qRound(scr.size.width), qRound(scr.size.height));
}

bool StatusItem::ownsWindowOf(const void *nsEvent) const {
    NSEvent *e = (__bridge NSEvent *)nsEvent;
    return e.window && e.window == P(d_)->item.button.window;
}

// ---------------------------------------------------------------- click monitor

OutsideClickMonitor::~OutsideClickMonitor() { stop(); }

void OutsideClickMonitor::start(QWidget *popup, const StatusItem *item,
                                std::function<void()> onOutside) {
    stop();
    NSView *pv = (__bridge NSView *)reinterpret_cast<void *>(popup->winId());
    NSWindow *pw = pv.window;
    const NSEventMask mask = NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown;

    std::function<void()> cb = onOutside;
    id g = [NSEvent addGlobalMonitorForEventsMatchingMask:mask handler:^(NSEvent *e) {
        (void)e;
        dispatch_async(dispatch_get_main_queue(), ^{ cb(); });
    }];
    id l = [NSEvent addLocalMonitorForEventsMatchingMask:mask handler:^NSEvent *(NSEvent *e) {
        bool inPopup = (e.window == pw);
        bool onItem = item && item->ownsWindowOf((__bridge const void *)e);
        if (!inPopup && !onItem)
            dispatch_async(dispatch_get_main_queue(), ^{ cb(); });
        return e;
    }];
    global_ = (__bridge_retained void *)g;
    local_ = (__bridge_retained void *)l;
}

void OutsideClickMonitor::stop() {
    if (global_) { [NSEvent removeMonitor:(__bridge_transfer id)global_]; global_ = nullptr; }
    if (local_)  { [NSEvent removeMonitor:(__bridge_transfer id)local_];  local_ = nullptr; }
}
