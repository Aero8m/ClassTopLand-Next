#include "UiWidgets.h"
#include "../Core/ThemeManager/ThemeManager.h"
#include <ElaLineEdit.h>
#include <ElaComboBox.h>
#include <ElaMessageBar.h>
#include <ElaPushButton.h>
#include <ElaRollerPicker.h>
#include <ElaScrollPage.h>
#include <ElaScrollArea.h>
#include <ElaScrollPageArea.h>
#include <ElaTableView.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <QHeaderView>
#include <QApplication>
#include <QAbstractScrollArea>
#include <QAbstractItemView>
#include <QMouseEvent>
#include <QPointer>
#include <QScrollBar>
#include <QScrollPrepareEvent>
#include <QScroller>
#include <QScrollerProperties>
#include <QHBoxLayout>
#include <QStandardItemModel>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>

namespace AppUi {
namespace {
constexpr auto touchDirections = "appUiTouchScrollDirections";
constexpr auto touchExcluded = "appUiTouchScrollExcluded";

bool hasScrollRange(QAbstractScrollArea* area)
{
    const auto axes = Qt::Orientations(area->property(touchDirections).toInt());
    return (axes.testFlag(Qt::Horizontal) && area->horizontalScrollBar()->maximum() > area->horizontalScrollBar()->minimum())
        || (axes.testFlag(Qt::Vertical) && area->verticalScrollBar()->maximum() > area->verticalScrollBar()->minimum());
}

// Scope the application filter to registered scroll areas. Buffer synthesized
// mouse presses until a tap completes: some Ela controls toggle even on a
// release outside their bounds, so a fake release cannot safely cancel them.
class TouchScrollFilter final : public QObject {
public:
    explicit TouchScrollFilter() : QObject(qApp)
    {
        setObjectName(QStringLiteral("appUiTouchScrollFilter"));
        qApp->installEventFilter(this);
    }

