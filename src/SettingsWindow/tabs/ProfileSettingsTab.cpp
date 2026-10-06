#include "ProfileSettingsTab.h"
#include "../../ProfileEditWindow/ProfileEditWidgets.h"
#include "../../Core/ProfileManager/ProfileExchange.h"

#include <ElaComboBox.h>
#include <ElaLineEdit.h>
#include <ElaMessageBar.h>
#include <ElaPushButton.h>
#include <ElaScrollArea.h>
#include <ElaTableView.h>
#include <ElaText.h>
#include <QFileInfo>
#include <QFileDialog>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHash>
#include <QPainter>
#include <QScopedValueRollback>
#include <QShowEvent>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {
// Ela's table style draws the complete text without elision. Keep that style's
// row selection and theme colors while containing long names within each cell.
class ProfileItemDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem display(option);
        initStyleOption(&display, index);
        display.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
        display.features.setFlag(QStyleOptionViewItem::WrapText, false);
        display.text = display.fontMetrics.elidedText(display.text, Qt::ElideRight,
                                                     qMax(0, display.rect.width() - 24));
        painter->save();
        painter->setClipRect(display.rect, Qt::IntersectClip);
        display.widget->style()->drawControl(QStyle::CE_ItemViewItem, &display, painter, display.widget);
        painter->restore();
    }
};
}

ProfileSettingsTab::ProfileSettingsTab(QWidget* parent) : ElaScrollPage(parent)
{
    using namespace ProfileEditUi;
    auto* layout = page(this, tr("档案管理"));
    auto* controls = card(layout);
    controls->addWidget(text(tr("添加或删除档案，切换档案需要重启应用。其他档案需先切换后编辑。")));
    auto* actions = new QHBoxLayout;
    add_ = button(tr("添加档案"), this, true);
    edit_ = button(tr("编辑当前档案"), this);
    switch_ = button(tr("切换并重启"), this);
    delete_ = button(tr("删除"), this);
    add_->setObjectName(QStringLiteral("addProfile"));
    edit_->setObjectName(QStringLiteral("editCurrentProfile"));
    switch_->setObjectName(QStringLiteral("switchProfile"));
    delete_->setObjectName(QStringLiteral("deleteProfile"));
    for (auto* action : {add_, edit_, switch_, delete_}) actions->addWidget(action);
    actions->addStretch();
    controls->addLayout(actions);
    auto* exchangeActions = new QHBoxLayout;
    import_ = button(tr("导入档案"), this);
    export_ = button(tr("导出档案"), this);
    import_->setObjectName(QStringLiteral("importProfile"));
    export_->setObjectName(QStringLiteral("exportProfile"));
    exchangeActions->addWidget(import_);
    exchangeActions->addWidget(export_);
    exchangeActions->addStretch();
    controls->addLayout(exchangeActions);
    controls->addWidget(text(tr("支持原生 JSON 完整档案、ClassTopLand tables.json 常规课表和 CSES 课表。导入会创建新档案；导出使用选中档案的已保存内容。")));
    table_ = table(layout, {tr("档案名称"), tr("文件名"), tr("状态")}, model_);
    table_->setObjectName(QStringLiteral("profileList"));
    table_->setItemDelegate(new ProfileItemDelegate(table_));
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table_->horizontalHeader()->resizeSection(2, 136);
    table_->setAccessibleName(tr("档案列表"));
    table_->setMinimumHeight(320);
    status_ = text({});
    status_->setObjectName(QStringLiteral("profileListStatus"));
    layout->addWidget(status_);
    layout->addStretch();
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ProfileSettingsTab::updateActions);
    connect(add_, &ElaPushButton::clicked, this, &ProfileSettingsTab::addProfile);
    connect(edit_, &ElaPushButton::clicked, this, &ProfileSettingsTab::profileEditorRequested);
    connect(switch_, &ElaPushButton::clicked, this, &ProfileSettingsTab::switchProfile);
    connect(delete_, &ElaPushButton::clicked, this, &ProfileSettingsTab::deleteProfile);
    connect(import_, &ElaPushButton::clicked, this, &ProfileSettingsTab::importProfile);
    connect(export_, &ElaPushButton::clicked, this, &ProfileSettingsTab::exportProfile);
    refreshProfiles();
}

int ProfileSettingsTab::selectedIndex() const
{
    const int index = ProfileEditUi::selected(table_);
    return index >= 0 && index < entries_.size() ? index : -1;
}

