#include "TimeLinesTab.h"
#include "../ProfileEditSession.h"
#include "../ProfileEditWidgets.h"
#include "../widgets/TimeLineCanvas.h"
#include <ElaComboBox.h>
#include <ElaLineEdit.h>
#include <ElaPushButton.h>
#include <ElaScrollArea.h>
#include <ElaScrollPageArea.h>
#include <ElaText.h>
#include <QBoxLayout>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace {
bool samePoints(const QList<TimeLinePoint>& a, const QList<TimeLinePoint>& b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i)
        if (a[i].startTime != b[i].startTime || a[i].endTime != b[i].endTime) return false;
    return true;
}
class PointsCommand final : public QUndoCommand {
public:
    PointsCommand(const QString& title, std::function<void(bool)> apply) : QUndoCommand(title), apply_(std::move(apply)) {}
    void undo() override { apply_(false); }
    void redo() override { apply_(true); }
private:
    std::function<void(bool)> apply_;
};
}

TimeLinesTab::TimeLinesTab(ProfileEditSession* session, QWidget* parent) : ElaScrollPage(parent), session_(session)
{
    using namespace ProfileEditUi;
    undo_ = new QUndoStack(this);
    auto* layout = page(this, tr("时间线管理"));
    auto* selector = card(layout);
    lines_ = new ElaComboBox; lines_->setFixedHeight(38); lines_->setAccessibleName(tr("时间线"));
    auto* actions = new QHBoxLayout;
    actions->addWidget(lines_, 1);
    auto* create = button(tr("新增时间线"), nullptr, true);
    rename_ = button(tr("重命名")); removeLine_ = button(tr("删除时间线"));
    actions->addWidget(create); actions->addWidget(rename_); actions->addWidget(removeLine_); selector->addLayout(actions);
    auto* tools = new QHBoxLayout;
    add_ = button(tr("新增时段"), nullptr, true); remove_ = button(tr("删除"));
    auto* undo = button(tr("撤销")); auto* redo = button(tr("重做"));
    tools->addWidget(add_); tools->addWidget(remove_); tools->addWidget(undo); tools->addWidget(redo); tools->addStretch(); layout->addLayout(tools);
    auto* content = new QWidget(this);
    columns_ = new QBoxLayout(QBoxLayout::LeftToRight, content);
    columns_->setContentsMargins(0, 0, 0, 0); columns_->setSpacing(16);
    auto* axis = new ElaScrollPageArea(content);
    axis->setMinimumHeight(0); axis->setMaximumHeight(QWIDGETSIZE_MAX); axis->setBorderRadius(8);
    axis->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* axisLayout = new QVBoxLayout(axis); axisLayout->setContentsMargins(12, 12, 12, 12); axisLayout->setSpacing(8);
    auto* zoomTools = new QHBoxLayout;
    zoomOut_ = button(QStringLiteral("−")); zoomIn_ = button(QStringLiteral("+"));
    for (auto* control : {zoomOut_, zoomIn_}) control->setFixedWidth(44);
    zoomOut_->setAccessibleName(tr("缩小")); zoomIn_->setAccessibleName(tr("放大"));
    auto* resetZoom = button(tr("重置")); zoomText_ = text(QStringLiteral("100%")); zoomText_->setAlignment(Qt::AlignCenter);
    zoomTools->addWidget(zoomOut_); zoomTools->addWidget(zoomText_); zoomTools->addWidget(zoomIn_); zoomTools->addStretch(); zoomTools->addWidget(resetZoom);
    axisLayout->addLayout(zoomTools);
    empty_ = text(tr("暂无时段")); axisLayout->addWidget(empty_);
    scroll_ = new ElaScrollArea(axis); scroll_->setObjectName(QStringLiteral("timeLineScroll"));
    scroll_->setStyleSheet(QStringLiteral("#timeLineScroll{background-color:transparent;border:0px;}"));
    scroll_->setIsGrabGesture(false); scroll_->setIsOverShoot(Qt::Vertical, false); scroll_->setIsAnimation(Qt::Vertical, false);
    scroll_->setWidgetResizable(true); scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    canvas_ = new TimeLineCanvas; scroll_->setWidget(canvas_); axisLayout->addWidget(scroll_);
    columns_->addWidget(axis, 1);
    auto* panel = new ElaScrollPageArea(content);
    panel->setObjectName(QStringLiteral("timeLineEditorPanel")); panel->setMinimumHeight(0); panel->setMaximumHeight(QWIDGETSIZE_MAX);
    panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred); panel->setBorderRadius(8);
    auto* panelLayout = new QVBoxLayout(panel); panelLayout->setContentsMargins(18, 18, 18, 18); panelLayout->setSpacing(14);
    panelLayout->addWidget(text(tr("编辑时段"), panel, 22));
    editor_ = new QWidget(panel); auto* fields = new QVBoxLayout(editor_); fields->setContentsMargins(0, 0, 0, 0); fields->setSpacing(14);
    fields->addWidget(text(tr("开始时间"))); start_ = new TimePicker(QTime(8, 0)); start_->setObjectName(QStringLiteral("timeLineStart"));
    start_->setAccessibleName(tr("开始时间")); fields->addWidget(start_);
    fields->addWidget(text(tr("结束时间"))); end_ = new TimePicker(QTime(8, 45)); end_->setObjectName(QStringLiteral("timeLineEnd"));
    end_->setAccessibleName(tr("结束时间")); fields->addWidget(end_);
    panelLayout->addWidget(editor_); panelLayout->addStretch(); columns_->addWidget(panel);
    layout->addWidget(content); layout->addStretch();
    connect(create, &ElaPushButton::clicked, this, [this] { editLine(true); });
    connect(rename_, &ElaPushButton::clicked, this, [this] { editLine(false); });
    connect(removeLine_, &ElaPushButton::clicked, this, &TimeLinesTab::removeLine);
    connect(lines_, qOverload<int>(&ElaComboBox::currentIndexChanged), this, &TimeLinesTab::switchLine);
    connect(add_, &ElaPushButton::clicked, this, [this] { addPoint(); });
    connect(remove_, &ElaPushButton::clicked, this, &TimeLinesTab::removePoint);
    const auto doUndo = [this] { canvas_->cancelDrag(); undo_->undo(); };
    const auto doRedo = [this] { canvas_->cancelDrag(); undo_->redo(); };
    connect(undo, &ElaPushButton::clicked, this, doUndo); connect(redo, &ElaPushButton::clicked, this, doRedo);
    undo->setEnabled(false); redo->setEnabled(false);
    connect(undo_, &QUndoStack::canUndoChanged, undo, &ElaPushButton::setEnabled);
    connect(undo_, &QUndoStack::canRedoChanged, redo, &ElaPushButton::setEnabled);
    auto* undoKey = new QShortcut(QKeySequence::Undo, this); undoKey->setContext(Qt::WidgetWithChildrenShortcut);
    auto* redoKey = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y), this); redoKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(undoKey, &QShortcut::activated, this, doUndo); connect(redoKey, &QShortcut::activated, this, doRedo);
    connect(zoomOut_, &ElaPushButton::clicked, this, [this] { changeZoom(canvas_->zoom() - 25); });
    connect(zoomIn_, &ElaPushButton::clicked, this, [this] { changeZoom(canvas_->zoom() + 25); });
    connect(resetZoom, &ElaPushButton::clicked, this, [this] { changeZoom(100); });
    connect(canvas_, &TimeLineCanvas::selectionRequested, this, [this] { syncSelection(); });
    connect(canvas_, &TimeLineCanvas::timeChangeRequested, this, [this](int index, QTime start, QTime end) {
        auto points = displayed_; if (index < 0 || index >= points.size()) return;
        points[index] = TimeLinePoint(start, end); QString message;
        if (!pushChange(points, index, tr("调整时段"), &message)) error(this, message);
    });
    connect(canvas_, &TimeLineCanvas::invalidChange, this, [this](const QString& message) { error(this, message); });
    connect(canvas_, &TimeLineCanvas::addRequested, this, &TimeLinesTab::addPoint);
    connect(canvas_, &TimeLineCanvas::previewChanged, this, [this](QTime start, QTime end) {
        if (start.isValid()) start_->setTime(start);
        if (end.isValid()) end_->setTime(end);
    });
    connect(canvas_, &TimeLineCanvas::previewFinished, this, &TimeLinesTab::syncSelection);
    connect(start_, &TimePicker::timeChanged, this, &TimeLinesTab::changeSelectedTime);
    connect(end_, &TimePicker::timeChanged, this, &TimeLinesTab::changeSelectedTime);
    connect(session_, &ProfileEditSession::changed, this, &TimeLinesTab::refresh);
    connect(session_, &ProfileEditSession::draftReset, this, [this] {
        undo_->clear(); canvas_->cancelDrag(); canvas_->setSelectedIndex(-1); syncSelection(); scrollToSelection(true);
    });
    refresh(); updateLayout();
}
void TimeLinesTab::refresh()
{
    const int previous = lines_->currentIndex();
    { QSignalBlocker blocker(lines_); lines_->clear();
      for (const auto& line : session_->draft().timeLines) lines_->addItem(line.name);
      if (lines_->count()) lines_->setCurrentIndex(qBound(0, previous, lines_->count() - 1)); }
    if (currentLine_ != lines_->currentIndex()) { switchLine(); return; }
    const bool enabled = currentLine_ >= 0;
    rename_->setEnabled(enabled); removeLine_->setEnabled(enabled); add_->setEnabled(enabled); canvas_->setEnabled(enabled);
    const auto points = enabled ? session_->draft().timeLines[currentLine_].timePoints : QList<TimeLinePoint>();
    if (!samePoints(points, displayed_)) {
        if (!applying_) undo_->clear();
        displayed_ = points; canvas_->setTimePoints(points);
    }
    empty_->setVisible(points.isEmpty()); syncSelection();
}
void TimeLinesTab::switchLine()
{
    canvas_->cancelDrag(); undo_->clear(); currentLine_ = lines_->currentIndex();
    displayed_ = currentLine_ >= 0 ? session_->draft().timeLines[currentLine_].timePoints : QList<TimeLinePoint>();
    canvas_->setTimePoints(displayed_); canvas_->setSelectedIndex(-1);
    refresh(); scrollToSelection(true);
}
void TimeLinesTab::syncSelection()
{
    const int index = canvas_->selectedIndex();
    const bool selected = index >= 0 && index < displayed_.size();
    remove_->setEnabled(selected); editor_->setEnabled(selected);
    if (selected) { start_->setTime(displayed_[index].startTime); end_->setTime(displayed_[index].endTime); }
}
void TimeLinesTab::editLine(bool create)
{
    using namespace ProfileEditUi;
    const int index = currentLine_;
    if (!create && index < 0) return;
    auto* dialog = new FormDialog(this, create ? tr("新增时间线") : tr("重命名时间线"));
    auto* name = dialog->line(tr("名称"), create ? QString() : session_->draft().timeLines[index].name);
    dialog->submit = [this, create, index, name] {
        const auto value = name->text().trimmed();
        if (value.isEmpty()) return tr("时间线名称不能为空");
        if (create) session_->edit().timeLines.append(TimeLine(value));
        else session_->edit().timeLines[index].name = value;
        session_->notifyChanged(); return QString();
    };
    if (run(dialog) && create) lines_->setCurrentIndex(lines_->count() - 1);
}
void TimeLinesTab::removeLine()
{
    using namespace ProfileEditUi;
    if (currentLine_ < 0 || !confirm(this, tr("删除时间线"), tr("确定删除“%1”及其全部时段？").arg(lines_->currentText()))) return;
    undo_->clear(); canvas_->cancelDrag();
    session_->edit().timeLines.removeAt(currentLine_); currentLine_ = -1; session_->notifyChanged();
}
bool TimeLinesTab::pushChange(const QList<TimeLinePoint>& points, int selection, const QString& title, QString* error)
{
    if (currentLine_ < 0) { if (error) *error = tr("请选择时间线"); return false; }
    if (!ProfileEditSession::validateTimePoints(points, error)) return false;
    if (samePoints(points, displayed_)) return true;
    const auto before = displayed_; const int beforeSelection = canvas_->selectedIndex();
    undo_->push(new PointsCommand(title, [this, before, points, beforeSelection, selection](bool forward) {
        applySnapshot(forward ? points : before, forward ? selection : beforeSelection);
    }));
    return true;
}
void TimeLinesTab::applySnapshot(const QList<TimeLinePoint>& points, int selection)
{
    applying_ = true;
    QString message;
    const bool accepted = session_->replaceTimePoints(currentLine_, points, &message);
    applying_ = false;
    if (!accepted) { ProfileEditUi::error(this, message); return; }
    canvas_->setSelectedIndex(selection); syncSelection(); scrollToSelection();
}
void TimeLinesTab::addPoint(QTime requested)
{
    using namespace ProfileEditUi;
    if (currentLine_ < 0) return;
    const int line = currentLine_;
    if (!requested.isValid()) {
        const int selected = canvas_->selectedIndex();
        requested = selected >= 0 ? displayed_[selected].endTime : QTime(8, 0);
        if (selected < 0 && !displayed_.isEmpty()) {
            requested = QTime(0, 0);
            for (const auto& point : displayed_) if (point.endTime > requested) requested = point.endTime;
        }
    }
    const int second = qBound(0, QTime(0, 0).secsTo(requested), 86398);
    auto* dialog = new FormDialog(this, tr("新增时段"));
    auto* from = new TimePicker(QTime(0, 0).addSecs(second));
    auto* to = new TimePicker(QTime(0, 0).addSecs(qMin(second + 45 * 60, 86399)));
    dialog->field(tr("开始时间"), from); dialog->field(tr("结束时间"), to);
    dialog->submit = [this, line, from, to] {
        if (currentLine_ != line) return tr("时间线已切换");
        auto points = displayed_; points.append(TimeLinePoint(from->time(), to->time()));
        QString message; pushChange(points, points.size() - 1, tr("新增时段"), &message); return message;
    };
    run(dialog);
}
void TimeLinesTab::removePoint()
{
    const int index = canvas_->selectedIndex();
    if (index < 0 || !ProfileEditUi::confirm(this, tr("删除时段"), tr("确定删除所选时段？"))) return;
    auto points = displayed_; points.removeAt(index);
    QString message;
    if (!pushChange(points, points.isEmpty() ? -1 : qMin(index, int(points.size()) - 1), tr("删除时段"), &message)) ProfileEditUi::error(this, message);
}
void TimeLinesTab::changeSelectedTime()
{
    const int index = canvas_->selectedIndex();
    if (index < 0) return;
    auto points = displayed_; points[index] = TimeLinePoint(start_->time(), end_->time());
    QString message;
    if (!pushChange(points, index, tr("编辑时间"), &message)) { syncSelection(); ProfileEditUi::error(this, message); }
}
void TimeLinesTab::scrollToSelection(bool initial)
{
    QTimer::singleShot(0, this, [this, initial] {
        const int selected = canvas_->selectedIndex();
        if (!initial && selected >= 0) {
            const auto rect = canvas_->pointRect(selected);
            const int visibleTop = scroll_->verticalScrollBar()->value(), visibleBottom = visibleTop + scroll_->viewport()->height();
            if (rect.top() < visibleTop + 12 || rect.bottom() > visibleBottom - 12)
                scroll_->verticalScrollBar()->setValue(qMax(0, int(rect.top()) - 30));
        } else if (initial) {
            QTime first;
            for (const auto& point : displayed_) if (point.startTime.isValid() && (!first.isValid() || point.startTime < first)) first = point.startTime;
            int second = !first.isValid() ? 7 * 3600 : qMax(0, QTime(0, 0).secsTo(first) - 1800);
            scroll_->verticalScrollBar()->setValue(qMax(0, qRound(canvas_->yForSeconds(second)) - 12));
        }
    });
}
void TimeLinesTab::changeZoom(int zoom)
{
    const int anchor = canvas_->secondsAtY(scroll_->verticalScrollBar()->value() + scroll_->viewport()->height() / 2.0);
    canvas_->setZoom(zoom);
    zoomText_->setText(QStringLiteral("%1%").arg(canvas_->zoom()));
    zoomOut_->setEnabled(canvas_->zoom() > 50); zoomIn_->setEnabled(canvas_->zoom() < 300);
    scroll_->verticalScrollBar()->setValue(qRound(canvas_->yForSeconds(anchor) - scroll_->viewport()->height() / 2.0));
}
void TimeLinesTab::focusItem(int index, int row)
{
    lines_->setCurrentIndex(index); canvas_->setSelectedIndex(row); syncSelection(); scrollToSelection(); canvas_->setFocus();
}
void TimeLinesTab::resizeEvent(QResizeEvent* event)
{
    ElaScrollPage::resizeEvent(event); updateLayout();
}
void TimeLinesTab::updateLayout()
{
    if (!columns_) return;
    const bool narrow = width() < 830;
    columns_->setDirection(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    auto* panel = editor_->parentWidget();
    panel->setMinimumWidth(narrow ? 0 : 238); panel->setMaximumWidth(narrow ? QWIDGETSIZE_MAX : 258);
    scroll_->setFixedHeight(narrow ? 300 : qMax(220, height() - 350));
}