    void add(QAbstractScrollArea* area)
    {
        areas_.removeIf([](const auto& entry) { return entry.isNull(); });
        areas_.append(area);
        connect(QScroller::scroller(area->viewport()), &QScroller::stateChanged, this,
                [this](QScroller::State state) {
            if (state == QScroller::Dragging || state == QScroller::Scrolling) canceled_ = true;
        });
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget) return false;
        if (event->type() == QEvent::ScrollPrepare && !preparing_) {
            auto* area = qobject_cast<QAbstractScrollArea*>(widget->parentWidget());
            if (!area || area->viewport() != widget || !area->property(touchDirections).isValid()) return false;
            auto* prepare = static_cast<QScrollPrepareEvent*>(event);
            // Reject an outer scroller before it becomes Pressed. Qt gives an
            // already active scroller priority, even if it is an ancestor.
            for (QWidget* child = widget->childAt(prepare->startPos().toPoint()); child && child != widget; child = child->parentWidget()) {
                if (child->property(touchExcluded).toBool()) {
                    event->ignore();
                    return true;
                }
                auto* inner = qobject_cast<QAbstractScrollArea*>(child);
                if (inner && inner->property(touchDirections).isValid() && hasScrollRange(inner)) {
                    event->ignore();
                    return true;
                }
            }
            if (!hasScrollRange(area)) {
                event->ignore();
                return true;
            }
            // Preserve native item-view preparation, including selection reset
            // on scrolling, then restrict the returned range to enabled axes.
            preparing_ = true;
            QCoreApplication::sendEvent(widget, event);
            preparing_ = false;
            if (event->isAccepted()) {
                auto range = prepare->contentPosRange();
                const auto axes = Qt::Orientations(area->property(touchDirections).toInt());
                if (!axes.testFlag(Qt::Horizontal)) { range.setX(prepare->contentPos().x()); range.setWidth(0); }
                if (!axes.testFlag(Qt::Vertical)) { range.setY(prepare->contentPos().y()); range.setHeight(0); }
                prepare->setContentPosRange(range);
            }
            return true;
        }
        if (event->type() == QEvent::Hide || event->type() == QEvent::Close || event->type() == QEvent::TouchCancel) {
            for (const auto& area : areas_) {
                if (area && (widget == area || widget == area->window() || widget->isAncestorOf(area)
                             || (event->type() == QEvent::TouchCancel && widget->window() == area->window()))) {
                    QScroller::scroller(area->viewport())->stop();
                    if (pressArea_ == area) clearPress(true);
                }
            }
        }
        if (event->type() == QEvent::TouchEnd && press_) {
            const auto sequence = sequence_;
            // A recognized gesture may consume the synthesized release.
            QTimer::singleShot(0, this, [this, sequence] { if (sequence == sequence_) clearPress(true); });
        }
        if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonDblClick
            && event->type() != QEvent::MouseMove && event->type() != QEvent::MouseButtonRelease) return false;
        auto* mouse = static_cast<QMouseEvent*>(event);
        // Replayed taps carry an application source, which also survives
        // QWidget mouse-event propagation. Do not buffer them a second time.
        if (mouse->source() == Qt::MouseEventNotSynthesized || mouse->source() == Qt::MouseEventSynthesizedByApplication) return false;
        if ((event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick)
            && mouse->button() == Qt::LeftButton) {
            QAbstractScrollArea* area = nullptr;
            for (auto* parent = widget; parent; parent = parent->parentWidget()) {
                if (parent->property(touchExcluded).toBool()) {
                    clearPress();
                    suppressRelease_ = false;
                    return false;
                }
                auto* candidate = qobject_cast<QAbstractScrollArea*>(parent);
                if (candidate && candidate->property(touchDirections).isValid()) { area = candidate; break; }
            }
            clearPress();
            suppressRelease_ = false;
            if (!area) return false;
            press_.reset(mouse->clone());
            target_ = widget;
            pressArea_ = area;
            return true;
        }
        if (!press_) {
            if (suppressRelease_ && event->type() == QEvent::MouseMove) return true;
            if (suppressRelease_ && event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton) {
                suppressRelease_ = false;
                return true;
            }
            return false;
        }
        if (event->type() == QEvent::MouseMove) {
            if ((mouse->globalPosition() - press_->globalPosition()).manhattanLength() >= QApplication::startDragDistance()) canceled_ = true;
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton) {
            auto target = target_;
            auto press = std::move(press_);
            const bool tap = !canceled_ && target && target->isVisible() && target->isEnabled();
            clearPress();
            if (tap) {
                // Finish Qt's original touch delivery before invoking controls.
                // Their click handlers can enter a modal event loop; replaying
                // inline would leave the old touch active inside that dialog.
                QTimer::singleShot(0, target, [target, press = std::move(press),
                                              released = std::unique_ptr<QMouseEvent>(mouse->clone())] {
                    if (!target || !target->isVisible() || !target->isEnabled()) return;
                    QMouseEvent replayPress(press->type(), press->position(), press->scenePosition(), press->globalPosition(),
                                            press->button(), press->buttons(), press->modifiers(),
                                            Qt::MouseEventSynthesizedByApplication, press->pointingDevice());
                    replayPress.setTimestamp(press->timestamp());
                    QCoreApplication::sendEvent(target, &replayPress);
                    if (target) {
                        QMouseEvent release(QEvent::MouseButtonRelease, target->mapFromGlobal(released->globalPosition()),
                                            target->window()->mapFromGlobal(released->globalPosition()),
                                            released->globalPosition(), Qt::LeftButton, released->buttons(), released->modifiers(),
                                            Qt::MouseEventSynthesizedByApplication, released->pointingDevice());
                        release.setTimestamp(released->timestamp());
                        QCoreApplication::sendEvent(target, &release);
                    }
                });
            }
            return true;
        }
        return false;
    }

private:
    void clearPress(bool suppressRelease = false)
    {
        if (suppressRelease && press_) suppressRelease_ = true;
        press_.reset(); target_.clear(); pressArea_.clear(); canceled_ = false; ++sequence_;
    }
    QList<QPointer<QAbstractScrollArea>> areas_;
    QPointer<QWidget> target_;
    QPointer<QAbstractScrollArea> pressArea_;
    std::unique_ptr<QMouseEvent> press_;
    bool preparing_ = false, canceled_ = false, suppressRelease_ = false;
    quint64 sequence_ = 0;
};

