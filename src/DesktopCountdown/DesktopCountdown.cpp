#include "DesktopCountdown.h"
#include "../Core/ConfigManager/ConfigManager.h"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHideEvent>
#include <QLabel>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include <QtMath>
#include <utility>

namespace {
const QStringList& fontFamilies()
{
    static const QStringList families = [] {
        QStringList result;
        for (int i = 1; i <= 3; ++i) {
            const QString path = QStringLiteral(":/res/fonts/DTF-%1.ttf").arg(i);
            const int id = QFontDatabase::addApplicationFont(path);
            const auto names = QFontDatabase::applicationFontFamilies(id);
            if (id < 0 || names.isEmpty()) {
                qWarning("DesktopCountdown: unable to load font %d; using system font", i);
                result.append(QFontDatabase::systemFont(QFontDatabase::GeneralFont).family());
            } else result.append(names.first());
        }
        return result;
    }();
    return families;
}

QString padded(qint64 value, int width = 2)
{
    return QStringLiteral("%1").arg(value, width, 10, QLatin1Char('0'));
}
}

DesktopCountdown::DesktopCountdown(QWidget* parent) : DesktopCountdown(Clock{}, parent) {}

DesktopCountdown::DesktopCountdown(Clock clock, QWidget* parent)
    : QWidget(parent), clock_(clock ? std::move(clock) : Clock{[] { return QDateTime::currentDateTime(); }})
{
    setObjectName(QStringLiteral("desktopCountdown"));
    setWindowTitle(QStringLiteral("DesktopCountdown"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnBottomHint |
                   Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    const auto label = [this](const QString& name, const QString& color) {
        auto* item = new QLabel(this);
        item->setObjectName(name);
        item->setTextFormat(Qt::PlainText);
        item->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(color));
        return item;
    };
    title_ = label(QStringLiteral("desktopCountdownTitle"), QStringLiteral("#ffffff"));
    title_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    prefix_ = label(QStringLiteral("desktopCountdownPrefix"), QStringLiteral("#ffffff"));
    number_ = label(QStringLiteral("desktopCountdownNumber"), QStringLiteral("#fc040d"));
    unit_ = label(QStringLiteral("desktopCountdownUnit"), QStringLiteral("#ffffff"));
    tail_ = label(QStringLiteral("desktopCountdownTail"), QStringLiteral("#ababab"));
    english_ = label(QStringLiteral("desktopCountdownEnglish"), QStringLiteral("#ffffff"));
    english_->setWordWrap(false);
    english_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    accent_ = new QWidget(this);
    accent_->setObjectName(QStringLiteral("desktopCountdownAccent"));
    accent_->setStyleSheet(QStringLiteral("background: #fc040d;"));
    timer_ = new QTimer(this);
    timer_->setObjectName(QStringLiteral("desktopCountdownTimer"));
    connect(timer_, &QTimer::timeout, this, &DesktopCountdown::refresh);
    for (auto* screen : QGuiApplication::screens()) watchScreen(screen);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        watchScreen(screen); layoutKey_.clear(); refresh();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] {
        // Qt may still be removing the screen from its list during this signal.
        QTimer::singleShot(0, this, [this] { layoutKey_.clear(); refresh(); });
    });
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, [this] {
        layoutKey_.clear(); refresh();
    });
    reloadConfig();
}

void DesktopCountdown::watchScreen(QScreen* screen)
{
    const auto changed = [this] { layoutKey_.clear(); refresh(); };
    connect(screen, &QScreen::availableGeometryChanged, this, changed);
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, changed);
}

QScreen* DesktopCountdown::targetScreen() const
{
    if (!config_.screenName.isEmpty())
        for (auto* screen : QGuiApplication::screens())
            if (screen->name() == config_.screenName) return screen;
    return QGuiApplication::primaryScreen();
}

void DesktopCountdown::reloadConfig()
{
    config_ = ConfigManager::instance().config().desktopCountdownConfig;
    layoutKey_.clear();
    refresh();
    setVisible(config_.enable && config_.targetDateTime.isValid());
}

void DesktopCountdown::refresh()
{
    const qint64 remaining = config_.targetDateTime.isValid()
        ? clock_().msecsTo(config_.targetDateTime) : 0;
    expired_ = remaining <= 0;
    QString englishUnit;
    if (expired_) {
        prefix_->clear(); unit_->clear(); tail_->clear();
        number_->setText(tr("一天不剩"));
        english_->setText(QStringLiteral("There is not a DAY\nleft until %1").arg(config_.englishTag).toUpper());
    } else {
        prefix_->setText(tr("还剩"));
        // Round up only in seconds mode, so a positive fraction never appears expired.
        const qint64 duration = config_.showMilliseconds ? remaining : ((remaining + 999) / 1000) * 1000;
        const qint64 days = duration / 86400000;
        const qint64 hours = duration / 3600000 % 24;
        const qint64 minutes = duration / 60000 % 60;
        const qint64 seconds = duration / 1000 % 60;
        const QString fraction = config_.showMilliseconds ? '.' + padded(duration % 1000, 3) : QString();
        qint64 value;
        QString tail;
        if (days > 0) {
            value = days; unit_->setText(tr("天")); englishUnit = QStringLiteral("DAYS");
            tail = ':' + padded(hours) + ':' + padded(minutes) + ':' + padded(seconds) + fraction;
        } else if (hours > 0) {
            value = hours; unit_->setText(tr("时")); englishUnit = QStringLiteral("HOURS");
            tail = ':' + padded(minutes) + ':' + padded(seconds) + fraction;
        } else if (minutes > 0) {
            value = minutes; unit_->setText(tr("分")); englishUnit = QStringLiteral("MINUTES");
            tail = ':' + padded(seconds) + fraction;
        } else if (seconds > 0) {
            value = seconds; unit_->setText(tr("秒")); englishUnit = QStringLiteral("SECONDS");
            tail = fraction;
        } else {
            value = duration; unit_->setText(tr("毫秒")); englishUnit = QStringLiteral("MILLISECONDS");
        }
        number_->setText(QString::number(value));
        tail_->setText(tail);
        english_->setText(QStringLiteral("There are %1 %2\nleft until %3")
            .arg(value).arg(englishUnit, config_.englishTag).toUpper());
    }
    updateGeometryAndFonts();
    const int interval = expired_ || !config_.showMilliseconds ? 1000 : 50;
    if (timer_->interval() != interval) timer_->setInterval(interval);
    if (isVisible() && config_.enable && !timer_->isActive()) timer_->start();
}

