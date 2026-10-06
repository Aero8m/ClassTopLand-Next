#include "TaskbarTrayMenu.h"

#include "../Core/Logger/Logger.h"

#include <ElaApplication.h>
#include <ElaIconButton.h>
#include <ElaScrollArea.h>
#include <ElaScrollPageArea.h>
#include <ElaText.h>
#include <ElaTheme.h>

#include <QApplication>
#include <QAbstractButton>
#include <QKeyEvent>
#include <QScrollBar>
#include <QCloseEvent>
#include <QCursor>
#include <QEvent>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHideEvent>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QRegion>
#include <QResizeEvent>
#include <QScreen>
#include <QShortcut>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int PanelWidth = 380;
constexpr int PanelMaxHeight = 480;
constexpr int AnchorGap = 10;
constexpr int CornerRadius = 6;
constexpr int SlideDistance = 12;
constexpr int OpenDuration = 180;
constexpr int CloseDuration = 140;
constexpr int ScheduleCardHeight = 56;
constexpr int ScheduleCardGap = 8;
constexpr int VisibleScheduleRows = 2;

QSize panelSize(const QRect& available, const QSize& preferred)
{
    // Leave room for the slide even on a small screen or at a high DPI scale.
    const int heightLimit = qMax(1, available.height() - SlideDistance);
    return {qMin(PanelWidth, available.width()), qMin(preferred.height(), qMin(PanelMaxHeight, heightLimit))};
}

// A real button provides tab focus and accessible checked state, while custom
// painting keeps both lines elided and all colors in the Ela theme palette.
class ScheduleCard final : public QAbstractButton
{
public:
    explicit ScheduleCard(QWidget* parent) : QAbstractButton(parent)
    {
        setFixedHeight(ScheduleCardHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setCheckable(true);
        connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { update(); });
    }

    void setContent(const QString& title, const QString& detail, bool current)
    {
        setText(title);
        detail_ = detail;
        setChecked(current);
        setAccessibleName(title);
        setAccessibleDescription(detail + (current ? tr("，当前课表") : QString()));
        setToolTip(title + QLatin1Char('\n') + detail);
        setProperty("detail", detail);
        update();
    }

    QSize sizeHint() const override { return {240, ScheduleCardHeight}; }
    QSize minimumSizeHint() const override { return {0, ScheduleCardHeight}; }

protected:
    // Checking is controlled by a successful save, never by a speculative click.
    void nextCheckState() override {}
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (!event->isAutoRepeat()) click();
            event->accept();
            return;
        }
        QAbstractButton::keyPressEvent(event);
    }
    void paintEvent(QPaintEvent*) override
    {
        const auto mode = eTheme->getThemeMode();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF outline = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QColor background = ElaThemeColor(mode, BasicBase);
        if (isDown()) background = ElaThemeColor(mode, BasicPress);
        else if (underMouse()) background = ElaThemeColor(mode, BasicHover);
        painter.setBrush(background);
        painter.setPen(hasFocus() ? ElaThemeColor(mode, PrimaryNormal) : ElaThemeColor(mode, BasicBorder));
        painter.drawRoundedRect(outline, 8, 8);
        if (isChecked()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(ElaThemeColor(mode, PrimaryNormal));
            painter.drawRoundedRect(QRectF(10, 12, 3, 32), 1.5, 1.5);
        }
        QFont titleFont = font();
        titleFont.setPixelSize(14);
        titleFont.setBold(true);
        painter.setFont(titleFont);
        const QString badge = tr("当前");
        const int badgeWidth = isChecked() ? QFontMetrics(titleFont).horizontalAdvance(badge) + 22 : 0;
        const QRect titleRect(22, 8, qMax(0, width() - 34 - badgeWidth), 20);
        painter.setPen(ElaThemeColor(mode, BasicText));
        painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(titleFont).elidedText(text(), Qt::ElideRight, titleRect.width()));
        if (isChecked()) {
            const QColor accent = ElaThemeColor(mode, PrimaryNormal);
            painter.setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const int tickX = width() - 12 - badgeWidth;
            painter.drawPolyline(QPolygonF{QPointF(tickX, 17), QPointF(tickX + 4, 21), QPointF(tickX + 11, 13)});
            painter.setPen(accent);
            painter.drawText(QRect(width() - 12 - badgeWidth + 16, 8, badgeWidth - 16, 20),
                             Qt::AlignRight | Qt::AlignVCenter, badge);
        }
        QFont detailFont = font();
        detailFont.setPixelSize(12);
        painter.setFont(detailFont);
        painter.setPen(ElaThemeColor(mode, BasicDetailsText));
        const QRect detailRect(22, 31, qMax(0, width() - 34), 18);
        painter.drawText(detailRect, Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(detailFont).elidedText(detail_, Qt::ElideRight, detailRect.width()));
    }