void sizeActionButton(ElaPushButton* control)
{
    // Include the shadow and comfortable padding on both sides of the text.
    control->setMinimumWidth(qMax(112, control->fontMetrics().horizontalAdvance(control->text()) + 40));
    control->setFixedHeight(44);
    control->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
}
}

void enableTouchScrolling(QAbstractScrollArea* area, Qt::Orientations directions)
{
    Q_ASSERT(area);
    const bool registered = area->property(touchDirections).isValid();
    area->setProperty(touchDirections, int(directions));
    if (registered) return;
    auto* filter = static_cast<TouchScrollFilter*>(qApp->findChild<QObject*>(QStringLiteral("appUiTouchScrollFilter"), Qt::FindDirectChildrenOnly));
    if (!filter) filter = new TouchScrollFilter;
    QScroller::grabGesture(area->viewport(), QScroller::TouchGesture);
    auto* scroller = QScroller::scroller(area->viewport());
    auto properties = scroller->scrollerProperties();
    properties.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy, QScrollerProperties::OvershootAlwaysOff);
    properties.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy, QScrollerProperties::OvershootAlwaysOff);
    scroller->setScrollerProperties(properties);
    if (auto* view = qobject_cast<QAbstractItemView*>(area)) {
        view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    }
    filter->add(area);
}

void excludeFromTouchScrolling(QWidget* widget)
{
    Q_ASSERT(widget);
    widget->setProperty(touchExcluded, true);
}

ElaText* text(const QString& value, QWidget* parent, int size)
{
    auto* label = new ElaText(value, size, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}

QVBoxLayout* page(ElaScrollPage* view, const QString& title, int titleSize)
{
    view->setWindowTitle(title);
    view->setTitleVisible(false);
    view->setContentsMargins(0, 0, 0, 0);
    auto* content = new QWidget(view);
    content->setWindowTitle(title);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);
    auto* heading = text(title, content, titleSize);
    QFont font = heading->font(); font.setWeight(QFont::Medium); heading->setFont(font);
    layout->addWidget(heading);
    view->addCentralWidget(content, true, false);
    // Locate the newly created wrapper, rather than any nested scroll areas.
    for (auto* parent = content->parentWidget(); parent && parent != view; parent = parent->parentWidget()) {
        if (auto* scroll = qobject_cast<QAbstractScrollArea*>(parent)) { enableTouchScrolling(scroll); break; }
    }
    return layout;
}

QVBoxLayout* card(QVBoxLayout* layout, const QString& title, int titleSize)
{
    auto* area = new ElaScrollPageArea;
    area->setBorderRadius(8);
    // ElaScrollPageArea defaults to a fixed 75px row; editor cards contain
    // multiple rows and must grow to their layout's minimum size.
    area->setMinimumHeight(0);
    area->setMaximumHeight(QWIDGETSIZE_MAX);
    area->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto* body = new QVBoxLayout(area);
    body->setSizeConstraint(QLayout::SetMinimumSize);
    body->setContentsMargins(16, 14, 16, 14);
    body->setSpacing(12);
    if (!title.isEmpty()) body->addWidget(text(title, area, titleSize));
    layout->addWidget(area);
    return body;
}

QVBoxLayout* section(QVBoxLayout* layout, const QString& title)
{
    auto* group = new QVBoxLayout;
    group->setContentsMargins(0, 0, 0, 0);
    group->setSpacing(8);
    group->addWidget(text(title, layout->parentWidget(), 16));
    layout->addLayout(group);
    return card(group);
}

