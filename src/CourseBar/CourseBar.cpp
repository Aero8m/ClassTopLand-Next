#include "CourseBar.h"

#include "BuiltinComponents/CourseView/CourseView.h"
#include "../Core/ThemeManager/ThemeManager.h"

#include <QEvent>
#include <QGuiApplication>
#include <QFontMetrics>
#include <QLabel>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScreen>
#include <QTimer>
#include <utility>

CourseBar::CourseBar(QWidget* parent) : QWidget(parent)
{
    Logger::instance().log(Logger::Level::Info,"Initializing CourseBar...");
    Logger::instance().flush();
    // set config variable and profile variable
    config = ConfigManager::instance().config().courseBarConfig;
    profile = ProfileManager::instance().profile();

    initUI();
    initNotifications();
    connect(&ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &CourseBar::refreshThemeColor);
    if (config.enable) initComponents();
    auto watchScreen = [this](QScreen* screen) {
        if (screen) {
            connect(screen, &QScreen::availableGeometryChanged,
                    this, &CourseBar::scheduleGeometryUpdate, Qt::UniqueConnection);
            connect(screen, &QScreen::geometryChanged,
                    this, &CourseBar::scheduleGeometryUpdate, Qt::UniqueConnection);
        }
        scheduleGeometryUpdate();
    };
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, watchScreen);
    watchScreen(QGuiApplication::primaryScreen());
    updateBarGeometry();
}


CourseBar::~CourseBar()
{

}

void CourseBar::initUI()
{
    // init window properties
    setWindowFlags(Qt::WindowType::FramelessWindowHint | Qt::WindowType::WindowStaysOnTopHint | Qt::WindowType::Tool | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WidgetAttribute::WA_TranslucentBackground,true);
    setAttribute(Qt::WidgetAttribute::WA_ShowWithoutActivating,true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    setFixedHeight(config.height);
    GlassHelper::enableBlurBehind(this);

    // init window layout
    mainLayout = new QHBoxLayout(this);
    mainLayout->setSizeConstraint(QLayout::SetNoConstraint);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(0, 0, 20, 0);
    setLayout(mainLayout);

    // init hide button
    hideButton = new QPushButton(this);
    hideButton->setObjectName(QStringLiteral("hideButton"));
    hideButton->setStyleSheet("background: rgba(200,200,200,0.5); border:none;");
    hideButton->setFixedSize(19,config.height);
    mainLayout->addWidget(hideButton);

    hideAnimation_ = new QPropertyAnimation(this, "pos", this);
    hideAnimation_->setDuration(700);
    hideAnimation_->setEasingCurve(QEasingCurve::InOutExpo);
    midpointTimer_ = new QTimer(this);
    midpointTimer_->setSingleShot(true);
    midpointTimer_->setTimerType(Qt::PreciseTimer);
    midpointTimer_->setInterval(hideAnimation_->duration() / 2);
    connect(hideButton, &QPushButton::clicked, this, &CourseBar::toggleCollapsed);
    connect(midpointTimer_, &QTimer::timeout, this, [this] {
        applyPresentation();
        resize(transitionWidth_, config.height);
        mainLayout->activate();
    });
    connect(hideAnimation_, &QPropertyAnimation::finished, this, [this] {
        midpointTimer_->stop();
        transitioning_ = false;
        geometryUpdatePending_ = false;
        updateBarGeometry();
        showNextNotification();
    });
}

void CourseBar::initComponents()
{
    Logger::instance().log(Logger::Level::Info,"Initializing Components...");
    Logger::instance().flush();
    for (auto component : config.components)
    {
        bool addedComponent = false;
        if (!component.enabled) continue;
        if (component.type == CourseBarComponentType::Date)
        {
            addedComponent = true;
            ICourseBarComponent* newComponent = new Date(this);
            mainLayout->addWidget(newComponent);
            components.append(newComponent);
        }
        else if (component.type == CourseBarComponentType::CourseView)
        {
            auto* newComponent = new CourseView(this);
            connect(newComponent, &CourseView::preferredSizeChanged,
                    this, &CourseBar::scheduleGeometryUpdate);
            auto* service = newComponent->refreshService();
            connect(service, &CourseRefreshService::eventOccurred,
                    this, &CourseBar::showCourseNotification);
            connect(service, &CourseRefreshService::tableChanged, this, [this, service] {
                invalidateNotifications(service);
                emit scheduleStatusChanged();
            });
            mainLayout->addWidget(newComponent);
            components.append(newComponent);
            addedComponent = true;
        }
        else if (component.type == CourseBarComponentType::TextTip)
        {
            auto* newComponent = new TextTip(this);
            newComponent->setText(component.text);
            mainLayout->addWidget(newComponent);
            components.append(newComponent);
            addedComponent = true;
        }
        if (addedComponent)
        {
            Logger::instance().log(Logger::Level::Info,QString("Added a course bar component, name: %1").arg(components.last()->getName()));
            Logger::instance().flush();
        }
    }
    if (components.isEmpty())
    {
        auto* newComponent = new TextTip(this);
        newComponent->setText("当前尚未添加组件");
        mainLayout->addWidget(newComponent);
        components.append(newComponent);
    }
}


void CourseBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    centerOnScreen();
    updateNotificationGeometry();
}