private:
    QString detail_;
};

QScrollArea* transparentScrollArea(QWidget* parent)
{
    auto* area = new ElaScrollArea(parent);
    area->setFrameShape(QFrame::NoFrame);
    // ElaScrollArea's default stylesheet uses its original objectName. Our named
    // areas need a class selector so renaming cannot restore an opaque viewport.
    area->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget { background: transparent; border: 0px; }"));
    area->setWidgetResizable(true);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    area->setIsAnimation(Qt::Vertical, false);
    area->setFocusPolicy(Qt::NoFocus);
    area->setMinimumSize(0, 0);
    area->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    area->setAutoFillBackground(false);
    area->viewport()->setAutoFillBackground(false);
    return area;
}

QPoint boundedPosition(QPoint position, const QRect& available, const QSize& size)
{
    position.setX(qBound(available.left(), position.x(), available.right() - size.width() + 1));
    position.setY(qBound(available.top(), position.y(), available.bottom() - size.height() + 1));
    return position;
}
}

TaskbarTrayMenu::TaskbarTrayMenu(QWidget* parent)
    : ElaWidget(parent)
{
    setObjectName(QStringLiteral("TaskbarTrayMenu"));
    setWindowTitle(tr("ClassTopLand Next - 快捷面板"));
    setWindowIcon(QIcon(QStringLiteral(":/res/images/icon.png")));
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet(QStringLiteral("#TaskbarTrayMenu { background: transparent; }"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setWindowButtonFlags(ElaAppBarType::CloseButtonHint);
    setIsDefaultClosed(false);
    setIsFixedSize(true);
    setAppBarHeight(36);
    setAttribute(Qt::WA_QuitOnClose, false);
    // This panel is a layered window (translucent and frameless), so its content
    // reaches the screen only through UpdateLayeredWindow. The DWM backdrop that
    // Mica applies to the window breaks those blits, which leaves the layered
    // surface without content and the panel invisible. Keep it out of the global
    // window display mode; the ElaWidget constructor registered it.
    eApp->syncWindowDisplayMode(this, false);
    initUI();

    visibilityAnimation_ = new QParallelAnimationGroup(this);
    visibilityAnimation_->setObjectName(QStringLiteral("visibilityAnimation"));
    positionAnimation_ = new QPropertyAnimation(this, "pos", visibilityAnimation_);
    opacityAnimation_ = new QPropertyAnimation(this, "windowOpacity", visibilityAnimation_);
    visibilityAnimation_->addAnimation(positionAnimation_);
    visibilityAnimation_->addAnimation(opacityAnimation_);
    connect(visibilityAnimation_, &QParallelAnimationGroup::finished, this, [this] {
        if (hiding_) {
            hide();
        } else if (pendingPanelResize_) {
            pendingPanelResize_ = false;
            resizeToAvailableScreen();
        }
    });

    connect(this, &ElaWidget::closeButtonClicked, this, &TaskbarTrayMenu::hideMenu);
    connect(qApp, &QGuiApplication::focusWindowChanged, this, [this] { scheduleDismissal(); });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &TaskbarTrayMenu::hideMenu);

    trayIcon_ = new QSystemTrayIcon(windowIcon(), this);
    trayIcon_->setToolTip(QStringLiteral("ClassTopLand Next"));
    connect(trayIcon_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::Context) {
            // Let the shell finish handling the tray click before taking focus.
            QTimer::singleShot(0, this, &TaskbarTrayMenu::showMenu);
        }
    });
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        Logger::instance().log(Logger::Level::Warning, "System tray is not available");
        Logger::instance().flush();
    }
    trayIcon_->show();
}