void primary(ElaPushButton* control)
{
    if (!control->property("themeColorBound").toBool()) {
        control->setProperty("themeColorBound", true);
        QObject::connect(&ThemeManager::instance(), &ThemeManager::themeColorChanged,
                         control, [control] { primary(control); });
    }
    control->setLightDefaultColor(ElaThemeColor(ElaThemeType::Light, PrimaryNormal));
    control->setLightHoverColor(ElaThemeColor(ElaThemeType::Light, PrimaryHover));
    control->setLightPressColor(ElaThemeColor(ElaThemeType::Light, PrimaryPress));
    control->setLightTextColor(Qt::white);
    control->setDarkDefaultColor(ElaThemeColor(ElaThemeType::Dark, PrimaryNormal));
    control->setDarkHoverColor(ElaThemeColor(ElaThemeType::Dark, PrimaryHover));
    control->setDarkPressColor(ElaThemeColor(ElaThemeType::Dark, PrimaryPress));
    control->setDarkTextColor(Qt::black);
    control->update();
}

ElaPushButton* button(const QString& title, QWidget* parent, bool accent)
{
    auto* control = new ElaPushButton(title, parent);
    sizeActionButton(control);
    control->setBorderRadius(6);
    control->setCursor(Qt::PointingHandCursor);
    if (accent) primary(control);
    return control;
}

ElaTableView* table(QVBoxLayout* layout, const QStringList& headers, QStandardItemModel*& model)
{
    auto* view = new ElaTableView;
    model = new QStandardItemModel(view);
    model->setHorizontalHeaderLabels(headers);
    view->setModel(model);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->verticalHeader()->hide();
    view->verticalHeader()->setDefaultSectionSize(44);
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    view->horizontalHeader()->setMinimumSectionSize(64);
    view->setShowGrid(false);
    view->setMinimumHeight(260);
    view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    enableTouchScrolling(view, Qt::Horizontal | Qt::Vertical);
    layout->addWidget(view);
    return view;
}

void appendRow(QStandardItemModel* model, const QStringList& values, int sourceIndex)
{
    QList<QStandardItem*> row;
    for (const auto& value : values) {
        auto* item = new QStandardItem(value);
        item->setData(sourceIndex, Qt::UserRole);
        item->setToolTip(value);
        row.append(item);
    }
    model->appendRow(row);
}
int selected(ElaTableView* view)
{
    const auto rows = view->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().data(Qt::UserRole).toInt();
}
void select(ElaTableView* view, int sourceIndex)
{
    for (int row = 0; row < view->model()->rowCount(); ++row)
        if (view->model()->index(row, 0).data(Qt::UserRole).toInt() == sourceIndex) { view->selectRow(row); return; }
    if (view->model()->rowCount()) view->selectRow(0);
}
void emptyState(ElaTableView* view, ElaText* label)
{
    const int rows = view->model()->rowCount();
    label->setVisible(rows == 0);
    view->setVisible(rows != 0);
    view->setFixedHeight(qBound(180, 44 * rows + 42, 440));
}
void error(QWidget* parent, const QString& message)
{
    ElaMessageBar::error(ElaMessageBarType::TopRight, QStringLiteral("操作失败"), message, 5000, parent->window());
}

TimePicker::TimePicker(QTime value, QWidget* parent) : QWidget(parent)
{
    picker_ = new ElaRollerPicker(this);
    QStringList hours, minutes;
    for (int i = 0; i < 24; ++i) hours.append(QStringLiteral("%1").arg(i, 2, 10, QLatin1Char('0')));
    for (int i = 0; i < 60; ++i) minutes.append(QStringLiteral("%1").arg(i, 2, 10, QLatin1Char('0')));
    picker_->addRoller(hours); picker_->addRoller(minutes); picker_->addRoller(minutes);
    for (int i = 0; i < 3; ++i) picker_->setRollerWidth(i, 66);
    if (!value.isValid()) value = QTime(8, 0);
    picker_->setCurrentIndex({value.hour(), value.minute(), value.second()});
    picker_->setFixedSize(198, 38);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(picker_);
    layout->addStretch();
    connect(picker_, &ElaRollerPicker::currentDataChanged, this, [this] { emit timeChanged(time()); });
}
QTime TimePicker::time() const
{
    const auto indexes = picker_->getCurrentIndex();
    return indexes.size() == 3 ? QTime(indexes[0], indexes[1], indexes[2]) : QTime();
}
void TimePicker::setTime(QTime value)
{
    if (!value.isValid()) value = QTime(8, 0);
    QSignalBlocker blocker(picker_);
    picker_->setCurrentIndex({value.hour(), value.minute(), value.second()});
}

