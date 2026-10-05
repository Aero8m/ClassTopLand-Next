#ifndef CLASSTOPLAND_NEXT_COURSEBAR_H
#define CLASSTOPLAND_NEXT_COURSEBAR_H
#include <QHBoxLayout>
#include<QWidget>

#include <QPushButton>
#include <QPointer>
#include <QQueue>
#include <QStringList>
#include <optional>
#include"../Core/ConfigManager/ConfigManager.h"
#include"../Core/ProfileManager/ProfileManager.h"
#include"../Core/Model/Config.h"
#include"../Core/Model/Profile.h"
#include"../Utils/GlassHelper/GlassHelper.h"
#include"ICourseBarComponent.h"
#include"BuiltinComponents/Date/Date.h"
#include "../Core/Logger/Logger.h"
#include "../Core/CourseRefreshService/CourseRefreshService.h"

class QLabel;
class QPropertyAnimation;
class QTimer;

class CourseBar : public QWidget
{
    Q_OBJECT
public:
    CourseBar(QWidget *parent = nullptr);
    ~CourseBar();
    bool hasCourseViews() const;
    QString scheduleStatus() const;

signals:
    void scheduleStatusChanged();

public slots:
    void reloadProfile();
    void showNotification(const QString& message);
    // The first alternative that fits is used, as in the original course bar.
    void showNotification(const QStringList& messages);

private:
    struct Notification {
        QStringList messages;
        QPointer<CourseRefreshService> source;
        quint64 tableRevision = 0;
        QString eventKey;
    };
    enum class NotificationPhase { Hidden, Entering, Holding, Leaving };

    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void initUI();
    void initComponents();
    void initNotifications();
    void showCourseNotification(const CourseRefreshService::Event& event);
    void enqueueNotification(const Notification& notification);
    void showNextNotification();
    void dismissNotification();
    void finishNotification();
    void invalidateNotifications(CourseRefreshService* source);
    void updateNotificationGeometry();
    int notificationMinimumWidth() const;
    QRect notificationHiddenRect() const;
    void selectNotificationText();
    void scheduleGeometryUpdate();
    void updateBarGeometry();
    void centerOnScreen();
    void toggleCollapsed();
    void applyPresentation();
    int barWidth(bool collapsed) const;
    QPoint barPosition(bool collapsed, int width) const;
    bool geometryUpdatePending_ = false;
    bool collapsed_ = false;
    bool transitioning_ = false;
    int transitionWidth_ = 0;
    QPropertyAnimation* hideAnimation_ = nullptr;
    QTimer* midpointTimer_ = nullptr;
    QWidget* notificationLayer_ = nullptr;
    QLabel* notificationIcon_ = nullptr;
    QLabel* notificationText_ = nullptr;
    QHBoxLayout* notificationLayout_ = nullptr;
    QPropertyAnimation* notificationAnimation_ = nullptr;
    QTimer* notificationTimer_ = nullptr;
    NotificationPhase notificationPhase_ = NotificationPhase::Hidden;
    QQueue<Notification> pendingNotifications_;
    std::optional<Notification> currentNotification_;
    CourseBarConfig config;
    Profile profile;
    QHBoxLayout* mainLayout;
    QPushButton* hideButton;
    QList<ICourseBarComponent*> components;
};


#endif //CLASSTOPLAND_NEXT_COURSEBAR_H