TaskbarTrayMenu::~TaskbarTrayMenu()
{
    trayIcon_->hide();
}

void TaskbarTrayMenu::initUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->setSizeConstraint(QLayout::SetNoConstraint);

    bodyScroll_ = transparentScrollArea(this);
    bodyScroll_->setObjectName(QStringLiteral("trayBodyScroll"));
    bodyContent_ = new QWidget;
    bodyContent_->setObjectName(QStringLiteral("trayBody"));
    bodyContent_->setAutoFillBackground(false);
    auto* bodyLayout = new QVBoxLayout(bodyContent_);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->setSizeConstraint(QLayout::SetMinimumSize);
    bodyScroll_->setWidget(bodyContent_);
    bodyContent_->setAutoFillBackground(false);
    mainLayout->addWidget(bodyScroll_, 1);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(18, 12, 18, 0);
    header->setSpacing(10);
    applicationIcon_ = new ElaText(this);
    applicationIcon_->setFixedSize(28, 28);
    applicationIcon_->setPixmap(windowIcon().pixmap(QSize(28, 28), devicePixelRatioF()));
    auto* name = new ElaText(QStringLiteral("ClassTopLand Next"), this);
    name->setTextStyle(ElaTextType::Subtitle);
    name->setTextPixelSize(18);
    name->setWordWrap(false);
    header->addWidget(applicationIcon_);
    header->addWidget(name, 1);
    bodyLayout->addLayout(header);

    auto* shortcuts = new QVBoxLayout;
    shortcuts->setContentsMargins(18, 14, 18, 16);
    shortcuts->setSpacing(10);
    auto* heading = new ElaText(tr("快捷方式"), this);
    heading->setTextStyle(ElaTextType::Subtitle);
    heading->setTextPixelSize(16);
    shortcuts->addWidget(heading);

    auto* cards = new QGridLayout;
    cards->setHorizontalSpacing(8);
    cards->setVerticalSpacing(12);
    cards->setColumnStretch(0, 1);
    cards->setColumnStretch(1, 1);
    auto addShortcut = [this, cards, index = 0](ElaIconType::IconName icon, const QString& text,
                                     const QString& objectName) mutable -> ElaIconButton* {
        auto* column = new QVBoxLayout;
        column->setSpacing(6);
        auto* card = new ElaScrollPageArea(this);
        card->setFixedHeight(56);
        card->setBorderRadius(8);
        auto* cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(1, 1, 1, 1);
        auto* button = new ElaIconButton(icon, 24, card);
        button->setObjectName(objectName);
        button->setAccessibleName(text);
        button->setBorderRadius(8);
        button->setCursor(Qt::PointingHandCursor);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        cardLayout->addWidget(button);
        auto* label = new ElaText(text, 13, this);
        label->setAlignment(Qt::AlignCenter);
        column->addWidget(card);
        column->addWidget(label);
        cards->addLayout(column, index / 2, index % 2);
        ++index;
        return button;
    };
    auto* settingsButton = addShortcut(ElaIconType::Gear, tr("设置"), QStringLiteral("settingsButton"));
    auto* profileEditorButton = addShortcut(ElaIconType::PenToSquare, tr("档案编辑"),
                                            QStringLiteral("courseEditorButton"));
    // The panel only reports the request; AppComposer owns and shows both windows.
    // The editor's objectName stays "courseEditorButton" because the local panel check
    // harness looks the button up by that name; only the label became "档案编辑".
    connect(settingsButton, &ElaIconButton::clicked, this, &TaskbarTrayMenu::settingsRequested);
    connect(profileEditorButton, &ElaIconButton::clicked, this,
            &TaskbarTrayMenu::profileEditorRequested);
    auto* reschedule = addShortcut(ElaIconType::CalendarDays, tr("调休"), QStringLiteral("rescheduleButton"));
    auto* swap = addShortcut(ElaIconType::ArrowRightArrowLeft, tr("换课"), QStringLiteral("swapClassesButton"));
    connect(reschedule, &ElaIconButton::clicked, this, &TaskbarTrayMenu::rescheduleWindowRequested);
    connect(swap, &ElaIconButton::clicked, this, &TaskbarTrayMenu::swapWindowRequested);
    shortcuts->addLayout(cards);
    bodyLayout->addLayout(shortcuts);

    auto* switcher = new QVBoxLayout;
    switcher->setContentsMargins(18, 0, 18, 16);
    switcher->setSpacing(ScheduleCardGap);
    auto* switchHeading = new ElaText(tr("切换课程表"), bodyContent_);
    switchHeading->setTextStyle(ElaTextType::Subtitle);
    switchHeading->setTextPixelSize(16);
    switcher->addWidget(switchHeading);
    automaticCard_ = new ScheduleCard(bodyContent_);
    automaticCard_->setObjectName(QStringLiteral("automaticScheduleCard"));
    automaticCard_->setProperty("scheduleId", QString());
    updateAutomaticCard();
    connect(automaticCard_, &QAbstractButton::clicked, this, [this] { requestWeekSchedule({}); });

    scheduleScroll_ = transparentScrollArea(bodyContent_);
    scheduleScroll_->setObjectName(QStringLiteral("scheduleListScroll"));
    auto* listContent = new QWidget;
    listContent->setAutoFillBackground(false);
    scheduleLayout_ = new QGridLayout(listContent);
    scheduleLayout_->setContentsMargins(0, 0, 0, 0);
    scheduleLayout_->setSpacing(ScheduleCardGap);
    scheduleLayout_->setColumnStretch(0, 1);
    scheduleLayout_->setColumnStretch(1, 1);
    scheduleLayout_->setAlignment(Qt::AlignTop);
    scheduleLayout_->setSizeConstraint(QLayout::SetMinimumSize);
    scheduleScroll_->setWidget(listContent);
    listContent->setAutoFillBackground(false);
    scheduleLayout_->addWidget(automaticCard_, 0, 0);
    scheduleScroll_->setFixedHeight(ScheduleCardHeight);
    switcher->addWidget(scheduleScroll_);
    scheduleEmpty_ = new ElaText(tr("暂无周课表，请在档案编辑中添加"), 13, bodyContent_);
    scheduleEmpty_->setObjectName(QStringLiteral("scheduleEmpty"));
    scheduleEmpty_->setWordWrap(true);
    switcher->addWidget(scheduleEmpty_);
    scheduleMore_ = new ElaText(tr("更多课表可滚动 ↓"), 12, bodyContent_);
    scheduleMore_->setAlignment(Qt::AlignRight);
    scheduleMore_->hide();
    switcher->addWidget(scheduleMore_);
    scheduleError_ = new ElaText(bodyContent_);
    scheduleError_->setObjectName(QStringLiteral("scheduleError"));
    scheduleError_->setTextPixelSize(13);
    scheduleError_->setWordWrap(true);
    scheduleError_->hide();
    switcher->addWidget(scheduleError_);
    bodyLayout->addLayout(switcher);

    // The panel paints this area's background inside its rounded outline.
    footer_ = new QWidget(this);
    footer_->setFixedHeight(48);
    auto* footerLayout = new QHBoxLayout(footer_);
    footerLayout->setContentsMargins(14, 6, 14, 6);
    footerLayout->setSpacing(8);
    footerLayout->addStretch();
    auto* restart = new ElaIconButton(ElaIconType::ArrowRotateRight, 20, 36, 36, footer_);
    restart->setObjectName(QStringLiteral("restartButton"));
    restart->setBorderRadius(6);
    restart->setToolTip(tr("重启应用"));
    restart->setAccessibleName(restart->toolTip());
    restart->setCursor(Qt::PointingHandCursor);
    auto* quit = new ElaIconButton(ElaIconType::ArrowRightFromBracket, 20, 36, 36, footer_);
    quit->setObjectName(QStringLiteral("quitButton"));
    quit->setBorderRadius(6);
    quit->setToolTip(tr("退出应用"));
    quit->setAccessibleName(quit->toolTip());
    quit->setCursor(Qt::PointingHandCursor);
    footerLayout->addWidget(restart);
    footerLayout->addWidget(quit);
    mainLayout->addWidget(footer_);
    connect(restart, &ElaIconButton::clicked, this, [this] {
        requestExit(RestartExitCode);
    });
    connect(quit, &ElaIconButton::clicked, this, [this] {
        requestExit(0);
    });
    setFixedWidth(PanelWidth);
    adjustSize();
}