void DesktopCountdown::updateGeometryAndFonts()
{
    auto* screen = targetScreen();
    if (!screen) return;
    const QRect available = screen->availableGeometry();
    const QString title = tr("距离%1").arg(config_.labelTag);
    // Numeric width is reserved for the widest digit, keeping the layout stable.
    const QString key = QStringLiteral("%1/%2/%3/%4/%5/%6/%7/%8")
        .arg(expired_).arg(number_->text().size()).arg(unit_->text(), title, config_.englishTag)
        .arg(available.width()).arg(available.height()).arg(config_.scalePercent);
    if (layoutKey_ != key) {
        layoutKey_ = key;
        const auto& families = fontFamilies();
        const auto font = [&](int family, qreal points, qreal scale = 1.0) {
            QFont result(families.at(family)); result.setPointSizeF(points * scale); return result;
        };
        const QFontMetrics numberMetrics(font(expired_ ? 0 : 1, expired_ ? 47 : 110));
        int digitWidth = 0;
        for (QChar c : QStringLiteral("0123456789")) digitWidth = qMax(digitWidth, numberMetrics.horizontalAdvance(c));
        const int numberWidth = expired_ ? numberMetrics.horizontalAdvance(number_->text()) + 2
            : digitWidth * number_->text().size() + 2;
        QString tailSample;
        if (unit_->text() == tr("天")) tailSample = QStringLiteral(":88:88:88");
        else if (unit_->text() == tr("时")) tailSample = QStringLiteral(":88:88");
        else if (unit_->text() == tr("分")) tailSample = QStringLiteral(":88");
        if (config_.showMilliseconds && unit_->text() != tr("毫秒") && !expired_)
            tailSample += QStringLiteral(".888");
        const int tailWidth = QFontMetrics(font(1, 20)).horizontalAdvance(tailSample) + 4;
        const int unitWidth = QFontMetrics(font(0, 48)).horizontalAdvance(unit_->text()) + 4;
        const int suffixX = 350 + numberWidth + 10;
        const int baseWidth = qMax(721, expired_ ? 230 + numberWidth + 10
            : suffixX + qMax(tailWidth, unitWidth) + 10);
        // Keep the original 721x330 composition and scale every anchor together.
        // Only extend its right edge when a large day count would be clipped.
        const qreal scale = qMin(config_.scalePercent / 100.0,
            qMin(available.width() / qreal(baseWidth), available.height() / 330.0));
        const auto position = [scale](int value) { return qRound(value * scale); };
        const auto extent = [scale](int value) { return qMax(1, qRound(value * scale)); };
        const auto place = [&](QWidget* item, int x, int y, int width, int height) {
            item->setGeometry(position(x), position(y), extent(width), extent(height));
        };
        title_->setFont(font(0, 36, scale)); prefix_->setFont(font(0, 48, scale));
        unit_->setFont(font(0, 48, scale)); number_->setFont(font(expired_ ? 0 : 1, expired_ ? 47 : 110, scale));
        tail_->setFont(font(1, 20, scale)); english_->setFont(font(2, 20, scale));
        // DayTimer's title ends immediately to the left of the large number.
        // The prefix and unit sit on the lower row; the tail sits above the unit.
        place(title_, 10, 70, 331, 41);
        place(prefix_, 220, 120, 141, 61);
        place(number_, expired_ ? 230 : 350, expired_ ? 100 : 70, numberWidth, 111);
        place(tail_, suffixX, 100, tailWidth, 21);
        place(unit_, suffixX, 120, unitWidth, 61);
        place(english_, 230, 190, 351, 61);
        place(accent_, 209, 120, 6, 131);
        title_->setText(title_->fontMetrics().elidedText(title, Qt::ElideRight, title_->width()));
        title_->setToolTip(title);
        resize(qMin(available.width(), extent(baseWidth)), qMin(available.height(), extent(330)));
    }
    const QString fullEnglish = english_->text();
    QStringList lines = fullEnglish.split('\n');
    for (auto& line : lines)
        line = english_->fontMetrics().elidedText(line, Qt::ElideRight, english_->width());
    english_->setText(lines.join('\n'));
    english_->setToolTip(fullEnglish);
    // Include the screen origin: secondary screens may have negative coordinates.
    move(qMax(available.left(), available.right() + 1 - width() - config_.rightMargin),
         qMax(available.top(), available.bottom() + 1 - height() - config_.bottomMargin));
}

void DesktopCountdown::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
}

void DesktopCountdown::hideEvent(QHideEvent* event)
{
    timer_->stop();
    QWidget::hideEvent(event);
}
