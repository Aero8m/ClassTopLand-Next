#ifndef CLASSTOPLAND_NEXT_TASKBARTRAYMENU_H
#define CLASSTOPLAND_NEXT_TASKBARTRAYMENU_H

#include <ElaWidget.h>
#include <functional>
#include <utility>

class QAbstractButton;
class QScrollArea;
class QGridLayout;
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
    void setExitGuard(std::function<bool()> guard) {
        exitGuard_ = [guard = std::move(guard)](int) { return !guard || guard(); };
    }
    void setExitGuard(std::function<bool(int)> guard) { exitGuard_ = std::move(guard); }
    struct ScheduleChoice {
        QString id;
        QString name;
        QString detail;
    };
    void setScheduleChoices(const QList<ScheduleChoice>& choices, const QString& selectedId);
    void setScheduleStatus(const QString& status, bool courseViewsEnabled);
    void showScheduleError(const QString& message);
    QSize sizeHint() const override;

public slots:
    void showMenu();
    void hideMenu();

    // The only way this window may end the application. Restart and quit both
    // go through it, so both use the same already-proven exit mechanism, and a
    // repeated signal can never exit twice. Hiding the panel is not an exit.
    void requestExit(int exitCode);

Q_SIGNALS:
    void exitAccepted(int exitCode);
    // The user asked for one of the AppComposer-owned windows. The panel never creates
    // or owns them, so it stays a pure presentation layer.
    void settingsRequested();
    void profileEditorRequested();
    void weekScheduleRequested(const QString& id);
    void scheduleListRefreshRequested();
    void swapWindowRequested();
    void rescheduleWindowRequested();

protected:
    bool event(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void initUI();
    void scheduleDismissal();
    void resizeToAvailableScreen();
    void updateAutomaticCard();
    void requestWeekSchedule(const QString& id);
    void startTransition(const QPoint& position, qreal opacity, int duration);
    QSystemTrayIcon* trayIcon_ = nullptr;
    ElaText* applicationIcon_ = nullptr;
    QWidget* footer_ = nullptr;
    QWidget* bodyContent_ = nullptr;
    QScrollArea* bodyScroll_ = nullptr;
    QScrollArea* scheduleScroll_ = nullptr;
    QGridLayout* scheduleLayout_ = nullptr;
    QAbstractButton* automaticCard_ = nullptr;
    ElaText* scheduleEmpty_ = nullptr;
    ElaText* scheduleMore_ = nullptr;
    ElaText* scheduleError_ = nullptr;
    QList<QAbstractButton*> scheduleCards_;
    QString selectedScheduleId_;
    QString scheduleStatus_;
    bool courseViewsEnabled_ = true;
    bool pendingPanelResize_ = false;
    QParallelAnimationGroup* visibilityAnimation_ = nullptr;
    QPropertyAnimation* positionAnimation_ = nullptr;
    QPropertyAnimation* opacityAnimation_ = nullptr;
    QPoint animationOffset_{0, 12};
    bool hiding_ = false;
    bool exitRequested_ = false;
    bool checkingExit_ = false;
    std::function<bool(int)> exitGuard_;
    quint64 presentationRevision_ = 0;
};


#endif //CLASSTOPLAND_NEXT_TASKBARTRAYMENU_H
