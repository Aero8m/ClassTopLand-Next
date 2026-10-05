#pragma once

#include <ElaContentDialog.h>
#include <QTime>
#include <functional>

class ElaLineEdit;
class ElaPushButton;
class ElaRollerPicker;
class ElaScrollPage;
class ElaTableView;
class ElaText;
class QStandardItemModel;
class QVBoxLayout;

namespace ProfileEditUi {
QVBoxLayout* page(ElaScrollPage* page, const QString& title);
QVBoxLayout* card(QVBoxLayout* layout, const QString& title = {});
ElaText* text(const QString& value, QWidget* parent = nullptr, int size = 14);
ElaPushButton* button(const QString& title, QWidget* parent = nullptr, bool primary = false);
void primary(ElaPushButton* button);
ElaTableView* table(QVBoxLayout* layout, const QStringList& headers, QStandardItemModel*& model);
void appendRow(QStandardItemModel* model, const QStringList& values, int sourceIndex);
int selected(ElaTableView* table);
void select(ElaTableView* table, int sourceIndex);
void emptyState(ElaTableView* table, ElaText* label);
void error(QWidget* parent, const QString& message);

class TimePicker final : public QWidget {
    Q_OBJECT
public:
    explicit TimePicker(QTime time, QWidget* parent = nullptr);
    QTime time() const;
    void setTime(QTime time);
signals:
    void timeChanged(QTime time);
private:
    ElaRollerPicker* picker_;
};

// The middle button has no unconditional auto-close in ElaContentDialog,
// allowing validation errors to keep the form and its entered values open.
class FormDialog final : public ElaContentDialog {
public:
    FormDialog(QWidget* parent, const QString& title, const QString& action = QStringLiteral("确定"));
    void field(const QString& label, QWidget* editor);
    ElaLineEdit* line(const QString& label, const QString& value = {});
    void message(const QString& message);
    void onMiddleButtonClicked() override;
    std::function<QString()> submit;
    bool confirmed = false;
private:
    QVBoxLayout* body_;
    ElaText* error_;
};

bool run(FormDialog* dialog);
bool confirm(QWidget* parent, const QString& title, const QString& message);
enum class UnsavedChoice { Cancel, Discard, Save };
UnsavedChoice unsaved(QWidget* parent);
}
