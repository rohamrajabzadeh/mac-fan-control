// mainwindow.h — main window: header with tabs (Dashboard / History /
// Settings), page stack, permission handling. Closing the window keeps the
// app alive in the menu bar.
#pragma once

#include <QMainWindow>

class FanModel;
class SegmentedControl;
class QStackedWidget;
class DashboardPage;
class HistoryPage;
class SettingsPage;
class ProButton;
class TrayController;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(FanModel *model, QWidget *parent = nullptr);
    void showFromTray(int tab = -1);
    void devCapture(const QString &dir);   // dev-only: screenshots of every page

protected:
    void closeEvent(QCloseEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private slots:
    void refresh();
    void enableControl();

private:
    QWidget *buildHeader();
    void switchTab(int i, bool animate = true);
    void updateStatusPill();
    void applySettings();

    FanModel *m_;
    bool quitting_ = false;
    SegmentedControl *tabs_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    DashboardPage *dash_ = nullptr;
    HistoryPage *hist_ = nullptr;
    SettingsPage *settings_ = nullptr;
    ProButton *pill_ = nullptr;
    TrayController *tray_ = nullptr;
};
