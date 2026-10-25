#include "DesktopCountdown.h"
#include "../Core/ConfigManager/ConfigManager.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QGuiApplication>
#include <QHideEvent>
#include <QImage>
#include <QLabel>
#include <QScreen>
#include <QShowEvent>
#include <QStyle>
#include <QTextLayout>
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

QRectF labelInkRect(const QLabel* label)
{
    if (label->text().isEmpty()) return {};
    // Match QLabel's plain-text drawing: explicit line breaks, no wrapping,
    // whole-pixel line positions and vertical alignment of the full line box.
    QString text = label->text();
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QChar::LineSeparator);
    QTextLayout layout(text, label->font(), label);
    QTextOption option;
    const auto direction = text.isRightToLeft() ? Qt::RightToLeft : Qt::LeftToRight;
    option.setTextDirection(direction);
    layout.setTextOption(option);
    const QFontMetricsF metrics(label->font(), label);
    const qreal leading = metrics.leading();
    qreal height = -leading;
    layout.beginLayout();
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(0x01000000);
        height = qCeil(height + leading);
        line.setPosition(QPointF(0, height));
        height += line.ascent() + line.descent();
    }
    layout.endLayout();
    const QRectF rect = label->contentsRect();
    const auto alignment = QStyle::visualAlignment(direction, label->alignment());
    qreal y = rect.y();
    if (alignment & Qt::AlignVCenter) y += (rect.height() - height) / 2;
    else if (alignment & Qt::AlignBottom) y += rect.height() - height;
    QRectF ink;
    for (int i = 0; i < layout.lineCount(); ++i) {
        const QTextLine line = layout.lineAt(i);
        qreal x = rect.x();
        if (alignment & Qt::AlignRight) x += rect.width() - line.horizontalAdvance();
        else if (alignment & Qt::AlignHCenter) x += (rect.width() - line.horizontalAdvance()) / 2;
        for (const auto& run : line.glyphRuns()) {
            const auto glyphs = run.glyphIndexes();
            const auto positions = run.positions();
            const auto font = run.rawFont();
            for (qsizetype j = 0; j < glyphs.size(); ++j)
                ink = ink.united(font.boundingRect(glyphs.at(j)).translated(positions.at(j) + QPointF(x, y)));
        }
    }
    // QPainter clips to the text rectangle only when the line box does not fit.
    QRectF clip = label->rect();
    if (height > rect.height()) clip = clip.intersected(rect);
    return ink.intersected(clip).translated(label->pos());
}