bool CourseBar::hasCourseViews() const
{
    if (!config.enable) return false;
    for (const auto* component : components)
        if (qobject_cast<const CourseView*>(component)) return true;
    return false;
}

QString CourseBar::scheduleStatus() const
{
    if (!config.enable) return tr("课程条已关闭");
    for (auto* component : components) {
        auto* view = qobject_cast<CourseView*>(component);
        if (!view) continue;
        const auto& table = view->refreshService()->table();
        if (!table.valid) {
            if (table.error == QStringLiteral("Multiple week schedules match; select an explicit index"))
                return tr("多张课表匹配，请手动选择");
            if (table.error == QStringLiteral("Odd/even schedules require a week reference date"))
                return tr("单双周缺少第一周日期，请手动选择");
            return tr("课表匹配失败：%1").arg(table.error);
        }
        if (table.name.isEmpty()) return tr("今日无匹配课表");
        if (table.classes.isEmpty()) return tr("%1 · 今日无课程").arg(table.name);
        return tr("%1 · 今日 %2 节").arg(table.name).arg(table.classes.size());
    }
    return tr("课程栏未启用");
}

void CourseBar::reloadConfig()
{
    hideAnimation_->stop();
    midpointTimer_->stop();
    transitioning_ = false;
    geometryUpdatePending_ = false;
    notificationAnimation_->stop();
    notificationTimer_->stop();
    notificationLayer_->hide();
    notificationPhase_ = NotificationPhase::Hidden;
    currentNotification_.reset();
    pendingNotifications_.clear();

    for (auto* component : std::as_const(components)) {
        mainLayout->removeWidget(component);
        delete component;
    }
    components.clear();
    config = ConfigManager::instance().config().courseBarConfig;
    setFixedHeight(config.height);
    hideButton->setFixedSize(19, config.height);
    const int iconSize = qMax(1, qRound(config.height * 35.0 / 49.0));
    notificationIcon_->setFixedSize(iconSize, iconSize);
    const QPixmap bell(QStringLiteral(":/res/images/ring.png"));
    notificationIcon_->setPixmap(bell.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    QFont font = notificationText_->font();
    font.setPixelSize(qMax(1, qRound(config.height * 24.0 / 49.0)));
    notificationText_->setFont(font);
    // A disabled bar has no running component services or queued notifications.
    if (config.enable) initComponents();
    updateBarGeometry();
    setVisible(config.enable);
    emit scheduleStatusChanged();
}

void CourseBar::reloadProfile()
{
    profile = ProfileManager::instance().profile();
    for (auto* component : components) {
        if (auto* view = qobject_cast<CourseView*>(component)) view->refreshService()->reloadTable();
    }
    scheduleGeometryUpdate();
    emit scheduleStatusChanged();
}

bool CourseBar::event(QEvent* event)
{
    const bool result = QWidget::event(event);
    if (event->type() == QEvent::LayoutRequest) scheduleGeometryUpdate();
    return result;
}

void CourseBar::scheduleGeometryUpdate()
{
    if (geometryUpdatePending_) return;
    geometryUpdatePending_ = true;
    if (transitioning_) return;
    QTimer::singleShot(0, this, [this] {
        geometryUpdatePending_ = false;
        updateBarGeometry();
    });
}

void CourseBar::updateBarGeometry()
{
    if (transitioning_) {
        geometryUpdatePending_ = true;
        return;
    }
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    applyPresentation();
    // Let the content extend beyond the screen; do not clamp or scroll courses.
    resize(barWidth(collapsed_), config.height);
    mainLayout->activate();
    centerOnScreen();
}

void CourseBar::centerOnScreen()
{
    if (transitioning_ || !QGuiApplication::primaryScreen()) return;
    move(barPosition(collapsed_, width()));
}

QPoint CourseBar::barPosition(bool collapsed, int width) const
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return pos();
    const QRect available = screen->availableGeometry();
    return QPoint(available.x() + (collapsed ? available.width() - width
        : (available.width() - width) / 2), available.y());
}