void ProfileSettingsTab::refreshProfiles(const QString& selectedId)
{
    QString selection = selectedId;
    const int oldIndex = selectedIndex();
    if (selection.isEmpty() && oldIndex >= 0) selection = entries_[oldIndex].id;
    QString error;
    entries_ = ProfileManager::instance().listProfiles(&error);
    model_->removeRows(0, model_->rowCount());
    int selectedRow = -1, currentRow = -1;
    for (int i = 0; i < entries_.size(); ++i) {
        const auto& entry = entries_[i];
        QString state = entry.current ? tr("当前使用") : tr("可切换");
        if (!entry.isValid()) state = entry.current ? tr("当前使用 · 文件异常") : tr("文件异常");
        ProfileEditUi::appendRow(model_, {entry.name, QFileInfo(entry.filePath).fileName(), state}, i);
        model_->item(i, 0)->setToolTip(entry.name + QLatin1Char('\n') + entry.filePath);
        if (!entry.isValid()) model_->item(i, 2)->setToolTip(entry.error);
        if (entry.id == selection) selectedRow = i;
        if (entry.current) currentRow = i;
    }
    if (selectedRow < 0) selectedRow = currentRow >= 0 ? currentRow : (entries_.isEmpty() ? -1 : 0);
    if (selectedRow >= 0) ProfileEditUi::select(table_, selectedRow);
    status_->setText(!error.isEmpty() ? tr("读取档案列表失败：%1").arg(error)
                       : entries_.isEmpty() ? tr("暂无档案，可点击“添加档案”创建。")
                                            : tr("共 %1 个档案。当前档案需先切换后删除；文件异常的档案无法切换。").arg(entries_.size()));
    updateActions();
}

void ProfileSettingsTab::showEvent(QShowEvent* event)
{
    ElaScrollPage::showEvent(event);
    refreshProfiles();
}

void ProfileSettingsTab::updateActions()
{
    const int index = selectedIndex();
    const bool other = index >= 0 && !entries_[index].current;
    add_->setEnabled(!busy_);
    edit_->setEnabled(!busy_ && !ProfileManager::instance().filePath().isEmpty());
    switch_->setEnabled(!busy_ && other && entries_[index].isValid());
    delete_->setEnabled(!busy_ && other);
    import_->setEnabled(!busy_ && !ProfileManager::instance().filePath().isEmpty());
    export_->setEnabled(!busy_ && index >= 0 && entries_[index].isValid());
}

void ProfileSettingsTab::showError(const QString& message)
{
    ProfileEditUi::error(this, message);
}

void ProfileSettingsTab::addProfile()
{
    if (busy_) return;
    {
        QScopedValueRollback<bool> guard(busy_, true);
        updateActions();
        auto* dialog = new ProfileEditUi::FormDialog(this, tr("添加档案"), tr("创建"));
        auto* name = dialog->line(tr("档案名称"));
        name->setObjectName(QStringLiteral("newProfileName"));
        auto* mode = new ElaComboBox(dialog);
        mode->setObjectName(QStringLiteral("newProfileMode"));
        mode->addItems({tr("空白档案"), tr("复制当前已保存档案")});
        dialog->field(tr("创建方式"), mode);
        dialog->message(tr("创建后不会自动切换。复制仅使用已保存内容，不包含编辑窗口中的未保存更改。"));
        QString createdId;
        dialog->submit = [&] {
            QString error;
            ProfileManager::instance().createProfile(name->text(), mode->currentIndex() == 1, createdId, &error);
            return error;
        };
        const bool confirmed = ProfileEditUi::run(dialog);
        // The dialog is retired after the nested event loop; clear stack captures.
        dialog->submit = {};
        if (confirmed) refreshProfiles(createdId);
    }
    updateActions();
}
void ProfileSettingsTab::deleteProfile()
{
    const int index = selectedIndex();
    if (busy_ || index < 0 || entries_[index].current) return;
    const auto entry = entries_[index];
    {
        QScopedValueRollback<bool> guard(busy_, true);
        updateActions();
        if (ProfileEditUi::confirm(this, tr("删除档案"),
                tr("确定永久删除档案“%1”？\n文件：%2\n此操作无法撤销。")
                    .arg(entry.name, QFileInfo(entry.filePath).fileName()))) {
            QString error;
            if (ProfileManager::instance().removeProfile(entry.id, &error)) refreshProfiles();
            else showError(tr("删除失败：%1").arg(error));
        }
    }
    updateActions();
}

void ProfileSettingsTab::switchProfile()
{
    const int index = selectedIndex();
    if (busy_ || index < 0 || entries_[index].current || !entries_[index].isValid()) return;
    const auto entry = entries_[index];
    {
        QScopedValueRollback<bool> guard(busy_, true);
        updateActions();
        if (ProfileEditUi::confirm(this, tr("切换档案"),
                tr("切换到档案“%1”需要立即重启应用。\n如有未保存更改，将先询问如何处理。\n确定切换并重启？").arg(entry.name)))
            emit profileSwitchRequested(entry.id);
    }
    updateActions();
}