void TaskbarTrayMenu::showMenu()
{
    emit scheduleListRefreshRequested();
    ++presentationRevision_;
    if (isVisible() && !hiding_) {
        raise();
        activateWindow();
        return;
    }
    const QRect trayGeometry = trayIcon_->geometry();
    const QPoint anchor = trayGeometry.isValid() ? trayGeometry.center() : QCursor::pos();
    QScreen* screen = QGuiApplication::screenAt(anchor);
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;

    const QRect available = screen->availableGeometry();
    setFixedSize(panelSize(available, sizeHint()));
    const QRect anchorRect = trayGeometry.isValid() ? trayGeometry : QRect(anchor, QSize(1, 1));
    QPoint position(anchorRect.right() - width(), anchorRect.top() - height() - AnchorGap);
    if (anchorRect.right() < available.left()) {
        position = QPoint(anchorRect.right() + AnchorGap, anchor.y() - height() / 2);
    } else if (anchorRect.left() > available.right()) {
        position = QPoint(anchorRect.left() - width() - AnchorGap, anchor.y() - height() / 2);
    } else if (position.y() < available.top()) {
        position.setY(anchorRect.bottom() + AnchorGap);
    }
    position = boundedPosition(position, available, size());
    if (anchorRect.right() < available.left()) {
        animationOffset_ = QPoint(-SlideDistance, 0);
    } else if (anchorRect.left() > available.right()) {
        animationOffset_ = QPoint(SlideDistance, 0);
    } else {
        animationOffset_ = QPoint(0, anchor.y() < position.y() ? -SlideDistance : SlideDistance);
    }
    visibilityAnimation_->stop();
    hiding_ = false;
    pendingPanelResize_ = false;
    if (!isVisible()) {
        QPoint start = boundedPosition(position + animationOffset_, available, size());
        // A boundary must not collapse the slide into a zero-distance animation.
        if (start == position) start = boundedPosition(position - animationOffset_, available, size());
        animationOffset_ = start - position;
        move(start);
        setWindowOpacity(0.0);
    }
    show();
    raise();
    activateWindow();
    startTransition(position, 1.0, OpenDuration);
}

