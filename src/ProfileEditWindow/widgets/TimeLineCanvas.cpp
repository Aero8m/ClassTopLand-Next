#include "TimeLineCanvas.h"
#include "../ProfileEditSession.h"
#include <ElaTheme.h>
#include <ElaToolTip.h>
#include <QHideEvent>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {
constexpr int top = 20;
constexpr int ruler = 66;
int seconds(QTime time) { return QTime(0, 0).secsTo(time); }
QTime timeOfDay(int value) { return value >= 0 && value < 86400 ? QTime(0, 0).addSecs(value) : QTime(); }
QString clockText(int value)
{
    if (value == 86400) return QStringLiteral("24:00:00");
    const QString sign = value < 0 ? QStringLiteral("−") : QString();
    value = std::abs(value);
    return sign + QStringLiteral("%1:%2:%3").arg(value / 3600, 2, 10, QLatin1Char('0'))
        .arg(value / 60 % 60, 2, 10, QLatin1Char('0')).arg(value % 60, 2, 10, QLatin1Char('0'));
}
QString pointText(int start, int end)
{
    return clockText(start) + QStringLiteral(" – ") + clockText(end) + QLatin1Char('\n') +
        TimeLineCanvas::tr("时长 %1").arg(clockText(end - start));
}
}

TimeLineCanvas::TimeLineCanvas(QWidget* parent) : ElaScrollPageArea(parent)
{
    setObjectName(QStringLiteral("timeLineCanvas"));
    setMinimumWidth(260);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("时间轴"));
    setBorderRadius(8);
    setZoom(100);
    // No automatic parent event filter: only time blocks trigger the tooltip.
    tip_ = new ElaToolTip;
    tip_->setParent(this, tip_->windowFlags());
    tip_->setAttribute(Qt::WA_ShowWithoutActivating);
    tip_->setDisplayMsec(-1);
    hoverTimer_ = new QTimer(this);
    hoverTimer_->setSingleShot(true);
    hoverTimer_->setInterval(450);
    connect(hoverTimer_, &QTimer::timeout, this, [this] {
        if (hovered_ >= 0 && !isDragging() && isVisible()) {
            tip_->move(QCursor::pos() + QPoint(12, 16)); tip_->show();
        }
    });
    connect(eTheme, &ElaTheme::themeModeChanged, this, qOverload<>(&TimeLineCanvas::update));
}
void TimeLineCanvas::setTimePoints(const QList<TimeLinePoint>& points)
{
    cancelDrag();
    points_ = points;
    if (selected_ >= points_.size()) selected_ = -1;
    hovered_ = -1; hoverTimer_->stop(); tip_->hide(); update();
}
void TimeLineCanvas::setSelectedIndex(int index)
{
    selected_ = index >= 0 && index < points_.size() ? index : -1;
    update();
}
void TimeLineCanvas::setZoom(int percent)
{
    cancelDrag();
    zoom_ = qBound(50, percent, 300);
    setFixedHeight(qCeil(1440 * pixelsPerMinute()) + top * 2);
    update();
}
qreal TimeLineCanvas::yForSeconds(int value) const { return top + value / 60.0 * pixelsPerMinute(); }
int TimeLineCanvas::secondsAtY(qreal y) const { return qRound((y - top) / pixelsPerMinute() * 60); }
QRectF TimeLineCanvas::rangeRect(int start, int end) const
{
    return QRectF(ruler + 6, yForSeconds(start), qMax(1, width() - ruler - 22), yForSeconds(end) - yForSeconds(start));
}
QRectF TimeLineCanvas::pointRect(int index) const
{
    if (index < 0 || index >= points_.size()) return {};
    return rangeRect(seconds(points_[index].startTime), seconds(points_[index].endTime));
}
bool TimeLineCanvas::isDragging() const { return drag_ != Drag::None; }
int TimeLineCanvas::hitTest(QPointF position) const
{
    // Exact interiors take precedence over enlarged targets of nearby short periods.
    for (int i = 0; i < points_.size(); ++i) if (pointRect(i).contains(position)) return i;
    int best = -1; qreal distance = 10000;
    for (int i = 0; i < points_.size(); ++i) {
        const auto rect = pointRect(i);
        auto hit = rect;
        if (hit.height() < 14) { hit.setTop(rect.center().y() - 7); hit.setBottom(rect.center().y() + 7); }
        if (hit.contains(position) && qAbs(position.y() - rect.center().y()) < distance) {
            best = i; distance = qAbs(position.y() - rect.center().y());
        }
    }
    return best;
}
QString TimeLineCanvas::previewError() const
{
    if (previewStart_ < 0 || previewEnd_ >= 86400) return tr("时段不能超出当天范围");
    if (previewStart_ >= previewEnd_) return tr("开始时间必须早于结束时间");
    auto candidate = points_;
    candidate[selected_] = TimeLinePoint(timeOfDay(previewStart_), timeOfDay(previewEnd_));
    QString error; ProfileEditSession::validateTimePoints(candidate, &error);
    return error;
}
void TimeLineCanvas::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto theme = eTheme->getThemeMode();
    const auto ink = ElaThemeColor(theme, BasicText);
    const auto border = ElaThemeColor(theme, BasicBorder);
    const auto accent = ElaThemeColor(theme, PrimaryNormal);
    painter.fillRect(event->rect(), ElaThemeColor(theme, PopupBase));
    painter.setPen(border); painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 8, 8);
    QFont labels = font(); labels.setPixelSize(14); painter.setFont(labels);
    const int labelStep = zoom_ < 100 ? 60 : 30;
    const int tickStep = zoom_ < 75 ? 30 : 15;
    const int first = qMax(0, (secondsAtY(event->rect().top()) / 60 / tickStep) * tickStep);
    const int last = qMin(1440, secondsAtY(event->rect().bottom()) / 60 + tickStep);
    for (int minute = first; minute <= last; minute += tickStep) {
        const auto y = yForSeconds(minute * 60);
        auto color = border; color.setAlpha(minute % 30 ? 90 : 190);
        painter.setPen(QPen(color, 1)); painter.drawLine(QPointF(ruler, y), QPointF(width() - 14, y));
        if (minute % labelStep == 0) {
            painter.setPen(ink);
            painter.drawText(QRectF(4, y - 9, ruler - 10, 18), Qt::AlignRight | Qt::AlignVCenter,
                QStringLiteral("%1:%2").arg(minute / 60, 2, 10, QLatin1Char('0')).arg(minute % 60, 2, 10, QLatin1Char('0')));
        }
    }
    QList<int> order;
    for (int i = 0; i < points_.size(); ++i) order.append(i);
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) { return points_[a].startTime < points_[b].startTime; });
    // Selected blocks render last so that the resize handles remain visible.
    if (order.removeOne(selected_)) order.append(selected_);
    for (int index : order) {
        int start = seconds(points_[index].startTime), end = seconds(points_[index].endTime);
        if (!points_[index].startTime.isValid() || !points_[index].endTime.isValid()) continue;
        if (index == selected_ && isDragging()) { start = previewStart_; end = previewEnd_; }
        auto rect = rangeRect(start, end);
        if (rect.height() < 0) rect = rect.normalized();
        if (!rect.adjusted(0, -8, 0, 8).intersects(event->rect())) continue;
        const bool selected = index == selected_;
        const bool invalid = selected && isDragging() && !previewError().isEmpty();
        QColor edge = invalid ? ElaThemeColor(theme, StatusDanger) : selected ? accent : border;
        QColor fill = accent; fill.setAlpha(selected ? 45 : 22);
        if (invalid) { fill = edge; fill.setAlpha(40); }
        painter.setPen(QPen(edge, selected ? 2 : 1)); painter.setBrush(fill);
        painter.drawRoundedRect(rect, qMin(6.0, rect.height() / 2), qMin(6.0, rect.height() / 2));
        if (rect.height() >= 18) {
            painter.save(); painter.setClipRect(rect.adjusted(6, 1, -6, -1)); painter.setPen(ink);
            painter.drawText(rect.adjusted(10, 4, -8, -2), Qt::AlignLeft | Qt::AlignTop,
                rect.height() >= 42 ? pointText(start, end) : clockText(start) + QStringLiteral(" – ") + clockText(end));
            painter.restore();
        }
        if (selected) {
            painter.setBrush(ElaThemeColor(theme, PopupBase)); painter.setPen(QPen(edge, 2));
            painter.drawEllipse(QPointF(rect.center().x(), rect.top()), 5, 5);
            painter.drawEllipse(QPointF(rect.center().x(), rect.bottom()), 5, 5);
        }
    }
}
void TimeLineCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) { ElaScrollPageArea::mousePressEvent(event); return; }
    cancelDrag(); setFocus(); tip_->hide(); hoverTimer_->stop(); hovered_ = -1;
    int hit = hitTest(event->position());
    // Selected handles are accessible even just outside the block's actual bounds.
    if (selected_ >= 0) {
        const auto rect = pointRect(selected_);
        if (qAbs(event->position().x() - rect.center().x()) < 12 &&
            (qAbs(event->position().y() - rect.top()) < 7 || qAbs(event->position().y() - rect.bottom()) < 7)) hit = selected_;
    }
    const bool wasSelected = hit == selected_;
    setSelectedIndex(hit); emit selectionRequested(hit);
    if (hit < 0) return;
    const auto rect = pointRect(hit);
    drag_ = Drag::Move;
    const auto fromTop = qAbs(event->position().y() - rect.top());
    const auto fromBottom = qAbs(event->position().y() - rect.bottom());
    if (wasSelected && qMin(fromTop, fromBottom) <= 6)
        drag_ = fromTop <= fromBottom ? Drag::Start : Drag::End;
    pressY_ = event->position().y(); moved_ = false;
    originalStart_ = previewStart_ = seconds(points_[hit].startTime);
    originalEnd_ = previewEnd_ = seconds(points_[hit].endTime);
    setCursor(drag_ == Drag::Move ? Qt::ClosedHandCursor : Qt::SizeVerCursor);
}
void TimeLineCanvas::updatePreview(qreal y)
{
    const int delta = qRound((y - pressY_) / pixelsPerMinute() * 60 / 300) * 300;
    previewStart_ = originalStart_ + (drag_ == Drag::End ? 0 : delta);
    previewEnd_ = originalEnd_ + (drag_ == Drag::Start ? 0 : delta);
    moved_ = previewStart_ != originalStart_ || previewEnd_ != originalEnd_;
    emit previewChanged(timeOfDay(previewStart_), timeOfDay(previewEnd_)); update();
}
void TimeLineCanvas::updateHover(QPointF position)
{
    const int hit = hitTest(position);
    if (hit != hovered_) {
        hovered_ = hit; tip_->hide(); hoverTimer_->stop();
        if (hit >= 0) {
            tip_->setToolTip(pointText(seconds(points_[hit].startTime), seconds(points_[hit].endTime)));
            tip_->adjustSize();
            hoverTimer_->start();
        }
    }
    if (hit >= 0) tip_->updatePos();
    auto cursor = hit >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor;
    if (selected_ >= 0) {
        const auto rect = pointRect(selected_);
        if (position.x() >= rect.left() && position.x() <= rect.right() &&
            (qAbs(position.y() - rect.top()) <= 6 || qAbs(position.y() - rect.bottom()) <= 6)) cursor = Qt::SizeVerCursor;
    }
    setCursor(cursor);
}
void TimeLineCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (isDragging()) updatePreview(event->position().y()); else updateHover(event->position());
}
void TimeLineCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !isDragging()) return;
    updatePreview(event->position().y());
    const QString error = previewError();
    const int index = selected_, start = previewStart_, end = previewEnd_;
    const bool changed = moved_;
    cancelDrag();
    if (changed) {
        if (!error.isEmpty()) emit invalidChange(error);
        else emit timeChangeRequested(index, timeOfDay(start), timeOfDay(end));
    }
}
void TimeLineCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    cancelDrag();
    if (event->button() == Qt::LeftButton && hitTest(event->position()) < 0) {
        const int value = qBound(0, qRound(secondsAtY(event->position().y()) / 300.0) * 300, 86398);
        emit addRequested(timeOfDay(value));
    }
}
void TimeLineCanvas::cancelDrag()
{
    if (!isDragging()) return;
    drag_ = Drag::None; moved_ = false; unsetCursor(); update(); emit previewFinished();
}
void TimeLineCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && isDragging()) { cancelDrag(); event->accept(); }
    else ElaScrollPageArea::keyPressEvent(event);
}
void TimeLineCanvas::hideEvent(QHideEvent* event)
{
    cancelDrag(); hoverTimer_->stop(); tip_->hide(); ElaScrollPageArea::hideEvent(event);
}
void TimeLineCanvas::leaveEvent(QEvent* event)
{
    hovered_ = -1; hoverTimer_->stop(); tip_->hide(); ElaScrollPageArea::leaveEvent(event);
}