int CourseBar::barWidth(bool collapsed) const
{
    int width = hideButton->width();
    int count = 0;
    for (ICourseBarComponent* component : components) {
        auto* courseView = qobject_cast<CourseView*>(component);
        if (collapsed && (!courseView || !courseView->hasCompactContent())) continue;
        width += courseView ? (collapsed ? courseView->compactSizeHint().width()
                                       : courseView->expandedSizeHint().width())
                            : component->sizeHint().width();
        ++count;
    }
    // A lone restore button must not leave an empty right margin.
    if (!collapsed || count > 0) width += 20;
    width += count * mainLayout->spacing();
    if (!collapsed && currentNotification_) width = qMax(width, notificationMinimumWidth());
    return qMax(1, width);
}

void CourseBar::applyPresentation()
{
    bool hasContent = false;
    for (ICourseBarComponent* component : components) {
        auto* courseView = qobject_cast<CourseView*>(component);
        if (courseView) courseView->setCompactMode(collapsed_);
        const bool visible = !collapsed_ || (courseView && courseView->hasCompactContent());
        component->setVisible(visible);
        hasContent |= visible;
    }
    mainLayout->setContentsMargins(0, 0, collapsed_ && !hasContent ? 0 : 20, 0);
    hideButton->raise();
    if (notificationLayer_ && notificationPhase_ != NotificationPhase::Hidden)
        notificationLayer_->raise();
}

void CourseBar::toggleCollapsed()
{
    if (transitioning_ || currentNotification_ || !QGuiApplication::primaryScreen()) return;
    updateBarGeometry();
    collapsed_ = !collapsed_;
    transitionWidth_ = barWidth(collapsed_);
    transitioning_ = true;
    hideAnimation_->setStartValue(pos());
    hideAnimation_->setEndValue(barPosition(collapsed_, transitionWidth_));
    hideAnimation_->start();
    midpointTimer_->start();
}

void CourseBar::refreshThemeColor()
{
    notificationLayer_->setStyleSheet(QStringLiteral("QWidget#notificationLayer { background: %1; }")
        .arg(ThemeManager::instance().themeColor().name(QColor::HexRgb)));
}

