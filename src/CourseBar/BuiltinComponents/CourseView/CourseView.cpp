#include "CourseView.h"
#include "../../CourseBarLabel.h"

#include "../../../Core/Logger/Logger.h"
#include "../../../Core/ThemeManager/ThemeManager.h"

#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QResizeEvent>
#include <QStackedLayout>

#include <utility>

// Keep QProgressBar's value/range semantics, with the remaining span centered.
class CenteredProgressBar final : public QProgressBar
{
public:
    explicit CenteredProgressBar(QWidget* parent) : QProgressBar(parent)
    {
        // The translucent track needs the parent's background painted first.
        setAttribute(Qt::WA_OpaquePaintEvent, false);
        setAutoFillBackground(false);
    }

    void setThemeColor(const QColor& color)
    {
        color_ = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        QColor track = color_;
        track.setAlpha(115);
        painter.fillRect(rect(), track);
        if (maximum() <= minimum() || value() <= minimum()) return;
        const double remaining = qBound(0.0,
            (double(value()) - minimum()) / (double(maximum()) - minimum()), 1.0);
        const int span = qRound(width() * remaining);
        painter.fillRect(QRect((width() - span) / 2, 0, span, height()), color_);
    }

private:
    QColor color_;
};

// Presentation only: the service owns all course and time calculations.
class CourseBlock final : public QWidget
{
public:
    explicit CourseBlock(QWidget* parent) : QWidget(parent)
    {
        name_ = new CourseBarLabel(this);
        name_->setObjectName(QStringLiteral("courseName"));
        name_->setTextFormat(Qt::PlainText);
        name_->setAlignment(Qt::AlignCenter);
        name_->setStyleSheet(QStringLiteral("color: black; background: transparent;"));
        name_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        progress_ = new CenteredProgressBar(this);
        progress_->setObjectName(QStringLiteral("remainingProgress"));
        progress_->setRange(0, 10000);
        progress_->setValue(10000);
        progress_->setTextVisible(false);
        setPresentation({}, false);
    }

    void setPresentation(const QString& name, bool active)
    {
        name_->setText(name);
        progress_->setVisible(active);
        setProperty("active", active);
        refreshThemeColor();
        updateWidth();
    }

    void refreshThemeColor()
    {
        progress_->setThemeColor(ThemeManager::instance().themeColor());
        update();
    }

    void setProgress(const CourseRefreshService::State& state)
    {
        const int value = state.phase == CourseRefreshService::Phase::BeforeFirstClass ? 10000 :
            (state.durationSeconds > 0 ? qRound(qBound(0.0,
                double(state.remainingSeconds) / double(state.durationSeconds), 1.0) * 10000) : 0);
        progress_->setValue(value);
    }

    void setMetrics(const CourseBarMetrics& metrics)
    {
        metrics_ = metrics;
        setFixedHeight(metrics.height);
        name_->setFont(metrics.font());
        progress_->setFixedHeight(metrics.progressHeight());
        updateWidth();
    }

    int textWidth() const
    {
        return metrics_.textWidth(name_->text(), name_->font());
    }

    void setHorizontalExtension(int left, int right)
    {
        leftExtension_ = left;
        rightExtension_ = right;
        updateWidth();
    }

private:
    void paintEvent(QPaintEvent*) override
    {
        if (!property("active").toBool()) return;
        const QColor background(200, 200, 200, 77);
        QPainter painter(this);
        painter.fillRect(rect(), background);
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        layoutText();
    }

    void layoutText()
    {
        // Keep the text in place while the decoration extends into existing gaps.
        name_->setGeometry(leftExtension_, 0, textWidth(), height());
        progress_->setGeometry(0, height() - progress_->height(), width(), progress_->height());
    }

    void updateWidth()
    {
        const int margin = metrics_.textGuard();
        name_->setContentsMargins(margin, 0, margin, 0);
        setFixedWidth(textWidth() + leftExtension_ + rightExtension_);
        layoutText();
    }

    QLabel* name_;
    CenteredProgressBar* progress_;
    CourseBarMetrics metrics_;
    int leftExtension_ = 0;
    int rightExtension_ = 0;
};

CourseView::CourseView(QWidget* parent) : CourseView(CourseRefreshService::Clock{}, parent)
{
}

CourseView::CourseView(CourseRefreshService::Clock clock, QWidget* parent)
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

    service_ = new CourseRefreshService(std::move(clock), this);
    connect(service_, &CourseRefreshService::tableChanged, this, &CourseView::applyTable);
    connect(service_, &CourseRefreshService::stateChanged, this, &CourseView::applyState);
    connect(service_, &CourseRefreshService::errorOccurred, this, &CourseView::logError);
    service_->start();
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

    const int spacing = metrics().courseSpacing();
    const int activeSpacing = qRound(spacing * 1.2);
    // Borrow 60% of each adjacent gap, reserving space at exposed active edges.
    const int extension = qRound(activeSpacing * 0.6);
    int width = !visibleBlocks.isEmpty() && visibleBlocks.first() == activeBlock_ ? extension : 0;
    for (qsizetype i = 0; i < visibleBlocks.size(); ++i) {
        CourseBlock* block = visibleBlocks[i];
        const bool active = block == activeBlock_;
        const int left = active ? extension : 0;
        const int right = left;
        block->setHorizontalExtension(left, right);
        block->move(width - left, 0);
        width += block->textWidth();
        if (i + 1 < visibleBlocks.size())
            width += active || visibleBlocks[i + 1] == activeBlock_ ? activeSpacing : spacing;
    }
    if (!visibleBlocks.isEmpty() && visibleBlocks.last() == activeBlock_)
        width += extension;
    content_->setFixedSize(qMax(1, width), metrics().height);
    const int preferred = showingStatus_
        ? metrics().textWidth(status_->text(), status_->font())
        : qMax(1, width);
    if (preferredWidth_ != preferred) {
        preferredWidth_ = preferred;
        notifyPreferredSizeChanged();
    }
}