void ProfileSettingsTab::importProfile()
{
    if (busy_) return;
    {
        QScopedValueRollback<bool> guard(busy_, true);
        updateActions();
        auto operation = [&] {
            QFileDialog picker(this, tr("导入档案"));
            picker.setObjectName(QStringLiteral("importProfileFile"));
            picker.setFileMode(QFileDialog::ExistingFile);
            picker.setNameFilters({tr("档案文件 (*.json *.yaml *.yml)"), tr("JSON 档案或 ClassTopLand 课表 (*.json)"), tr("CSES 课表 (*.yaml *.yml)")});
            if (picker.exec() != QDialog::Accepted) return;
            const QString path = picker.selectedFiles().value(0);
            const QString suffix = QFileInfo(path).suffix().toLower();
            if (suffix != QStringLiteral("json") && suffix != QStringLiteral("yaml") && suffix != QStringLiteral("yml")) {
                showError(tr("请选择 JSON 或 YAML 文件。")); return;
            }
            ProfileExchange::Format format = ProfileExchange::Format::NativeJson;
            Profile candidate;
            QStringList warnings;
            QString error;
            if (!ProfileExchange::readImportFile(path, candidate, format, &warnings, &error)) {
                showError(tr("导入失败：%1").arg(error)); return;
            }
            auto* dialog = new ProfileEditUi::FormDialog(this, tr("导入档案"), tr("导入"));
            auto* name = dialog->line(tr("档案名称"), candidate.name);
            name->setObjectName(QStringLiteral("importProfileName"));
            const QString formatName = format == ProfileExchange::Format::NativeJson ? tr("原生 JSON 完整档案")
                : format == ProfileExchange::Format::ClassTopLandTablesJson ? tr("ClassTopLand tables.json 常规课表") : tr("CSES 课表");
            QString message = tr("识别格式：%1。\n导入后创建新档案，不会自动切换或覆盖已有档案。").arg(formatName);
            if (format == ProfileExchange::Format::ClassTopLandTablesJson)
                message += tr("\n常规课表按全部周次导入；缺失星期补为空课表，科目自动生成，教师为空，不生成时间线。暂不支持非空附加课表。");
            if (format == ProfileExchange::Format::CsesYaml) message += tr("\nCSES 教室字段将被忽略。");
            if (!warnings.isEmpty()) message += QLatin1Char('\n') + warnings.join(QLatin1Char('\n'));
            dialog->message(message);
            QString createdId;
            dialog->submit = [&] {
                candidate.name = name->text().trimmed();
                QString failure;
                if (!ProfileExchange::validateTransfer(candidate, &failure)) return failure;
                ProfileManager::instance().createProfile(candidate, createdId, &failure);
                return failure;
            };
            const bool confirmed = ProfileEditUi::run(dialog);
            dialog->submit = {};
            if (confirmed) {
                refreshProfiles(createdId);
                ElaMessageBar::success(ElaMessageBarType::TopRight, tr("导入成功"), tr("已创建档案“%1”。").arg(candidate.name), 4000, window());
            }
        };
        operation();
    }
    updateActions();
}