void trimTransparentRight(QWidget* widget)
{
    // Font bounds can overestimate rasterized ink at fractional DPI. Use the
    // final widget's pixel grid (including child clipping), then inspect only
    // its trailing columns. Cached layouts do not need this calibration again.
    const qreal dpr = widget->devicePixelRatioF();
    QImage image(QSize(qCeil(widget->width() * dpr), qCeil(widget->height() * dpr)),
                 QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return;
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    widget->render(&image);
    for (int x = image.width() - 1; x >= 0; --x) {
        for (int y = 0; y < image.height(); ++y) {
            if (qAlpha(reinterpret_cast<const QRgb*>(image.constScanLine(y))[x])) {
                widget->resize(qMin(widget->width(), qCeil((x + 1) / dpr)), widget->height());
                return;
            }
        }
    }
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
        item->setMargin(0);
        item->setIndent(0);
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
    connect(screen, &QScreen::geometryChanged, this, changed);
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
    if (this->screen() != screen) setScreen(screen);
    const QRect available = screen->availableGeometry();
    const QRect screenGeometry = screen->geometry();
    if (available.isEmpty()) return;
    const QString title = tr("距离%1").arg(config_.labelTag);
    const QString fullEnglish = english_->text();
    const QStringList key{title, prefix_->text(), number_->text(), unit_->text(), tail_->text(), fullEnglish,
        QString::number(available.width()), QString::number(available.height()),
        QString::number(config_.scalePercent), QString::number(logicalDpiX()),
        QString::number(logicalDpiY()), QString::number(devicePixelRatioF()),
        QString::number(config_.useLegacyPlacement)};
    if (layoutKey_ != key) {
        layoutKey_ = key;
        const auto& families = fontFamilies();
        qreal scale = config_.scalePercent / 100.0;
        QRect bounds;
        const auto labels = {title_, prefix_, number_, unit_, tail_, english_};
        do {
            const auto font = [&](int family, qreal points) {
                QFont result(families.at(family));
                result.setPointSizeF(points * scale);
                return result;
            };
            title_->setFont(font(0, 36)); prefix_->setFont(font(0, 48));
            unit_->setFont(font(0, 48)); number_->setFont(font(expired_ ? 0 : 1, expired_ ? 47 : 110));
            tail_->setFont(font(1, 20)); english_->setFont(font(2, 20));
            const auto place = [scale](QWidget* item, int x, int y, int width, int height) {
                item->setGeometry(qRound(x * scale), qRound(y * scale),
                                  qMax(1, qRound(width * scale)), qMax(1, qRound(height * scale)));
            };
            // Measure the final font on this window, without scaling its width again.
            const QFontMetricsF numberMetrics(number_->font(), number_);
            const qreal numberAdvance = numberMetrics.horizontalAdvance(number_->text());
            const int numberX = qRound((expired_ ? 230 : 350) * scale);
            const int suffixX = qRound(numberX + numberAdvance + 10 * scale);
            const auto placeText = [scale](QLabel* label, int x, int y, int height) {
                const QFontMetricsF metrics(label->font(), label);
                const QRectF ink = metrics.tightBoundingRect(label->text());
                const qreal advance = metrics.horizontalAdvance(label->text());
                const int left = qMax(0, qCeil(-ink.left()));
                const int right = qMax(0, qCeil(ink.right() - advance));
                label->setContentsMargins(left, 0, right, 0);
                label->setGeometry(x - left, qRound(y * scale),
                    qMax(1, left + qCeil(advance) + right), qMax(1, qRound(height * scale)));
            };
            // Preserve DayTimer's relative anchors in both placement modes.
            place(title_, 10, 70, 331, 41);
            place(prefix_, 220, 120, 141, 61);
            placeText(number_, numberX, expired_ ? 100 : 70, 111);
            placeText(tail_, suffixX, 100, 21);
            placeText(unit_, suffixX, 120, 61);
            place(english_, 230, 190, 351, 61);
            place(accent_, 209, 120, 6, 131);
            title_->setText(QFontMetricsF(title_->font(), title_).elidedText(title, Qt::ElideRight, title_->width()));
            QStringList lines = fullEnglish.split(QLatin1Char('\n'));
            const QFontMetricsF englishMetrics(english_->font(), english_);
            for (auto& line : lines)
                line = englishMetrics.elidedText(line, Qt::ElideRight, english_->width());
            english_->setText(lines.join(QLatin1Char('\n')));
            QRectF ink = accent_->geometry();
            for (auto* label : labels) ink = ink.united(labelInkRect(label));
            contentBounds_ = ink.toAlignedRect();
            if (config_.useLegacyPlacement)
                ink = ink.united(QRectF(0, 0, qMax(1, qRound(721 * scale)), qMax(1, qRound(330 * scale))));
            bounds = ink.toAlignedRect();
            if (bounds.width() <= available.width() && bounds.height() <= available.height()) break;
            // Re-measure after fitting: font hinting and integer anchors do not scale exactly.
            scale *= qMin(0.95, qMin(available.width() / qreal(bounds.width()),
                                    available.height() / qreal(bounds.height())));
        } while (true);
        for (auto* label : labels) label->move(label->pos() - bounds.topLeft());
        accent_->move(accent_->pos() - bounds.topLeft());
        contentBounds_.translate(-bounds.topLeft());
        resize(bounds.size());
        if (!config_.useLegacyPlacement) trimTransparentRight(this);
        title_->setToolTip(title);
        english_->setToolTip(fullEnglish);
        displayedEnglish_ = english_->text();
    }
    english_->setText(displayedEnglish_);
    // DayTimer positions its whole canvas against the full screen, retaining
    // its intentional outer whitespace. Only visible content is kept clear of taskbars.
    if (config_.useLegacyPlacement) {
        const int x = screenGeometry.right() + 1 - width();
        const int y = screenGeometry.top() + qRound((screenGeometry.height() - height()) * 0.95);
        move(qBound(available.left() - contentBounds_.left(), x,
                    available.right() - contentBounds_.right()),
             qBound(available.top() - contentBounds_.top(), y,
                    available.bottom() - contentBounds_.bottom()));
    } else {
        // Include the screen origin: secondary screens may have negative coordinates.
        move(qMax(available.left(), available.right() + 1 - width() - config_.rightMargin),
             qMax(available.top(), available.bottom() + 1 - height() - config_.bottomMargin));
    }
}

bool DesktopCountdown::event(QEvent* event)
{
    const bool result = QWidget::event(event);
    if (event->type() == QEvent::ScreenChangeInternal || event->type() == QEvent::DevicePixelRatioChange) {
        QTimer::singleShot(0, this, [this] { layoutKey_.clear(); refresh(); });
    }
    return result;
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
