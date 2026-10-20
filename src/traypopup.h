// traypopup.h — a sleek macOS menu-bar dropdown for quick fan control.
// Opens under the status-bar icon; lets you switch Auto / Smart / Custom and
// drag a slider in Custom mode. Controls the primary fan (fan 0).
#pragma once

#include <QWidget>

class FanModel;
class QLabel;
class QSlider;
class QPushButton;

class TrayPopup : public QWidget {
    Q_OBJECT
public:
    explicit TrayPopup(FanModel *model, QWidget *parent = nullptr);

    // Show the popup anchored just below the given global rect (the tray icon),
    // or below the cursor if the rect is empty.
    void popUpAt(const QRect &iconRect);

    void refresh(); // pull live readings + reflect current mode

signals:
    void openWindowRequested();
    void quitRequested();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void setMode(int mode);       // 0 Auto, 1 Smart, 2 Custom(manual)
    void applyCustomRpm(int rpm);

    FanModel *model_;
    QLabel *fanRpm_ = nullptr;
    QLabel *tempLabel_ = nullptr;
    QLabel *rpmValue_ = nullptr;
    QPushButton *btnAuto_ = nullptr, *btnSmart_ = nullptr, *btnCustom_ = nullptr;
    QWidget *sliderRow_ = nullptr;
    QSlider *slider_ = nullptr;
    bool syncing_ = false;
};