void CourseBar::initNotifications()
{
    // This is a sibling of all components, deliberately outside mainLayout.
    notificationLayer_ = new QWidget(this);
    notificationLayer_->setObjectName(QStringLiteral("notificationLayer"));
    refreshThemeColor();
    notificationLayer_->setFocusPolicy(Qt::NoFocus);
    notificationLayout_ = new QHBoxLayout(notificationLayer_);
    notificationLayout_->setSizeConstraint(QLayout::SetNoConstraint);
    notificationLayout_->setContentsMargins(12, 0, 12, 0);
    notificationLayout_->setSpacing(12);
    notificationIcon_ = new QLabel(notificationLayer_);
    notificationIcon_->setObjectName(QStringLiteral("notificationIcon"));
    notificationIcon_->setAlignment(Qt::AlignCenter);
    const QPixmap bell(QStringLiteral(":/res/images/ring.png"));
    if (bell.isNull()) {
        Logger::instance().log(Logger::Level::Error, "CourseBar notification bell resource could not be loaded");
        Logger::instance().flush();
    }
    const int iconSize = qMax(1, qRound(height() * 35.0 / 49.0));
    notificationIcon_->setFixedSize(iconSize, iconSize);
    notificationIcon_->setPixmap(bell.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    notificationText_ = new QLabel(notificationLayer_);
    notificationText_->setObjectName(QStringLiteral("notificationText"));
    notificationText_->setAlignment(Qt::AlignCenter);
    notificationText_->setTextFormat(Qt::PlainText);
    notificationText_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    notificationText_->setStyleSheet(QStringLiteral("color: white; background: transparent;"));
    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPixelSize(qMax(1, qRound(height() * 24.0 / 49.0)));
    notificationText_->setFont(font);
    notificationLayout_->addWidget(notificationIcon_);
    notificationLayout_->addWidget(notificationText_, 1);
    notificationLayer_->hide();

    notificationAnimation_ = new QPropertyAnimation(notificationLayer_, "geometry", this);
    notificationAnimation_->setObjectName(QStringLiteral("notificationAnimation"));
    notificationAnimation_->setDuration(500);
    notificationAnimation_->setEasingCurve(QEasingCurve::OutExpo);
    notificationTimer_ = new QTimer(this);
    notificationTimer_->setObjectName(QStringLiteral("notificationTimer"));
    notificationTimer_->setSingleShot(true);
    notificationTimer_->setInterval(5000);
    connect(notificationTimer_, &QTimer::timeout, this, &CourseBar::dismissNotification);
    connect(notificationAnimation_, &QPropertyAnimation::finished, this, [this] {
        if (notificationPhase_ == NotificationPhase::Entering) {
            notificationPhase_ = NotificationPhase::Holding;
            notificationLayer_->setGeometry(rect());
            notificationTimer_->start();
        } else if (notificationPhase_ == NotificationPhase::Leaving) {
            finishNotification();
        }
    });
}

void CourseBar::showNotification(const QString& message)
{
    showNotification(QStringList{message});
}

void CourseBar::showNotification(const QStringList& messages)
{
    Notification notification;
    for (const QString& message : messages) {
        if (!message.trimmed().isEmpty()) notification.messages.append(message);
    }
    if (!notification.messages.isEmpty()) enqueueNotification(notification);
}

void CourseBar::showCourseNotification(const CourseRefreshService::Event& event)
{
    auto* source = qobject_cast<CourseRefreshService*>(sender());
    if (!source || event.tableRevision != source->table().revision) return;
    Notification notification;
    notification.source = source;
    notification.tableRevision = event.tableRevision;
    notification.eventKey = QStringLiteral("%1:%2:%3")
        .arg(static_cast<int>(event.type)).arg(event.scheduledTime.toMSecsSinceEpoch()).arg(event.subject);
    using Type = CourseRefreshService::EventType;
    switch (event.type) {
    case Type::ClassStarted:
        notification.messages = {tr("%1 已经上课，请回到座位").arg(event.subject),
                                 tr("%1 已上课").arg(event.subject), tr("上课时间到")};
        break;
    case Type::ClassEnded:
        notification.messages = {tr("%1 已经下课，请做好下节课上课准备").arg(event.subject),
                                 tr("%1 已下课").arg(event.subject), tr("下课时间到")};
        break;
    case Type::ClassStartingSoon:
        notification.messages = {tr("%1 即将上课，请做好上课准备").arg(event.subject),
                                 tr("%1 即将上课").arg(event.subject), tr("即将上课")};
        break;
    }
    if (!notification.messages.isEmpty()) enqueueNotification(notification);
}

void CourseBar::enqueueNotification(const Notification& notification)
{
    if (!config.enable) return;
    // Multiple CourseView instances may publish the same course boundary.
    if (!notification.eventKey.isEmpty()) {
        if (currentNotification_ && currentNotification_->eventKey == notification.eventKey) return;
        for (const Notification& pending : pendingNotifications_)
            if (pending.eventKey == notification.eventKey) return;
    }
    pendingNotifications_.enqueue(notification);
    showNextNotification();
}

void CourseBar::showNextNotification()
{
    if (currentNotification_ || transitioning_) return;
    while (!pendingNotifications_.isEmpty()) {
        const Notification& pending = pendingNotifications_.head();
        if (pending.tableRevision && (!pending.source || pending.source->table().revision != pending.tableRevision)) {
            pendingNotifications_.dequeue();
            continue;
        }
        // Use animation completion to resume, without a nested event loop.
        if (collapsed_) {
            toggleCollapsed();
            return;
        }
        currentNotification_ = pendingNotifications_.dequeue();
        updateBarGeometry();
        selectNotificationText();
        notificationPhase_ = NotificationPhase::Entering;
        notificationAnimation_->setDirection(QAbstractAnimation::Forward);
        notificationAnimation_->setStartValue(notificationHiddenRect());
        notificationAnimation_->setEndValue(rect());
        notificationLayer_->setGeometry(notificationHiddenRect());
        notificationLayer_->show();
        notificationLayer_->raise();
        notificationAnimation_->start();
        return;
    }
}

void CourseBar::dismissNotification()
{
    if (notificationPhase_ != NotificationPhase::Holding) return;
    notificationPhase_ = NotificationPhase::Leaving;
    notificationAnimation_->setStartValue(notificationHiddenRect());
    notificationAnimation_->setEndValue(rect());
    notificationAnimation_->setDirection(QAbstractAnimation::Backward);
    notificationAnimation_->start();
}

void CourseBar::finishNotification()
{
    notificationTimer_->stop();
    notificationAnimation_->stop();
    notificationLayer_->hide();
    notificationPhase_ = NotificationPhase::Hidden;
    currentNotification_.reset();
    updateBarGeometry();
    QTimer::singleShot(0, this, &CourseBar::showNextNotification);
}

void CourseBar::invalidateNotifications(CourseRefreshService* source)
{
    for (qsizetype i = pendingNotifications_.size(); i > 0; --i) {
        const Notification& pending = pendingNotifications_[i - 1];
        if (pending.source == source && pending.tableRevision != source->table().revision)
            pendingNotifications_.removeAt(i - 1);
    }
    if (currentNotification_ && currentNotification_->source == source &&
        currentNotification_->tableRevision != source->table().revision) finishNotification();
}

int CourseBar::notificationMinimumWidth() const
{
    if (!currentNotification_) return 0;
    const QMargins margins = notificationLayout_->contentsMargins();
    const QFontMetrics metrics(notificationText_->font());
    int textWidth = metrics.horizontalAdvance(currentNotification_->messages.first());
    for (const QString& message : currentNotification_->messages)
        textWidth = qMin(textWidth, metrics.horizontalAdvance(message));
    return margins.left() + margins.right() + notificationIcon_->width() +
           notificationLayout_->spacing() + textWidth + 2;
}

void CourseBar::selectNotificationText()
{
    if (!currentNotification_) return;
    const QMargins margins = notificationLayout_->contentsMargins();
    const int available = width() - margins.left() - margins.right() -
                          notificationIcon_->width() - notificationLayout_->spacing();
    const QFontMetrics metrics(notificationText_->font());
    QString text = currentNotification_->messages.last();
    for (const QString& message : currentNotification_->messages) {
        if (metrics.horizontalAdvance(message) <= available) {
            text = message;
            break;
        }
    }
    notificationText_->setText(text);
}

QRect CourseBar::notificationHiddenRect() const
{
    return QRect((width() - height()) / 2, -qRound(height() * 78.0 / 49.0), height(), height());
}

void CourseBar::updateNotificationGeometry()
{
    if (!notificationLayer_ || !currentNotification_) return;
    selectNotificationText();
    if (notificationPhase_ == NotificationPhase::Holding) {
        notificationLayer_->setGeometry(rect());
    } else if (notificationPhase_ == NotificationPhase::Entering || notificationPhase_ == NotificationPhase::Leaving) {
        const int time = notificationAnimation_->currentTime();
        notificationAnimation_->setStartValue(notificationHiddenRect());
        notificationAnimation_->setEndValue(rect());
        notificationAnimation_->setCurrentTime(time);
    }
    notificationLayer_->raise();
}