FormDialog::FormDialog(QWidget* parent, const QString& title, const QString& action) : ElaContentDialog(parent->window())
{
    setLeftButtonText(QStringLiteral("取消"));
    setMiddleButtonText(action);
    setRightButtonText(QStringLiteral("__unused__"));
    for (auto* control : findChildren<ElaPushButton*>()) {
        sizeActionButton(control);
        if (control->text() == QStringLiteral("__unused__")) control->hide();
        if (control->text() == action) primary(control);
    }
    auto* body = new QWidget(this);
    body->setMinimumWidth(440);
    body_ = new QVBoxLayout(body);
    body_->setContentsMargins(28, 24, 28, 24);
    body_->setSpacing(12);
    body_->addWidget(text(title, body, 22));
    error_ = text({}, body);
    error_->setObjectName(QStringLiteral("validationError"));
    error_->hide();
    body_->addWidget(error_);
    setCentralWidget(body);
}
void FormDialog::field(const QString& label, QWidget* editor)
{
    auto* title = text(label);
    if (auto* edit = qobject_cast<ElaLineEdit*>(editor)) title->setBuddy(edit);
    body_->insertWidget(body_->count() - 1, title);
    if (qobject_cast<ElaLineEdit*>(editor) || qobject_cast<ElaComboBox*>(editor)) editor->setFixedHeight(38);
    else editor->setMinimumHeight(38);
    editor->setAccessibleName(label);
    body_->insertWidget(body_->count() - 1, editor);
}
ElaLineEdit* FormDialog::line(const QString& label, const QString& value)
{
    auto* edit = new ElaLineEdit(this); edit->setText(value); field(label, edit); return edit;
}
void FormDialog::message(const QString& message)
{
    auto* label = text(message); label->setMaximumWidth(440);
    body_->insertWidget(body_->count() - 1, label);
}
void FormDialog::onMiddleButtonClicked()
{
    const QString failure = submit ? submit() : QString();
    if (!failure.isEmpty()) { error_->setText(failure); error_->show(); adjustSize(); return; }
    confirmed = true;
    close();
}
bool run(FormDialog* dialog)
{
    dialog->exec();
    const bool result = dialog->confirmed;
    // Retire after the button handler has returned from the nested event loop.
    QTimer::singleShot(0, dialog, &QObject::deleteLater);
    return result;
}
bool confirm(QWidget* parent, const QString& title, const QString& message)
{
    auto* dialog = new FormDialog(parent, title);
    dialog->message(message);
    return run(dialog);
}

class UnsavedDialog final : public ElaContentDialog {
public:
    UnsavedDialog(QWidget* parent, const QString& message) : ElaContentDialog(parent->window()) {
        setLeftButtonText(QStringLiteral("取消")); setMiddleButtonText(QStringLiteral("放弃")); setRightButtonText(QStringLiteral("保存"));
        for (auto* control : findChildren<ElaPushButton*>()) sizeActionButton(control);
        auto* body = new QWidget(this);
        auto* layout = new QVBoxLayout(body); layout->setContentsMargins(28, 24, 28, 24);
        layout->addWidget(text(message, body, 22));
        setCentralWidget(body);
    }
    void onMiddleButtonClicked() override { choice = UnsavedChoice::Discard; close(); }
    void onRightButtonClicked() override { choice = UnsavedChoice::Save; }
    UnsavedChoice choice = UnsavedChoice::Cancel;
};
UnsavedChoice unsaved(QWidget* parent, const QString& message)
{
    auto* dialog = new UnsavedDialog(parent, message);
    dialog->exec();
    const auto choice = dialog->choice;
    QTimer::singleShot(0, dialog, &QObject::deleteLater);
    return choice;
}
}