QSize TaskbarTrayMenu::sizeHint() const
{
    if (!bodyContent_ || !footer_) return ElaWidget::sizeHint();
    const auto* bodyLayout = bodyContent_->layout();
    const int bodyHeight = bodyLayout->hasHeightForWidth()
        ? bodyLayout->totalHeightForWidth(PanelWidth) : bodyLayout->sizeHint().height();
    return {PanelWidth, contentsMargins().top() + bodyHeight + footer_->height()};
}

void TaskbarTrayMenu::resizeToAvailableScreen()
{
    if (!isVisible() || hiding_ || !screen()) return;
    const QRect available = screen()->availableGeometry();
    const QSize targetSize = panelSize(available, sizeHint());
    if (targetSize == size()) return;
    if (visibilityAnimation_ && visibilityAnimation_->state() == QAbstractAnimation::Running) {
        // Changing geometry here interrupts the slide's path. Apply size changes
        // after the opening transition, using the existing bottom anchor.
        pendingPanelResize_ = true;
        return;
    }
    const int oldHeight = height();
    setFixedSize(targetSize);
    // Keep the bottom edge anchored when the list grows while the panel is open.
    QPoint position = pos() + QPoint(0, oldHeight - height());
    position = boundedPosition(position, available, size());
    move(position);
}

void TaskbarTrayMenu::updateAutomaticCard()
{
    QString detail = tr("按当天日期匹配课表");
    if (!courseViewsEnabled_ || selectedScheduleId_.isEmpty()) {
        if (!scheduleStatus_.isEmpty()) detail = scheduleStatus_;
    }
    static_cast<ScheduleCard*>(automaticCard_)->setContent(tr("自动匹配"), detail, selectedScheduleId_.isEmpty());
}

void TaskbarTrayMenu::requestWeekSchedule(const QString& id)
{
    if (id == selectedScheduleId_) return;
    scheduleError_->hide();
    emit weekScheduleRequested(id);
}

