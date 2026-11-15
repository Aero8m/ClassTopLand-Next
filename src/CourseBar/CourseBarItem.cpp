#include "CourseBarItem.h"
#include "CourseBarLabel.h"
#include "../Core/ThemeManager/ThemeManager.h"
#include <QPainter>
#include <QProgressBar>
#include <QResizeEvent>

namespace
{
class CenteredProgressBar final : public QProgressBar
{
  public:
    explicit CenteredProgressBar(QWidget *parent) : QProgressBar(parent)
    {
        setAttribute(Qt::WA_OpaquePaintEvent, false);
        setAutoFillBackground(false);
    }
    void setThemeColor(const QColor &color) { color_ = color; update(); }
  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QColor track = color_;
        track.setAlpha(115);
        painter.fillRect(rect(), track);
        if (maximum() <= minimum() || value() <= minimum()) return;
        const double remaining = qBound(0.0, (double(value()) - minimum()) /
                                                   (double(maximum()) - minimum()), 1.0);
        const int span = qRound(width() * remaining);
        painter.fillRect(QRect((width() - span) / 2, 0, span, height()), color_);
    }
  private:
    QColor color_;
};
}

CourseBarItem::CourseBarItem(QWidget *parent) : QWidget(parent)
{
    label_ = new CourseBarLabel(this);
    label_->setAlignment(Qt::AlignCenter);
    label_->setStyleSheet(QStringLiteral("color: black; background: transparent;"));
    label_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    progress_ = new CenteredProgressBar(this);
    progress_->setRange(0, 10000);
    progress_->setValue(10000);
    progress_->setTextVisible(false);
    progress_->hide();
    setMetrics(metrics_);
    refreshThemeColor();
    connect(&ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &CourseBarItem::refreshThemeColor);
}
QString CourseBarItem::text() const { return label_->text(); }
QFont CourseBarItem::textFont() const { return label_->font(); }
void CourseBarItem::setText(const QString &text)
{
    if (text == label_->text()) return;
    label_->setText(text);
    updateWidth();
    emit preferredSizeChanged();
}
void CourseBarItem::setActive(bool active)
{
    if (active_ == active) return;
    active_ = active;
    progress_->setVisible(active);
    update();
    updateGeometry();
    if (parentWidget() && parentWidget()->layout()) parentWidget()->layout()->invalidate();
    emit preferredSizeChanged();
}
int CourseBarItem::progressValue() const { return progress_->value(); }
void CourseBarItem::setProgressValue(int value) { progress_->setValue(qBound(0, value, 10000)); }
void CourseBarItem::setLeftMargin(int value)
{
    value = qBound(-1, value, CourseBarConfig::MaximumSpacing);
    if (leftMargin_ == value) return;
    leftMargin_ = value;
    updateGeometry();
    emit preferredSizeChanged();
}
void CourseBarItem::setRightMargin(int value)
{
    value = qBound(-1, value, CourseBarConfig::MaximumSpacing);
    if (rightMargin_ == value) return;
    rightMargin_ = value;
    updateGeometry();
    emit preferredSizeChanged();
}
void CourseBarItem::setPresentation(const QString &text, bool active)
{
    setText(text);
    setActive(active);
}
void CourseBarItem::refreshThemeColor()
{
    static_cast<CenteredProgressBar *>(progress_)->setThemeColor(ThemeManager::instance().themeColor());
    update();
}
void CourseBarItem::setMetrics(const CourseBarMetrics &metrics)
{
    metrics_ = metrics;
    setFixedHeight(metrics.height);
    label_->setFont(metrics.font());
    progress_->setFixedHeight(metrics.progressHeight());
    updateWidth();
}
int CourseBarItem::textWidth() const { return metrics_.textWidth(text(), label_->font()); }
QSize CourseBarItem::sizeHint() const { return {textWidth() + left_ + right_, metrics_.height}; }
void CourseBarItem::setHorizontalExtension(int left, int right)
{
    if (left_ == left && right_ == right) return;
    left_ = left;
    right_ = right;
    updateWidth();
}
void CourseBarItem::updateWidth()
{
    const int guard = metrics_.textGuard();
    label_->setContentsMargins(guard, 0, guard, 0);
    setFixedWidth(textWidth() + left_ + right_);
    layoutText();
    updateGeometry();
}
void CourseBarItem::layoutText()
{
    label_->setGeometry(left_, 0, textWidth(), height());
    progress_->setGeometry(0, height() - progress_->height(), width(), progress_->height());
}
void CourseBarItem::resizeEvent(QResizeEvent *) { layoutText(); }
void CourseBarItem::paintEvent(QPaintEvent *)
{
    if (!active_) return;
    QPainter painter(this);
    painter.fillRect(rect(), QColor(200, 200, 200, 77));
}
namespace
{
int arrangeItems(const QList<CourseBarItem *> &items, const CourseBarMetrics &metrics,
                 const QPoint &origin, bool position)
{
    const int spacing = metrics.courseSpacing();
    const int activeSpacing = qRound(spacing * 1.2);
    const int extension = qRound(activeSpacing * 0.6);
    QList<int> leftMargins, rightMargins;
    auto margin = [&metrics](int value, int inherited) {
        return value < 0 ? inherited : value == 0 ? 0 : metrics.scaled(value);
    };
    for (qsizetype i = 0; i < items.size(); ++i)
    {
        const int before = i > 0 ? (items[i - 1]->active() || items[i]->active() ? activeSpacing : spacing) : 0;
        const int after = i + 1 < items.size() ? (items[i]->active() || items[i + 1]->active() ? activeSpacing : spacing) : 0;
        leftMargins.append(margin(items[i]->leftMargin(), before / 2));
        rightMargins.append(margin(items[i]->rightMargin(), after - after / 2));
    }
    int width = !items.isEmpty() && items.first()->active() ? extension : 0;
    for (qsizetype i = 0; i < items.size(); ++i)
    {
        auto *item = items[i];
        width += leftMargins[i];
        // Tight margins must not let an active highlight cover a neighbouring item's text.
        const int leftEdge = !item->active() ? 0 : i == 0 ? extension : qMin(extension, rightMargins[i - 1] + leftMargins[i]);
        const int rightEdge = !item->active() ? 0 : i + 1 == items.size() ? extension : qMin(extension, rightMargins[i] + leftMargins[i + 1]);
        if (position)
        {
            item->setHorizontalExtension(leftEdge, rightEdge);
            item->move(origin + QPoint(width - leftEdge, 0));
        }
        width += item->textWidth() + rightMargins[i];
    }
    if (!items.isEmpty() && items.last()->active()) width += extension;
    return width;
}
}
int layoutCourseBarItems(const QList<CourseBarItem *> &items, const CourseBarMetrics &metrics,
                         const QPoint &origin)
{
    return arrangeItems(items, metrics, origin, true);
}
CourseBarRowLayout::CourseBarRowLayout(QWidget *parent) : QLayout(parent)
{
    setContentsMargins(0, 0, 0, 0);
    setSizeConstraint(QLayout::SetNoConstraint);
}
CourseBarRowLayout::~CourseBarRowLayout()
{
    while (!items_.isEmpty()) delete takeAt(0);
}
void CourseBarRowLayout::addItem(QLayoutItem *item) { items_.append(item); invalidate(); }
QLayoutItem *CourseBarRowLayout::takeAt(int index)
{
    if (index < 0 || index >= items_.size()) return nullptr;
    auto *item = items_.takeAt(index);
    invalidate();
    return item;
}
void CourseBarRowLayout::insertWidget(int index, CourseBarItem *item)
{
    addChildWidget(item);
    items_.insert(index < 0 ? items_.size() : qMin(index, int(items_.size())), new QWidgetItem(item));
    invalidate();
}
QList<CourseBarItem *> CourseBarRowLayout::visibleItems() const
{
    QList<CourseBarItem *> result;
    for (auto *entry : items_)
        if (auto *item = qobject_cast<CourseBarItem *>(entry->widget()); item && !item->isHidden())
            result.append(item);
    return result;
}
QSize CourseBarRowLayout::sizeHint() const
{
    return {arrangeItems(visibleItems(), metrics_, {}, false), metrics_.height};
}
void CourseBarRowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    layoutCourseBarItems(visibleItems(), metrics_, rect.topLeft());
}
void CourseBarRowLayout::setMetrics(const CourseBarMetrics &metrics)
{
    metrics_ = metrics;
    for (auto *entry : items_)
        if (auto *item = qobject_cast<CourseBarItem *>(entry->widget())) item->setMetrics(metrics);
    invalidate();
}
