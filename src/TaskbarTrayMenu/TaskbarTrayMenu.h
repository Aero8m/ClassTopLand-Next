#ifndef CLASSTOPLAND_NEXT_TASKBARTRAYMENU_H
#define CLASSTOPLAND_NEXT_TASKBARTRAYMENU_H

#include <ElaWidget.h>

class QCloseEvent;
class QHideEvent;
class QParallelAnimationGroup;
class QPropertyAnimation;
class QResizeEvent;
class QSystemTrayIcon;
class ElaText;

class TaskbarTrayMenu : public ElaWidget
{
    Q_OBJECT
public:
    // Handled by main after AppComposer has saved data and destroyed its windows.
    static constexpr int RestartExitCode = 1000;

    explicit TaskbarTrayMenu(QWidget* parent = nullptr);
    ~TaskbarTrayMenu() override;

public slots:
    void showMenu();
    void hideMenu();

    // The only way this window may end the application. Restart and quit both
    // go through it, so both use the same already-proven exit mechanism, and a
    // repeated signal can never exit twice. Hiding the panel is not an exit.
    void requestExit(int exitCode);

protected:
    bool event(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void initUI();
    void startTransition(const QPoint& position, qreal opacity, int duration);
    QSystemTrayIcon* trayIcon_ = nullptr;
    ElaText* applicationIcon_ = nullptr;
    QWidget* footer_ = nullptr;
    QParallelAnimationGroup* visibilityAnimation_ = nullptr;
    QPropertyAnimation* positionAnimation_ = nullptr;
    QPropertyAnimation* opacityAnimation_ = nullptr;
    QPoint animationOffset_{0, 12};
    bool hiding_ = false;
    bool exitRequested_ = false;
    quint64 presentationRevision_ = 0;
};


#endif //CLASSTOPLAND_NEXT_TASKBARTRAYMENU_H
