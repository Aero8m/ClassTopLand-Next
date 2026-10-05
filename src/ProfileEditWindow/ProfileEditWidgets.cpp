#include "ProfileEditWidgets.h"
#include <ElaLineEdit.h>
#include <ElaComboBox.h>
#include <ElaMessageBar.h>
#include <ElaPushButton.h>
#include <ElaRollerPicker.h>
#include <ElaScrollPage.h>
#include <ElaScrollPageArea.h>
#include <ElaTableView.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QStandardItemModel>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

namespace ProfileEditUi {
namespace {
void sizeActionButton(ElaPushButton* control)
{
    // Include the shadow and comfortable padding on both sides of the text.
    control->setMinimumWidth(qMax(112, control->fontMetrics().horizontalAdvance(control->text()) + 40));
    control->setFixedHeight(44);
    control->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
}
}

ElaText* text(const QString& value, QWidget* parent, int size)
{
    auto* label = new ElaText(value, size, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}

QVBoxLayout* page(ElaScrollPage* view, const QString& title)
{
    view->setWindowTitle(title);
    view->setTitleVisible(false);
    view->setContentsMargins(0, 0, 0, 0);
    auto* content = new QWidget(view);
    content->setWindowTitle(title);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(20);
    auto* heading = text(title, content, 30);
    QFont font = heading->font(); font.setBold(true); heading->setFont(font);
    layout->addWidget(heading);
    view->addCentralWidget(content, true, false);
    return layout;
}

QVBoxLayout* card(QVBoxLayout* layout, const QString& title)
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
    body->setContentsMargins(20, 18, 20, 18);
    body->setSpacing(14);
    if (!title.isEmpty()) body->addWidget(text(title, area, 18));
    layout->addWidget(area);
    return body;
}

void primary(ElaPushButton* control)
{
    control->setLightDefaultColor(ElaThemeColor(ElaThemeType::Light, PrimaryNormal));
    control->setLightHoverColor(ElaThemeColor(ElaThemeType::Light, PrimaryHover));
    control->setLightPressColor(ElaThemeColor(ElaThemeType::Light, PrimaryPress));
    control->setLightTextColor(Qt::white);
    control->setDarkDefaultColor(ElaThemeColor(ElaThemeType::Dark, PrimaryNormal));
    control->setDarkHoverColor(ElaThemeColor(ElaThemeType::Dark, PrimaryHover));
    control->setDarkPressColor(ElaThemeColor(ElaThemeType::Dark, PrimaryPress));
    control->setDarkTextColor(Qt::black);
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
    explicit UnsavedDialog(QWidget* parent) : ElaContentDialog(parent->window()) {
        setLeftButtonText(QStringLiteral("取消")); setMiddleButtonText(QStringLiteral("放弃")); setRightButtonText(QStringLiteral("保存"));
        for (auto* control : findChildren<ElaPushButton*>()) sizeActionButton(control);
        auto* body = new QWidget(this);
        auto* layout = new QVBoxLayout(body); layout->setContentsMargins(28, 24, 28, 24);
        layout->addWidget(text(QStringLiteral("档案有未保存的更改"), body, 22));
        setCentralWidget(body);
    }
    void onMiddleButtonClicked() override { choice = UnsavedChoice::Discard; close(); }
    void onRightButtonClicked() override { choice = UnsavedChoice::Save; }
    UnsavedChoice choice = UnsavedChoice::Cancel;
};
UnsavedChoice unsaved(QWidget* parent)
{
    auto* dialog = new UnsavedDialog(parent);
    dialog->exec();
    const auto choice = dialog->choice;
    QTimer::singleShot(0, dialog, &QObject::deleteLater);
    return choice;
}
}