void TaskbarTrayMenu::setScheduleChoices(const QList<ScheduleChoice>& choices, const QString& selectedId)
{
    bool sameIds = choices.size() == scheduleCards_.size();
    if (sameIds) {
        for (int i = 0; i < choices.size(); ++i)
            if (choices[i].id != scheduleCards_[i]->property("scheduleId").toString()) { sameIds = false; break; }
    }
    QString focusId;
    const bool hadListFocus = focusWidget() && scheduleScroll_->isAncestorOf(focusWidget());
    if (hadListFocus) focusId = focusWidget()->property("scheduleId").toString();
    const int scrollPosition = scheduleScroll_->verticalScrollBar()->value();
    if (!sameIds) {
        qDeleteAll(scheduleCards_);
        scheduleCards_.clear();
        for (int i = 0; i < choices.size(); ++i) {
            const auto& choice = choices[i];
            auto* card = new ScheduleCard(scheduleScroll_->widget());
            card->setObjectName(QStringLiteral("weekScheduleCard"));
            card->setProperty("scheduleId", choice.id);
            connect(card, &QAbstractButton::clicked, this, [this, id = choice.id] { requestWeekSchedule(id); });
            const int cell = i + 1; // The automatic card occupies the first cell.
            scheduleLayout_->addWidget(card, cell / 2, cell % 2);
            scheduleCards_.append(card);
        }
    }
    if (selectedScheduleId_ != selectedId) scheduleError_->hide();
    selectedScheduleId_ = selectedId;
    for (int i = 0; i < choices.size(); ++i) {
        static_cast<ScheduleCard*>(scheduleCards_[i])->setContent(choices[i].name, choices[i].detail,
                                                              choices[i].id == selectedId);
    }
    // Cards are created after the footer, so explicitly follow visual order.
    auto* previous = automaticCard_;
    for (auto* card : scheduleCards_) {
        QWidget::setTabOrder(previous, card);
        previous = card;
    }
    if (auto* restart = findChild<QAbstractButton*>(QStringLiteral("restartButton"))) {
        QWidget::setTabOrder(previous, restart);
        if (auto* quit = findChild<QAbstractButton*>(QStringLiteral("quitButton")))
            QWidget::setTabOrder(restart, quit);
    }
    const int rows = (static_cast<int>(choices.size()) + 2) / 2;
    const int visibleRows = qMin(VisibleScheduleRows, rows);
    scheduleScroll_->setFixedHeight(visibleRows * ScheduleCardHeight + (visibleRows - 1) * ScheduleCardGap);
    scheduleEmpty_->setVisible(choices.isEmpty());
    scheduleMore_->setVisible(rows > VisibleScheduleRows);
    updateAutomaticCard();
    bodyContent_->layout()->invalidate();
    bodyContent_->layout()->activate();
    updateGeometry();
    resizeToAvailableScreen();
    if (!sameIds) {
        scheduleScroll_->verticalScrollBar()->setValue(scrollPosition);
        if (hadListFocus) {
            QAbstractButton* target = automaticCard_;
            for (auto* card : scheduleCards_) if (card->property("scheduleId").toString() == focusId) target = card;
            target->setFocus(Qt::OtherFocusReason);
        }
    }
}

void TaskbarTrayMenu::setScheduleStatus(const QString& status, bool courseViewsEnabled)
{
    scheduleStatus_ = status;
    courseViewsEnabled_ = courseViewsEnabled;
    updateAutomaticCard();
}

void TaskbarTrayMenu::showScheduleError(const QString& message)
{
    scheduleError_->setText(message);
    scheduleError_->show();
    bodyContent_->layout()->invalidate();
    resizeToAvailableScreen();
    bodyScroll_->ensureWidgetVisible(scheduleError_);
}

void TaskbarTrayMenu::hideMenu()
{
    if (!isVisible() || hiding_) return;
    ++presentationRevision_;
    hiding_ = true;
    const QRect available = screen()->availableGeometry();
    const QPoint position = boundedPosition(pos() + animationOffset_, available, size());
    startTransition(position, 0.0, CloseDuration);
}

