#include "CourseView.h"

#include "../../../Core/ConfigManager/ConfigManager.h"
#include "../../../Core/Logger/Logger.h"
#include "../../../Core/ThemeManager/ThemeManager.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QStackedLayout>

#include <utility>

namespace {
QString timeText(qint64 seconds)
{
    seconds = qMax<qint64>(0, seconds);
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg(seconds / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

// Presentation only: the service owns all course and time calculations.
class CourseBlock final : public QWidget
{
public:
    explicit CourseBlock(QWidget* parent) : QWidget(parent)
    {
        name_ = new QLabel(this);
        name_->setObjectName(QStringLiteral("courseName"));
        time_ = new QLabel(this);
        time_->setObjectName(QStringLiteral("remainingTime"));
        for (QLabel* label : {name_, time_}) {
            label->setTextFormat(Qt::PlainText);
            label->setStyleSheet(QStringLiteral("color: black; background: transparent;"));
            label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        }
        time_->hide();

        underline_ = new QWidget(this);
        underline_->setObjectName(QStringLiteral("activeUnderline"));
        setPresentation({}, false);
    }

    void setPresentation(const QString& name, bool active)
    {
        name_->setText(name);
        active_ = active;
        time_->setVisible(active);
        refreshThemeColor();
        setProperty("active", active);
        updateWidth();
    }

    void refreshThemeColor()
    {
        underline_->setStyleSheet(active_
            ? QStringLiteral("background: %1;").arg(ThemeManager::instance().themeColor().name(QColor::HexRgb))
            : QStringLiteral("background: transparent;"));
    }

    void setRemaining(qint64 seconds)
    {
        const QString text = timeText(seconds);
        if (time_->text() != text) time_->setText(text);
        updateTimeWidth();
        updateWidth();
    }

    void setTrailing(bool trailing)
    {
        trailing_ = trailing;
        updateWidth();
    }

    void setMetrics(int height)
    {
        height_ = height;
        setFixedHeight(height);
        QFont nameFont(QStringLiteral("Microsoft YaHei UI"));
        nameFont.setPixelSize(qMax(1, qRound(height * 0.4)));
        name_->setFont(nameFont);
        QFont timeFont = nameFont;
        timeFont.setPixelSize(qMax(1, qRound(nameFont.pixelSize() * 0.7)));
        time_->setFont(timeFont);
        padding_ = qMax(1, qRound(height * 0.12));
        underline_->setFixedHeight(qMax(2, qRound(height * 4.0 / 49.0)));
        updateTimeWidth();
        updateWidth();
    }

private:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        layoutText();
    }

    void layoutText()
    {
        const QFontMetrics nameMetrics(name_->font());
        const QFontMetrics timeMetrics(time_->font());
        const int ascent = qMax(nameMetrics.ascent(), active_ ? timeMetrics.ascent() : 0);
        const int descent = qMax(nameMetrics.descent(), active_ ? timeMetrics.descent() : 0);
        const int baseline = (height() - underline_->height() - ascent - descent) / 2 + ascent;
        const int nameWidth = nameMetrics.horizontalAdvance(name_->text()) + 2;
        name_->setGeometry(padding_, baseline - nameMetrics.ascent(), nameWidth, nameMetrics.height());
        time_->setGeometry(padding_ + nameWidth + padding_, baseline - timeMetrics.ascent(),
                           time_->width(), timeMetrics.height());
        underline_->setGeometry(0, height() - underline_->height(), width(), underline_->height());
    }

    void updateTimeWidth()
    {
        // Reserve the widest digits so each ticking second keeps the same width.
        const QFontMetrics metrics(time_->font());
        int digitWidth = 0;
        for (char digit = '0'; digit <= '9'; ++digit)
            digitWidth = qMax(digitWidth, metrics.horizontalAdvance(QChar::fromLatin1(digit)));
        int width = 2;
        for (const QChar character : time_->text())
            width += character.isDigit() ? digitWidth : metrics.horizontalAdvance(character);
        time_->setFixedWidth(width);
    }

    void updateWidth()
    {
        int width = QFontMetrics(name_->font()).horizontalAdvance(name_->text()) +
                    padding_ + (trailing_ ? 0 : padding_) + 2;
        if (active_) width += padding_ + time_->width();
        // The bar already supplies its 20px right margin. Do not add an empty
        // square cell or internal right padding after the final course label.
        setFixedWidth(trailing_ ? width : qMax(height_, width));
        layoutText();
    }

    QLabel* name_;
    QLabel* time_;
    QWidget* underline_;
    int padding_ = 6;
    int height_ = 49;
    bool active_ = false;
    bool trailing_ = false;
};

CourseView::CourseView(QWidget* parent) : CourseView(CourseRefreshService::Clock{}, parent)
{
}

CourseView::CourseView(CourseRefreshService::Clock clock, QWidget* parent)
    : ICourseBarComponent(parent)
{
    setObjectName(QStringLiteral("courseView"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    componentHeight_ = qMax(1, ConfigManager::instance().config().courseBarConfig.height);
    pages_ = new QStackedLayout(this);
    pages_->setContentsMargins(0, 0, 0, 0);
    pages_->setSizeConstraint(QLayout::SetNoConstraint);

    content_ = new QWidget(this);
    content_->setObjectName(QStringLiteral("courseList"));
    courseLayout_ = new QHBoxLayout(content_);
    courseLayout_->setContentsMargins(0, 0, 0, 0);
    courseLayout_->setSizeConstraint(QLayout::SetNoConstraint);
    content_->setAutoFillBackground(false);
    pages_->addWidget(content_);

    status_ = new QLabel(tr("正在加载课表…"), this);
    status_->setObjectName(QStringLiteral("courseStatus"));
    status_->setAlignment(Qt::AlignCenter);
    status_->setTextFormat(Qt::PlainText);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    status_->setStyleSheet(QStringLiteral("color: black; background: transparent;"));
    pages_->addWidget(status_);
    pages_->setCurrentWidget(status_);
    breakBlock_ = new CourseBlock(content_);
    breakBlock_->setObjectName(QStringLiteral("breakBlock"));
    breakBlock_->hide();
    compactContent_ = new QWidget(this);
    compactContent_->setObjectName(QStringLiteral("compactCourse"));
    compactBlock_ = new CourseBlock(compactContent_);
    compactBlock_->setTrailing(true);
    pages_->addWidget(compactContent_);
    updateMetrics(componentHeight_);
    connect(&ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &CourseView::refreshThemeColor);

    service_ = new CourseRefreshService(std::move(clock), this);
    connect(service_, &CourseRefreshService::tableChanged, this, &CourseView::applyTable);
    connect(service_, &CourseRefreshService::stateChanged, this, &CourseView::applyState);
    connect(service_, &CourseRefreshService::errorOccurred, this, &CourseView::logError);
    service_->start();
}

QSize CourseView::sizeHint() const
{
    return QSize(preferredWidth_, componentHeight_);
}

void CourseView::refreshThemeColor()
{
    for (CourseBlock* block : blocks_) block->refreshThemeColor();
    breakBlock_->refreshThemeColor();
    compactBlock_->refreshThemeColor();
}

QSize CourseView::minimumSizeHint() const
{
    return QSize(1, 0);
}

void CourseView::setCompactMode(bool compact)
{
    if (compactMode_ == compact) return;
    compactMode_ = compact;
    updateDisplayPage();
    updateContentSize();
}

bool CourseView::hasCompactContent() const noexcept
{
    return hasVisualState_ && !showingStatus_ && activeBlock_;
}

QSize CourseView::expandedSizeHint() const
{
    return QSize(expandedWidth_, componentHeight_);
}

QSize CourseView::compactSizeHint() const
{
    return QSize(compactWidth_, componentHeight_);
}

void CourseView::updateDisplayPage()
{
    pages_->setCurrentWidget(compactMode_ ? compactContent_ :
        (showingStatus_ ? static_cast<QWidget*>(status_) : content_));
}

QString CourseView::shortName(const QString& subject) const
{
    for (const Subject& item : table_.subjects) {
        if (item.name == subject && !item.simplifiedName.trimmed().isEmpty())
            return item.simplifiedName.trimmed().left(1);
    }
    return subject.trimmed().left(1);
}

void CourseView::applyTable(const CourseRefreshService::TableSnapshot& table)
{
    activeBlock_ = nullptr;
    hasVisualState_ = false;
    courseLayout_->removeWidget(breakBlock_);
    breakBlock_->hide();
    qDeleteAll(blocks_);
    blocks_.clear();
    table_ = table;
    if (table.valid) {
        for (qsizetype i = 0; i < table.classes.size(); ++i) {
            auto* block = new CourseBlock(content_);
            block->setObjectName(QStringLiteral("courseBlock_%1").arg(i));
            block->setTrailing(i + 1 == table.classes.size());
            block->setMetrics(componentHeight_);
            block->setPresentation(shortName(table.classes[i].subject), false);
            courseLayout_->addWidget(block);
            block->show();
            blocks_.append(block);
        }
    }
    showingStatus_ = !table.valid || table.classes.isEmpty();
    status_->setText(table.valid ? tr("今天没有课程") : tr("课表不可用"));
    updateDisplayPage();
    updateContentSize();
}

void CourseView::applyState(const CourseRefreshService::State& state)
{
    if (state.tableRevision != table_.revision) return;
    using Phase = CourseRefreshService::Phase;
    const bool transition = !hasVisualState_ || state.phase != visualState_.phase ||
                            state.currentClassIndex != visualState_.currentClassIndex ||
                            state.nextClassIndex != visualState_.nextClassIndex;
    if (transition) {
        activeBlock_ = nullptr;
        courseLayout_->removeWidget(breakBlock_);
        breakBlock_->hide();
        for (qsizetype i = 0; i < blocks_.size(); ++i) {
            const bool current = state.phase == Phase::InClass && state.currentClassIndex == i;
            const bool first = state.phase == Phase::BeforeFirstClass && state.nextClassIndex == i;
            blocks_[i]->setPresentation(current ? table_.classes[i].subject :
                                       shortName(table_.classes[i].subject), current || first);
            if (current || first) activeBlock_ = blocks_[i];
        }
        showingStatus_ = state.phase == Phase::NoClasses || state.phase == Phase::Finished ||
                         state.phase == Phase::InvalidTable;
        switch (state.phase) {
        case Phase::NoClasses: status_->setText(tr("今天没有课程")); break;
        case Phase::Finished: status_->setText(tr("今日课程已结束")); break;
        case Phase::InvalidTable: status_->setText(tr("课表不可用")); break;
        case Phase::Break:
            if (state.nextClassIndex > 0 && state.nextClassIndex < blocks_.size()) {
                breakBlock_->setPresentation(tr("课间"), true);
                courseLayout_->insertWidget(state.nextClassIndex, breakBlock_);
                breakBlock_->show();
                activeBlock_ = breakBlock_;
            }
            break;
        default: break;
        }
        if (!showingStatus_ && !activeBlock_) {
            showingStatus_ = true;
            status_->setText(tr("课表不可用"));
            logError(QStringLiteral("CourseView received an invalid course index"));
        }
    }
    visualState_ = state;
    hasVisualState_ = true;
    if (activeBlock_) activeBlock_->setRemaining(state.remainingSeconds);
    if (hasCompactContent()) {
        if (transition) {
            const int index = state.phase == Phase::BeforeFirstClass
                ? state.nextClassIndex : state.currentClassIndex;
            compactBlock_->setPresentation(state.phase == Phase::Break
                ? tr("课间") : table_.classes[index].subject, true);
        }
        compactBlock_->setRemaining(state.remainingSeconds);
    }
    updateDisplayPage();
    updateContentSize();
}

void CourseView::logError(const QString& message)
{
    Logger::instance().log(Logger::Level::Error, QStringLiteral("CourseView: %1").arg(message));
    Logger::instance().flush();
}

void CourseView::updateMetrics(int height)
{
    componentHeight_ = qMax(1, height);
    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPixelSize(qMax(1, qRound(componentHeight_ * 0.4)));
    status_->setFont(font);
    courseLayout_->setSpacing(qMax(1, qRound(componentHeight_ * 0.08)));
    for (CourseBlock* block : blocks_) block->setMetrics(componentHeight_);
    breakBlock_->setMetrics(componentHeight_);
    compactBlock_->setMetrics(componentHeight_);
    updateContentSize();
}

void CourseView::updateContentSize()
{
    int count = 0;
    int width = 0;
    for (CourseBlock* block : blocks_) {
        width += block->width();
        ++count;
    }
    if (!breakBlock_->isHidden()) {
        width += breakBlock_->width();
        ++count;
    }
    width += qMax(0, count - 1) * courseLayout_->spacing();
    content_->setFixedSize(qMax(1, width), componentHeight_);
    courseLayout_->activate();
    const int expanded = showingStatus_
        ? QFontMetrics(status_->font()).horizontalAdvance(status_->text()) + componentHeight_ / 2
        : qMax(1, width);
    const int compact = hasCompactContent() ? compactBlock_->width() : 0;
    compactBlock_->setVisible(hasCompactContent());
    compactContent_->setFixedSize(qMax(1, compact), componentHeight_);
    const int preferred = compactMode_ ? compact : expanded;
    if (preferredWidth_ != preferred || expandedWidth_ != expanded || compactWidth_ != compact) {
        preferredWidth_ = preferred;
        expandedWidth_ = expanded;
        compactWidth_ = compact;
        updateGeometry();
        emit preferredSizeChanged();
    }
}

void CourseView::resizeEvent(QResizeEvent* event)
{
    ICourseBarComponent::resizeEvent(event);
    if (height() > 0 && componentHeight_ != height()) updateMetrics(height());
}
