#include "PluginHistoryDialog.h"
#include "../UiWidgets.h"
#include "../../Core/Plugins/PluginWorkspace.h"
#include <ElaComboBox.h>
#include <ElaPlainTextEdit.h>
#include <ElaMessageBar.h>
#include <QPointer>
#include <QDateTime>

void AppUi::showPluginHistory(PluginWorkspace *workspace, const QString &plugin, QWidget *parent, std::function<bool()> taskBusy)
{
    QString failure;
    const auto versions = workspace->versions(plugin, &failure);
    if (!failure.isEmpty() || versions.isEmpty()) {
        AppUi::error(parent, failure.isEmpty() ? QStringLiteral("该插件暂无修改历史") : failure); return;
    }
    auto *dialog = new AppUi::FormDialog(parent, QStringLiteral("插件版本历史"), QStringLiteral("回滚到所选版本"));
    dialog->message(plugin + QStringLiteral("\n版本保存修改前的代码和资源；回滚保留当前设置、私有数据和课程档案。"));
    auto *select = new ElaComboBox(dialog);
    for (const auto &entry : versions) {
        const auto value = entry.toObject();
        const auto state = value.value(QStringLiteral("status")).toString();
        const auto status = state == QStringLiteral("applied") ? QStringLiteral("已应用") : state == QStringLiteral("rolled-back") ? QStringLiteral("已回滚") :
            state == QStringLiteral("editing") ? QStringLiteral("编辑中") : state == QStringLiteral("superseded") ? QStringLiteral("已由后续修改处理") : QStringLiteral("待应用");
        const auto time = QDateTime::fromString(value.value(QStringLiteral("time")).toString(), Qt::ISODateWithMs).toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        select->addItem(time + QStringLiteral(" · ") +
            value.value(QStringLiteral("summary")).toString().left(40) + QStringLiteral(" · ") + status,
            value.value(QStringLiteral("id")).toString());
    }
    dialog->field(QStringLiteral("恢复到该次修改之前"), select);
    auto *diff = new ElaPlainTextEdit(dialog);
    diff->setReadOnly(true); diff->setLineWrapMode(QPlainTextEdit::NoWrap);
    diff->setMinimumSize(560, 260);
    dialog->field(QStringLiteral("所选版本与当前源文件的差异"), diff);
    const auto update = [workspace, plugin, select, diff] {
        QString error;
        const auto text = workspace->difference(plugin, select->currentData().toString(), &error);
        diff->setPlainText(error.isEmpty() ? text.isEmpty() ? QStringLiteral("无差异") : text : error);
    };
    QObject::connect(select, &ElaComboBox::currentIndexChanged, dialog, update);
    update();
    const QPointer<QWidget> context(parent);
    dialog->submit = [workspace, plugin, select, context, taskBusy] {
        if (taskBusy && taskBusy()) return QStringLiteral("请先停止正在运行的 Agent 任务");
        if (!AppUi::confirm(context, QStringLiteral("确认回滚"), QStringLiteral("恢复所选版本的代码和资源？当前源文件会先备份。"))) return QStringLiteral("已取消回滚");
        QString error;
        if (!workspace->rollback(plugin, select->currentData().toString(), [context](const QString &failure) {
            if (!context) return;
            if (failure.isEmpty()) ElaMessageBar::success(ElaMessageBarType::TopRight, QStringLiteral("回滚完成"), QStringLiteral("插件代码和资源已恢复"), 4000, context->window());
            else AppUi::error(context, failure);
        }, &error)) return error;
        return QString();
    };
    AppUi::run(dialog);
}