void TaskbarTrayMenu::requestExit(int exitCode)
{
    // A double click, a duplicated signal, or a queued repeat must not exit twice.
    if (exitRequested_ || checkingExit_) return;
    checkingExit_ = true;
    const bool allowed = !exitGuard_ || exitGuard_(exitCode);
    checkingExit_ = false;
    if (!allowed) return;
    exitRequested_ = true;
    emit exitAccepted(exitCode);
    // Written before the loop ends, so a failure later is distinguishable from
    // the click never reaching the button at all.
    Logger::instance().log(Logger::Level::Info,
                           QStringLiteral("Exit requested: code=%1").arg(exitCode));
    Logger::instance().flush();
    // Let the click event and any queued dismissal or deactivation finish first:
    // those paths must be unable to interrupt the exit instruction. `this` is the
    // context object, so a destroyed panel cancels the pending exit.
    QTimer::singleShot(0, this, [exitCode] {
        QCoreApplication::exit(exitCode);
    });
}

void TaskbarTrayMenu::startTransition(const QPoint& position, qreal opacity, int duration)
{
    visibilityAnimation_->stop();
    const auto easing = hiding_ ? QEasingCurve::InCubic : QEasingCurve::OutCubic;
    positionAnimation_->setDuration(duration);
    positionAnimation_->setStartValue(pos());
    positionAnimation_->setEndValue(position);
    positionAnimation_->setEasingCurve(easing);
    opacityAnimation_->setDuration(duration);
    opacityAnimation_->setStartValue(windowOpacity());
    opacityAnimation_->setEndValue(opacity);
    opacityAnimation_->setEasingCurve(easing);
    visibilityAnimation_->start();
}

void TaskbarTrayMenu::scheduleDismissal()
{
    if (!isVisible()) return;
    const quint64 revision = presentationRevision_;
    QTimer::singleShot(0, this, [this, revision] {
        if (revision == presentationRevision_ && isVisible() && !isActiveWindow()) hideMenu();
    });
}

bool TaskbarTrayMenu::event(QEvent* event)
{
    const bool result = ElaWidget::event(event);
    if (event->type() == QEvent::WindowDeactivate) {
        scheduleDismissal();
    } else if (event->type() == QEvent::DevicePixelRatioChange && applicationIcon_) {
        applicationIcon_->setPixmap(windowIcon().pixmap(applicationIcon_->size(), devicePixelRatioF()));
    }
    return result;
}

void TaskbarTrayMenu::closeEvent(QCloseEvent* event)
{
    event->ignore();
    hideMenu();
}

void TaskbarTrayMenu::hideEvent(QHideEvent* event)
{
    if (visibilityAnimation_) visibilityAnimation_->stop();
    hiding_ = false;
    ++presentationRevision_;
    setWindowOpacity(1.0);
    ElaWidget::hideEvent(event);
}

void TaskbarTrayMenu::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    const auto theme = eTheme->getThemeMode();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath outline;
    outline.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), CornerRadius, CornerRadius);
    painter.fillPath(outline, ElaThemeColor(theme, WindowBase));
    painter.setClipPath(outline);
    if (footer_) {
        painter.fillRect(footer_->geometry(), ElaThemeColor(theme, BasicBaseAlpha));
        painter.setPen(ElaThemeColor(theme, BasicBorder));
        painter.drawLine(footer_->geometry().topLeft(), footer_->geometry().topRight());
    }
    painter.setClipping(false);
    painter.setPen(ElaThemeColor(theme, PopupBorder));
    painter.drawPath(outline);
}

void TaskbarTrayMenu::resizeEvent(QResizeEvent* event)
{
    ElaWidget::resizeEvent(event);
    // Clip the titlebar's close-button hover and Windows 10 border at the top corners.
    // Keep the top-level window unmasked so its translucent edges stay antialiased.
    if (auto* appBar = findChild<ElaAppBar*>()) {
        QPainterPath outline;
        outline.addRoundedRect(QRectF(rect()), CornerRadius, CornerRadius);
        const QRegion roundedRegion(outline.toFillPolygon().toPolygon());
        appBar->setMask(roundedRegion.translated(-appBar->pos()).intersected(appBar->rect()));
    }
}
