#pragma once

#include <ElaContentDialog.h>
#include <QHash>
#include "SettingsLayout.h"
#include <QSize>
#include <QTime>
#include <functional>

class ElaLineEdit;
class ElaPushButton;
class ElaRollerPicker;
class ElaScrollArea;
class ElaScrollPage;
class ElaTableView;
class ElaText;
class ElaWindow;
class QAbstractScrollArea;
class QStandardItemModel;
class QVBoxLayout;

namespace AppUi {
inline constexpr QSize defaultWindowSize{850, 550};

// Keeps stable page IDs for reopening while Ela owns the actual navigation.
class LazyPages final : public QObject {
public:
    explicit LazyPages(ElaWindow* window);
    void add(const QString& id, const QString& title, ElaIconType::IconName icon,
             std::function<QWidget*(QWidget*)> create);
    QWidget* showPage(const QString& id);
    QString currentPage() const;

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    struct Page {
        QWidget* host;
        std::function<QWidget*(QWidget*)> create;
        QWidget* content = nullptr;
    };
    QWidget* ensure(Page& page);

    ElaWindow* window_;
    QHash<QString, Page> pages_;
};

// Touch gestures only; mouse selection, editing and drag/drop remain available.
void enableTouchScrolling(QAbstractScrollArea* area, Qt::Orientations directions = Qt::Vertical);
// Let an interactive canvas handle touches without ancestor scrolling or tap buffering.
void excludeFromTouchScrolling(QWidget* widget);
QVBoxLayout* page(ElaScrollPage* page, const QString& title, int titleSize = 28);
QVBoxLayout* card(QVBoxLayout* layout, const QString& title = {}, int titleSize = 18);
// Settings section: a 16px heading above the card, separated by 8px.
QVBoxLayout* section(QVBoxLayout* layout, const QString& title);
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
    ElaScrollArea* content_;
    QVBoxLayout* body_;
    ElaText* error_;
};

bool run(FormDialog* dialog);
bool confirm(QWidget* parent, const QString& title, const QString& message);
enum class UnsavedChoice { Cancel, Discard, Save };

class UnsavedDialog final : public ElaContentDialog {
public:
    UnsavedDialog(QWidget* parent, const QString& message);
    void onMiddleButtonClicked() override;
    void onRightButtonClicked() override;
    UnsavedChoice choice() const { return choice_; }
private:
    UnsavedChoice choice_ = UnsavedChoice::Cancel;
};

UnsavedChoice unsaved(QWidget* parent, const QString& message = QStringLiteral("档案有未保存的更改"));
}