void ProfileSettingsTab::exportProfile()
{
    const int index = selectedIndex();
    if (busy_ || index < 0 || !entries_[index].isValid()) return;
    const auto entry = entries_[index];
    {
        QScopedValueRollback<bool> guard(busy_, true);
        updateActions();
        auto operation = [&] {
            QFileDialog picker(this, tr("导出档案"));
            picker.setObjectName(QStringLiteral("exportProfileFile"));
            picker.setAcceptMode(QFileDialog::AcceptSave);
            picker.setFileMode(QFileDialog::AnyFile);
            const QString jsonFilter = tr("JSON 档案 (*.json)");
            const QString csesFilter = tr("CSES 课表 (*.yaml *.yml)");
            const QString tablesFilter = tr("ClassTopLand 课表 (*.json)");
            picker.setNameFilters({jsonFilter, tablesFilter, csesFilter});
            picker.setDefaultSuffix(QStringLiteral("json"));
            picker.selectFile(entry.id + QStringLiteral(".json"));
            connect(&picker, &QFileDialog::filterSelected, &picker, [&picker, csesFilter, tablesFilter](const QString& filter) {
                picker.setDefaultSuffix(filter == csesFilter ? QStringLiteral("yaml") : QStringLiteral("json"));
                if (filter == tablesFilter) { picker.selectFile(picker.directory().filePath(QStringLiteral("tables.json"))); return; }
                const auto files = picker.selectedFiles();
                if (!files.isEmpty()) {
                    const QFileInfo file(files.first());
                    picker.selectFile(file.completeBaseName() + QLatin1Char('.') + picker.defaultSuffix());
                }
            });
            if (picker.exec() != QDialog::Accepted) return;
            QString path = picker.selectedFiles().value(0);
            const bool cses = picker.selectedNameFilter() == csesFilter;
            const bool tables = picker.selectedNameFilter() == tablesFilter;
            if (QFileInfo(path).suffix().isEmpty()) path += cses ? QStringLiteral(".yaml") : QStringLiteral(".json");
            const QString suffix = QFileInfo(path).suffix().toLower();
            if ((!cses && suffix != QStringLiteral("json")) ||
                (cses && suffix != QStringLiteral("yaml") && suffix != QStringLiteral("yml"))) {
                showError(tr("文件扩展名与选中的导出格式不一致。")); return;
            }
            auto& manager = ProfileManager::instance();
            if (manager.isManagedExportPath(path)) {
                showError(tr("不能导出到受管理的档案文件，请选择其他位置。")); return;
            }
            Profile candidate;
            QByteArray contents;
            QStringList warnings;
            QString error;
            const auto format = tables ? ProfileExchange::Format::ClassTopLandTablesJson
                : cses ? ProfileExchange::Format::CsesYaml : ProfileExchange::Format::NativeJson;
            if (!manager.readProfile(entry.id, candidate, &error) ||
                !ProfileExchange::validateTransfer(candidate, &error)) {
                showError(tr("导出失败：%1").arg(error)); return;
            }
            if (tables) {
                if (candidate.schedules.isEmpty()) { showError(tr("没有可导出的周课表。")); return; }
                auto* dialog = new ProfileEditUi::FormDialog(this, tr("导出 ClassTopLand 课表"), tr("导出"));
                auto* weeks = new ElaComboBox(dialog);
                weeks->setObjectName(QStringLiteral("exportTablesWeekSchedule"));
                QHash<QString, int> nameCounts;
                for (const auto& week : candidate.schedules) ++nameCounts[week.name];
                int defaultIndex = 0;
                for (int i = 0; i < candidate.schedules.size(); ++i) {
                    const auto& week = candidate.schedules[i];
                    const QString label = nameCounts.value(week.name) > 1 ? tr("%1（第 %2 项）").arg(week.name).arg(i + 1) : week.name;
                    weeks->addItem(label, week.id);
                    weeks->setItemData(i, week.id, Qt::ToolTipRole);
                    if (week.id == candidate.activeWeekScheduleId) defaultIndex = i;
                }
                weeks->setCurrentIndex(defaultIndex);
                dialog->field(tr("周课表"), weeks);
                auto* details = new ElaScrollArea(dialog);
                details->setWidgetResizable(true);
                details->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
                details->viewport()->setAutoFillBackground(false);
                auto* notice = ProfileEditUi::text({}, details);
                notice->setObjectName(QStringLiteral("exportTablesWarnings"));
                notice->setMargin(8);
                notice->setAlignment(Qt::AlignLeft | Qt::AlignTop);
                details->setWidget(notice);
                notice->setAutoFillBackground(false);
                dialog->field(tr("转换说明"), details);
                details->setFixedSize(440, 220);
                QString conversionError;
                auto preview = [&] {
                    const bool valid = ProfileExchange::serialize(candidate, format, contents, &warnings, &conversionError,
                                                                   weeks->currentData().toString());
                    notice->setText(valid ? warnings.join(QLatin1Char('\n')) : tr("无法导出：%1").arg(conversionError));
                    for (auto* action : dialog->findChildren<ElaPushButton*>())
                        if (action->text() == tr("导出")) action->setEnabled(valid);
                    details->ensureVisible(0, 0);
                };
                const auto previewConnection = connect(weeks, qOverload<int>(&ElaComboBox::currentIndexChanged), dialog, preview);
                preview();
                dialog->submit = [&] { return conversionError; };
                const bool confirmed = ProfileEditUi::run(dialog);
                dialog->submit = {};
                disconnect(previewConnection);
                if (!confirmed) return;
            } else if (!ProfileExchange::serialize(candidate, format, contents, &warnings, &error)) {
                showError(tr("导出失败：%1").arg(error)); return;
            } else if (cses) {
                QString message = tr("将导出整个档案的科目与日课表。\nCSES 不保存档案名称、周课表分组名称和 ID、时间线及当前周课表选择。完整备份请使用 JSON。");
                if (!warnings.isEmpty()) message += QLatin1Char('\n') + warnings.join(QLatin1Char('\n'));
                if (!ProfileEditUi::confirm(this, tr("导出 CSES 课表"), message)) return;
            }
            // Recheck after the confirmation's nested event loop.
            if (manager.isManagedExportPath(path)) { showError(tr("不能覆盖受管理的档案文件。")); return; }
            if (!ProfileExchange::writeFile(path, contents, &error)) { showError(tr("导出失败：%1").arg(error)); return; }
            ElaMessageBar::success(ElaMessageBarType::TopRight, tr("导出成功"), tr("已导出档案“%1”。").arg(candidate.name), 4000, window());
        };
        operation();
    }
    updateActions();
}
