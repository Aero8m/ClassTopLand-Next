#include "SubjectsTab.h"
#include "../ProfileEditSession.h"
#include "../ProfileEditWidgets.h"
#include <ElaLineEdit.h>
#include <ElaPushButton.h>
#include <ElaTableView.h>
#include <ElaText.h>
#include <QHBoxLayout>
#include <QStandardItemModel>
#include <QVBoxLayout>

SubjectsTab::SubjectsTab(ProfileEditSession* session, QWidget* parent) : ElaScrollPage(parent), session_(session)
{
    using namespace ProfileEditUi;
    auto* layout = page(this, tr("科目管理"));
    auto* body = card(layout);
    auto* actions = new QHBoxLayout;
    auto* add = button(tr("新增科目"), nullptr, true);
    edit_ = button(tr("编辑")); remove_ = button(tr("删除"));
    actions->addWidget(add); actions->addWidget(edit_); actions->addWidget(remove_); actions->addStretch(); body->addLayout(actions);
    empty_ = text(tr("暂无科目")); body->addWidget(empty_);
    table_ = table(body, {tr("科目名称"), tr("简称"), tr("教师")}, model_);
    layout->addStretch();
    connect(add, &ElaPushButton::clicked, this, [this] { editSubject(-1); });
    connect(edit_, &ElaPushButton::clicked, this, [this] { editSubject(ProfileEditUi::selected(table_)); });
    connect(remove_, &ElaPushButton::clicked, this, &SubjectsTab::removeSubject);
    connect(table_, &ElaTableView::doubleClicked, this, [this] { editSubject(ProfileEditUi::selected(table_)); });
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        const bool enabled = ProfileEditUi::selected(table_) >= 0; edit_->setEnabled(enabled); remove_->setEnabled(enabled);
    });
    connect(session_, &ProfileEditSession::changed, this, &SubjectsTab::refresh);
    refresh();
}
void SubjectsTab::refresh()
{
    using namespace ProfileEditUi;
    int index = selected(table_);
    model_->removeRows(0, model_->rowCount());
    const auto& subjects = session_->draft().subjects;
    for (int i = 0; i < subjects.size(); ++i) appendRow(model_, {subjects[i].name, subjects[i].simplifiedName, subjects[i].teacher}, i);
    select(table_, index); emptyState(table_, empty_);
    edit_->setEnabled(selected(table_) >= 0); remove_->setEnabled(selected(table_) >= 0);
}
void SubjectsTab::editSubject(int index)
{
    using namespace ProfileEditUi;
    if (index < -1 || index >= session_->draft().subjects.size()) return;
    const Subject value = index >= 0 ? session_->draft().subjects[index] : Subject({});
    auto* dialog = new FormDialog(this, index < 0 ? tr("新增科目") : tr("编辑科目"));
    auto* name = dialog->line(tr("科目名称"), value.name);
    auto* shortName = dialog->line(tr("简称"), value.simplifiedName);
    auto* teacher = dialog->line(tr("教师"), value.teacher);
    dialog->submit = [this, index, name, shortName, teacher] {
        QString error;
        session_->updateSubject(index, Subject(name->text(), shortName->text(), teacher->text()), &error);
        return error;
    };
    if (run(dialog)) select(table_, index < 0 ? session_->draft().subjects.size() - 1 : index);
}
void SubjectsTab::removeSubject()
{
    using namespace ProfileEditUi;
    const int index = selected(table_);
    if (index < 0) return;
    if (!confirm(this, tr("删除科目"), tr("确定删除“%1”？").arg(session_->draft().subjects[index].name))) return;
    QString failure;
    if (!session_->removeSubject(index, &failure)) error(this, failure);
}
void SubjectsTab::focusItem(int index) { ProfileEditUi::select(table_, index); table_->setFocus(); }
