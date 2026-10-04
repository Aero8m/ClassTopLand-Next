#include "TaskbarTrayMenu.h"

#include "../Core/Logger/Logger.h"

#include <ElaIconButton.h>
#include <ElaScrollPageArea.h>
#include <ElaText.h>
#include <ElaTheme.h>

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QEvent>
#include <QHBoxLayout>
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
constexpr int PanelWidth = 440;
constexpr int AnchorGap = 10;
constexpr int CornerRadius = 6;
constexpr int SlideDistance = 12;
constexpr int OpenDuration = 180;
constexpr int CloseDuration = 140;

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
    setAppBarHeight(40);
    setAttribute(Qt::WA_QuitOnClose, false);
    initUI();

    visibilityAnimation_ = new QParallelAnimationGroup(this);
    visibilityAnimation_->setObjectName(QStringLiteral("visibilityAnimation"));
    positionAnimation_ = new QPropertyAnimation(this, "pos", visibilityAnimation_);
    opacityAnimation_ = new QPropertyAnimation(this, "windowOpacity", visibilityAnimation_);
    visibilityAnimation_->addAnimation(positionAnimation_);
    visibilityAnimation_->addAnimation(opacityAnimation_);
    connect(visibilityAnimation_, &QParallelAnimationGroup::finished, this, [this] {
        if (hiding_) hide();
    });

    connect(this, &ElaWidget::closeButtonClicked, this, &TaskbarTrayMenu::hideMenu);
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

    auto* header = new QHBoxLayout;
    header->setContentsMargins(24, 24, 24, 0);
    header->setSpacing(12);
    applicationIcon_ = new ElaText(this);
    applicationIcon_->setFixedSize(40, 40);
    applicationIcon_->setPixmap(windowIcon().pixmap(QSize(40, 40), devicePixelRatioF()));
    auto* name = new ElaText(QStringLiteral("ClassTopLand Next"), this);
    name->setTextStyle(ElaTextType::Subtitle);
    name->setTextPixelSize(22);
    name->setWordWrap(false);
    header->addWidget(applicationIcon_);
    header->addWidget(name, 1);
    mainLayout->addLayout(header);

    auto* shortcuts = new QVBoxLayout;
    shortcuts->setContentsMargins(24, 22, 24, 24);
    shortcuts->setSpacing(14);
    auto* heading = new ElaText(tr("快捷方式"), this);
    heading->setTextStyle(ElaTextType::Subtitle);
    heading->setTextPixelSize(18);
    shortcuts->addWidget(heading);

    auto* cards = new QHBoxLayout;
    cards->setSpacing(12);
    auto addShortcut = [this, cards](ElaIconType::IconName icon, const QString& text,
                                     const QString& objectName) {
        auto* column = new QVBoxLayout;
        column->setSpacing(8);
        auto* card = new ElaScrollPageArea(this);
        card->setFixedHeight(76);
        card->setBorderRadius(8);
        auto* cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(1, 1, 1, 1);
        auto* button = new ElaIconButton(icon, 28, card);
        button->setObjectName(objectName);
        button->setAccessibleName(text);
        button->setBorderRadius(8);
        button->setCursor(Qt::PointingHandCursor);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        cardLayout->addWidget(button);
        auto* label = new ElaText(text, 14, this);
        label->setAlignment(Qt::AlignCenter);
        column->addWidget(card);
        column->addWidget(label);
        cards->addLayout(column, 1);
        // Settings and course editing intentionally have no actions yet.
    };
    addShortcut(ElaIconType::Gear, tr("设置"), QStringLiteral("settingsButton"));
    addShortcut(ElaIconType::PenToSquare, tr("课程编辑"), QStringLiteral("courseEditorButton"));
    shortcuts->addLayout(cards);
    mainLayout->addLayout(shortcuts);

    // The panel paints this area's background inside its rounded outline.
    footer_ = new QWidget(this);
    footer_->setFixedHeight(56);
    auto* footerLayout = new QHBoxLayout(footer_);
    footerLayout->setContentsMargins(18, 8, 18, 8);
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
    setFixedSize(qMin(PanelWidth, available.width()), qMin(sizeHint().height(), available.height()));
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
    if (!isVisible()) {
        move(boundedPosition(position + animationOffset_, available, size()));
        setWindowOpacity(0.0);
    }
    show();
    raise();
    activateWindow();
    startTransition(position, 1.0, OpenDuration);
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
    if (exitRequested_) return;
    exitRequested_ = true;
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

bool TaskbarTrayMenu::event(QEvent* event)
{
    const bool result = ElaWidget::event(event);
    if (event->type() == QEvent::WindowDeactivate) {
        const quint64 revision = presentationRevision_;
        QTimer::singleShot(0, this, [this, revision] {
            if (revision == presentationRevision_ && isVisible() && !isActiveWindow()) hideMenu();
        });
    } else if (event->type() == QEvent::DevicePixelRatioChange && applicationIcon_) {
        applicationIcon_->setPixmap(windowIcon().pixmap(QSize(40, 40), devicePixelRatioF()));
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
