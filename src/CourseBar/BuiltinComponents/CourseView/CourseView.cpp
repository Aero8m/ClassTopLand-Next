#include "CourseView.h"
#include "../../CourseBarLabel.h"
#include "../../CourseBarItem.h"

#include "../../../Core/Logger/Logger.h"
#include "../../../Core/ThemeManager/ThemeManager.h"

#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QResizeEvent>
#include <QStackedLayout>

#include <utility>

// The service owns time calculations; presentation is shared with plugin items.
class CourseBlock final : public CourseBarItem
{
public:
    explicit CourseBlock(QWidget* parent) : CourseBarItem(parent) {}
    void setProgress(const CourseRefreshService::State& state)
    {
        const int value = state.phase == CourseRefreshService::Phase::BeforeFirstClass ? 10000 :
            (state.durationSeconds > 0 ? qRound(qBound(0.0,
                double(state.remainingSeconds) / double(state.durationSeconds), 1.0) * 10000) : 0);
        setProgressValue(value);
    }
};

CourseView::CourseView(QWidget* parent) : CourseView(CourseRefreshService::Clock{}, parent)
{
}

CourseView::CourseView(CourseRefreshService::Clock clock, QWidget* parent) : CourseView(std::move(clock), nullptr, parent) {}
CourseView::CourseView(CourseRefreshService* service, QWidget* parent) : CourseView(CourseRefreshService::Clock{}, service, parent) {}
CourseView::CourseView(CourseRefreshService::Clock clock, CourseRefreshService* service, QWidget* parent)
    : ICourseBarComponent(parent)
{
    setObjectName(QStringLiteral("courseView"));
    pages_ = new QStackedLayout(this);
    pages_->setContentsMargins(0, 0, 0, 0);
    pages_->setSizeConstraint(QLayout::SetNoConstraint);

    content_ = new QWidget(this);
    content_->setObjectName(QStringLiteral("courseList"));
    content_->setAutoFillBackground(false);
    pages_->addWidget(content_);

    status_ = new CourseBarLabel(this);
    status_->setText(tr("正在加载课表…"));
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
    updateMetrics();
    connect(&ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &CourseView::refreshThemeColor);

    service_ = service ? service : new CourseRefreshService(std::move(clock), this);
    connect(service_, &CourseRefreshService::tableChanged, this, &CourseView::applyTable);
    connect(service_, &CourseRefreshService::stateChanged, this, &CourseView::applyState);
    connect(service_, &CourseRefreshService::errorOccurred, this, &CourseView::logError);
    if (service_->isRunning()) { applyTable(service_->table()); applyState(service_->state()); }
    else service_->start();
}

QSize CourseView::sizeHint() const
{
    return QSize(preferredWidth_, metrics().height);
}

void CourseView::refreshThemeColor()
{
    for (CourseBlock* block : blocks_) block->refreshThemeColor();
    breakBlock_->refreshThemeColor();
}

QSize CourseView::minimumSizeHint() const
{
    return QSize(1, 0);
}

void CourseView::updateDisplayPage()
{
    pages_->setCurrentWidget(showingStatus_ ? static_cast<QWidget*>(status_) : content_);
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
    breakBlock_->hide();
    qDeleteAll(blocks_);
    blocks_.clear();
    table_ = table;
    if (table.valid) {
        for (qsizetype i = 0; i < table.classes.size(); ++i) {
            auto* block = new CourseBlock(content_);
            block->setObjectName(QStringLiteral("courseBlock_%1").arg(i));
            block->setMetrics(metrics());
            block->setPresentation(shortName(table.classes[i].subject), false);
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
    if (activeBlock_) activeBlock_->setProgress(state);
    updateDisplayPage();
    updateContentSize();
}

void CourseView::logError(const QString& message)
{
    Logger::instance().log(Logger::Level::Error, QStringLiteral("CourseView: %1").arg(message));
    Logger::instance().flush();
}

void CourseView::updateMetrics()
{
    status_->setFont(metrics().font());
    status_->setContentsMargins(metrics().textGuard(), 0, metrics().textGuard(), 0);
    for (CourseBlock* block : blocks_) block->setMetrics(metrics());
    breakBlock_->setMetrics(metrics());
    updateContentSize();
}

void CourseView::updateContentSize()
{
    QList<CourseBlock*> visibleBlocks = blocks_;
    if (!breakBlock_->isHidden())
        visibleBlocks.insert(visualState_.nextClassIndex, breakBlock_);

    QList<CourseBarItem*> items;
    for (auto* block : visibleBlocks) items.append(block);
    const int width = layoutCourseBarItems(items, metrics());
    content_->setFixedSize(qMax(1, width), metrics().height);
    const int preferred = showingStatus_
        ? metrics().textWidth(status_->text(), status_->font())
        : qMax(1, width);
    if (preferredWidth_ != preferred) {
        preferredWidth_ = preferred;
        notifyPreferredSizeChanged();
    }
}
