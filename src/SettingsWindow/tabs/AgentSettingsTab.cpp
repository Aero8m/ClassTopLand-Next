#include "AgentSettingsTab.h"

#include "../../Ui/UiWidgets.h"
#include <ElaComboBox.h>
#include <ElaLineEdit.h>
#include <ElaListView.h>
#include <ElaPivot.h>
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaScrollPageArea.h>
#include <ElaSpinBox.h>
#include <ElaTabWidget.h>
#include <ElaText.h>
#include <ElaToggleSwitch.h>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace
{
using namespace Qt::StringLiterals;

ElaListView *list(QWidget *parent, QStandardItemModel *&model, const QString &description)
{
    auto *view = new ElaListView(parent);
    view->setAccessibleName(description);
    view->setTextElideMode(Qt::ElideRight);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    model = new QStandardItemModel(view);
    view->setModel(model);
    return view;
}

void addItem(QStandardItemModel *model, const QString &name, const QString &id, const QString &hint = {})
{
    auto *item = new QStandardItem(name);
    item->setData(id, Qt::UserRole);
    item->setToolTip(hint.isEmpty() ? name : hint);
    model->appendRow(item);
}

QString currentId(const ElaListView *view)
{
    return view->currentIndex().data(Qt::UserRole).toString();
}

void selectId(ElaListView *view, QStandardItemModel *model, const QString &id)
{
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, 0);
        if (index.data(Qt::UserRole).toString() == id) {
            view->setCurrentIndex(index);
            return;
        }
    }
    view->setCurrentIndex({});
}

QVBoxLayout *section(QWidget *parent, const QString &title)
{
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 4, 0, 4);
    layout->addWidget(AppUi::text(title, parent, 16));
    return layout;
}

QString diff(const QString &before, const QString &after)
{
    if (before == after) return u"（无变更）\n"_s + after;
    const auto oldLines = before.isEmpty() ? QStringList() : before.split(u'\n');
    const auto newLines = after.split(u'\n');
    qsizetype start = 0, end = 0;
    const auto limit = std::min(oldLines.size(), newLines.size());
    while (start < limit && oldLines.at(start) == newLines.at(start)) ++start;
    while (end < limit - start &&
           oldLines.at(oldLines.size() - end - 1) == newLines.at(newLines.size() - end - 1)) ++end;
    QStringList lines;
    for (auto i = qMax<qsizetype>(0, start - 3); i < start; ++i) lines.append(u"  "_s + oldLines.at(i));
    for (auto i = start; i < oldLines.size() - end; ++i) lines.append(u"- "_s + oldLines.at(i));
    for (auto i = start; i < newLines.size() - end; ++i) lines.append(u"+ "_s + newLines.at(i));
    for (auto i = newLines.size() - end; i < newLines.size() && i < newLines.size() - end + 3; ++i)
        lines.append(u"  "_s + newLines.at(i));
    return lines.join(u'\n');
}
}

AgentSettingsTab::AgentSettingsTab(AgentService *service, QWidget *parent)
    : ElaScrollPage(parent), service_(service)
{
    auto *layout = AppUi::page(this, u"AI Agent"_s);
    pivot_ = new ElaPivot(this);
    pivot_->appendPivot(u"模型供应商"_s);
    pivot_->appendPivot(u"工作区与历史"_s);
    if (!service_->draftSessions().isEmpty()) pivot_->appendPivot(u"旧草稿恢复"_s);
    pivot_->setPivotSpacing(20);
    layout->addWidget(pivot_);
    pages_ = new QStackedWidget(this);
    pages_->addWidget(makeProviders());
    pages_->addWidget(makeWorkspaces());
    pages_->addWidget(makeDrafts());
    layout->addWidget(pages_, 1);
    connect(pivot_, &ElaPivot::pivotClicked, this, [this](int index) {
        if (index != 0) cancelProviderRequests();
        pages_->setCurrentIndex(index);
        if (index == 2) reloadDrafts();
    });
    connect(service_, &AgentService::busyChanged, this, [this](bool busy) {
        updateControls();
        if (!busy && pages_->currentIndex() == 2) reloadDrafts();
    });
    connect(service_, &AgentService::sessionsChanged, this, [this] {
        if (pages_->currentIndex() == 2 && !service_->busy()) reloadDrafts();
    });
    connect(service_, &AgentService::draftChanged, this, [this] {
        if (pages_->currentIndex() == 2) reloadDrafts();
    });
    connect(service_, &AgentService::storageError, this, [this](const QString &message) {
        showStatus(pages_->currentIndex() == 2 ? draftStatus_ :
                   pages_->currentIndex() == 1 ? workspaceStatus_ : providerStatus_, message);
    });
    reloadProviders();
    reloadWorkspaces();
    reloadDrafts();
    if (!service_->migrationError().isEmpty())
        showStatus(providerStatus_, u"旧连接可继续使用，迁移暂未写入：%1"_s.arg(service_->migrationError()));
}

QWidget *AgentSettingsTab::makeProviders()
{
    auto *page = new QWidget;
    auto *columns = new QHBoxLayout(page);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(14);
    auto *side = new QWidget(page);
    side->setFixedWidth(136);
    auto *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    auto *add = AppUi::button(u"新增供应商"_s, side);
    sideLayout->addWidget(add);
    providers_ = list(side, providersModel_, u"供应商"_s);
    providers_->setMinimumHeight(180);
    sideLayout->addWidget(providers_, 1);
    columns->addWidget(side);

    auto *detail = new QWidget(page);
    auto *form = section(detail, u"供应商配置"_s);
    auto *fields = new QGridLayout;
    fields->setColumnStretch(1, 1);
    preset_ = new ElaComboBox(detail);
    for (const auto &preset : agentPresets()) preset_->addItem(preset.label, preset.id);
    name_ = new ElaLineEdit(detail);
    address_ = new ElaLineEdit(detail);
    key_ = new ElaLineEdit(detail);
    key_->setEchoMode(QLineEdit::Password);
    const QList<QPair<QString, QWidget *>> editors{{u"预设"_s, preset_}, {u"名称"_s, name_},
                                                   {u"Base URL"_s, address_}, {u"API Key"_s, key_}};
    for (int row = 0; row < editors.size(); ++row) {
        auto *label = AppUi::text(editors.at(row).first, detail, 12);
        label->setBuddy(editors.at(row).second);
        fields->addWidget(label, row, 0);
        fields->addWidget(editors.at(row).second, row, 1);
    }
    form->addLayout(fields);
    auto *saveRow = new QHBoxLayout;
    save_ = AppUi::button(u"保存供应商"_s, detail, true);
    removeProvider_ = AppUi::button(u"移除"_s, detail);
    saveRow->addWidget(save_);
    saveRow->addWidget(removeProvider_);
    saveRow->addStretch();
    form->addLayout(saveRow);

    form->addWidget(AppUi::text(u"已配置的模型"_s, detail, 16));
    models_ = list(detail, modelsModel_, u"已配置的模型"_s);
    models_->setMinimumHeight(104);
    form->addWidget(models_, 1);
    auto *modelsRow = new QHBoxLayout;
    auto *addModel = AppUi::button(u"添加"_s, detail);
    auto *editModel = AppUi::button(u"编辑"_s, detail);
    auto *deleteModel = AppUi::button(u"删除"_s, detail);
    auto *makeDefault = AppUi::button(u"设为默认"_s, detail);
    for (auto *button : {addModel, editModel, deleteModel, makeDefault}) {
        button->setMinimumWidth(56);
        modelsRow->addWidget(button);
    }
    modelsRow->addStretch();
    form->addLayout(modelsRow);
    auto *online = new QHBoxLayout;
    fetch_ = AppUi::button(u"获取模型"_s, detail);
    test_ = AppUi::button(u"测试对话"_s, detail);
    testTool_ = AppUi::button(u"测试工具"_s, detail);
    for (auto *button : {fetch_, test_, testTool_}) {
        button->setMinimumWidth(75);
        online->addWidget(button);
    }
    online->addStretch();
    form->addLayout(online);
    providerStatus_ = AppUi::text({}, detail, 12);
    providerStatus_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    form->addWidget(providerStatus_);
    columns->addWidget(detail, 1);

    connect(add, &ElaPushButton::clicked, this, [this] {
        cancelProviderRequests();
        if (!canLeaveProvider()) return;
        QSignalBlocker block(providers_->selectionModel());
        providers_->setCurrentIndex({});
        selectProvider({});
        name_->setFocus();
    });
    connect(providers_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &index) {
        if (loadingProvider_ || !index.isValid()) return;
        if (!canLeaveProvider()) {
            QSignalBlocker block(providers_->selectionModel());
            selectId(providers_, providersModel_, selectedProvider_.id);
            return;
        }
        const auto id = index.data(Qt::UserRole).toString();
        for (const auto &provider : service_->providers())
            if (provider.id == id) { selectProvider(provider); break; }
    });
    connect(preset_, &ElaComboBox::currentIndexChanged, this, [this](int index) {
        if (loadingProvider_ || index < 0) return;
        const auto &preset = agentPresets().at(index);
        address_->setText(preset.baseUrl);
        if (selectedProvider_.id.isEmpty()) name_->setText(preset.label);
        key_->clear();
        updateControls();
    });
    connect(save_, &ElaPushButton::clicked, this, [this] {
        auto provider = editedProvider();
        QString error;
        if (!service_->saveProvider(provider, key_->text().trimmed(), &error)) {
            showStatus(providerStatus_, error);
            return;
        }
        reloadProviders(provider.id);
        showStatus(providerStatus_, u"供应商已保存"_s);
    });
    connect(removeProvider_, &ElaPushButton::clicked, this, [this] {
        if (!AppUi::confirm(this, u"移除供应商"_s, u"移除该供应商和它的 API Key？会话历史会保留。"_s)) return;
        QString error;
        if (service_->removeProvider(selectedProvider_.id, &error)) reloadProviders();
        else showStatus(providerStatus_, error);
    });
    connect(addModel, &ElaPushButton::clicked, this, [this] { this->editModel(false); });
    connect(editModel, &ElaPushButton::clicked, this, [this] { this->editModel(true); });
    connect(models_, &ElaListView::doubleClicked, this, [this] { this->editModel(true); });
    connect(deleteModel, &ElaPushButton::clicked, this, [this] {
        const auto id = currentId(models_);
        if (id.isEmpty() || !AppUi::confirm(this, u"删除模型"_s, u"保存供应商后，使用该模型的旧会话需要重新选择模型。"_s)) return;
        editableModels_.removeIf([&](const AgentModelEntry &model) { return model.id == id; });
        reloadModels();
    });
    connect(makeDefault, &ElaPushButton::clicked, this, [this] {
        QString error;
        if (service_->setDefaultModel(selectedProvider_.id, currentId(models_), &error)) {
            reloadModels(currentId(models_));
            showStatus(providerStatus_, u"默认模型已更新"_s);
        } else showStatus(providerStatus_, error);
    });
    connect(fetch_, &ElaPushButton::clicked, this, [this] {
        const auto provider = editedProvider();
        const auto key = key_->text().trimmed();
        if (key.contains(u'\n') || key.contains(u'\r') || key.contains(QChar::Null)) {
            showStatus(providerStatus_, u"API Key 含无效字符"_s);
            return;
        }
        fetching_ = true;
        updateControls();
        showStatus(providerStatus_, u"正在获取模型列表…"_s);
        catalog_.fetch(provider, key.isEmpty() ? service_->storedKey(provider.connection()) : key);
    });
    connect(&catalog_, &AgentCatalog::fetched, this, &AgentSettingsTab::showCandidates);
    const auto test = [this](bool tools) {
        QString error;
        const auto connection = editedProvider().connection(currentId(models_));
        testing_ = service_->testConnection(connection, key_->text().trimmed(), tools, &error);
        showStatus(providerStatus_, testing_ ? u"正在测试…"_s : error);
        updateControls();
    };
    connect(test_, &ElaPushButton::clicked, this, [test] { test(false); });
    connect(testTool_, &ElaPushButton::clicked, this, [test] { test(true); });
    connect(service_, &AgentService::testFinished, this, [this](const QString &error) {
        testing_ = false;
        showStatus(providerStatus_, error.isEmpty() ? u"测试成功"_s : error);
        updateControls();
    });
    connect(models_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { updateControls(); });
    return page;
}

AgentProvider AgentSettingsTab::editedProvider() const
{
    return {selectedProvider_.id, name_->text().trimmed(), preset_->currentData().toString(),
            address_->text().trimmed(), editableModels_};
}

bool AgentSettingsTab::canLeaveProvider()
{
    const auto current = editedProvider();
    return (current.toJson() == selectedProvider_.toJson() && key_->text().isEmpty()) ||
        AppUi::confirm(this, u"放弃未保存的供应商配置？"_s, u"当前修改尚未保存。"_s);
}

void AgentSettingsTab::reloadProviders(const QString &selected)
{
    const QScopedValueRollback guard(loadingProvider_, true);
    providersModel_->clear();
    const auto entries = service_->providers();
    for (const auto &provider : entries) addItem(providersModel_, provider.name, provider.id, provider.baseUrl);
    const auto it = std::find_if(entries.cbegin(), entries.cend(), [&](const AgentProvider &item) { return item.id == selected; });
    if (it != entries.cend()) selectProvider(*it);
    else selectProvider(entries.isEmpty() ? AgentProvider() : entries.first());
    selectId(providers_, providersModel_, selectedProvider_.id);
}

void AgentSettingsTab::selectProvider(const AgentProvider &provider)
{
    const QScopedValueRollback guard(loadingProvider_, true);
    catalog_.cancel();
    service_->cancelTest();
    fetching_ = testing_ = false;
    selectedProvider_ = provider;
    if (provider.id.isEmpty()) {
        const auto &preset = agentPresets().first();
        selectedProvider_ = {{}, preset.label, preset.id, preset.baseUrl, {}};
    }
    preset_->setCurrentIndex(qMax(0, preset_->findData(selectedProvider_.preset)));
    name_->setText(selectedProvider_.name);
    address_->setText(selectedProvider_.baseUrl);
    key_->clear();
    key_->setPlaceholderText(service_->storedKey(selectedProvider_.connection()).isEmpty()
        ? u"输入 API Key"_s : u"已保存，留空继续使用"_s);
    editableModels_ = selectedProvider_.models;
    reloadModels();
    showStatus(providerStatus_, {});
}

void AgentSettingsTab::reloadModels(const QString &selected)
{
    modelsModel_->clear();
    const auto defaultModel = service_->defaultModel();
    for (const auto &model : std::as_const(editableModels_)) {
        const bool isDefault = defaultModel.id == selectedProvider_.id && defaultModel.model == model.id;
        addItem(modelsModel_, (model.name.isEmpty() ? model.id : model.name) + (isDefault ? u" · 默认"_s : QString()),
                model.id, model.id + (model.contextWindow ? u" · 上下文 %1"_s.arg(model.contextWindow) : QString()));
    }
    if (selected.isEmpty() && modelsModel_->rowCount() > 0) models_->setCurrentIndex(modelsModel_->index(0, 0));
    else selectId(models_, modelsModel_, selected);
    updateControls();
}

void AgentSettingsTab::editModel(bool existing)
{
    const auto current = currentId(models_);
    const auto it = std::find_if(editableModels_.begin(), editableModels_.end(),
                                 [&](const AgentModelEntry &model) { return model.id == current; });
    if (existing && it == editableModels_.end()) return;
    const auto model = existing ? *it : AgentModelEntry{};
    auto *dialog = new AppUi::FormDialog(this, existing ? u"编辑模型"_s : u"添加模型"_s);
    auto *id = dialog->line(u"模型 ID"_s, model.id);
    auto *name = dialog->line(u"显示名称"_s, model.name);
    auto *window = new ElaSpinBox(dialog);
    window->setRange(0, 100000000);
    window->setValue(model.contextWindow);
    dialog->field(u"上下文窗口（0 表示未知）"_s, window);
    dialog->submit = [this, existing, current, id, name, window] {
        const auto wireId = id->text().trimmed();
        if (wireId.isEmpty() || wireId.size() > 200) return u"请填写有效的模型 ID"_s;
        for (const auto &entry : std::as_const(editableModels_))
            if (entry.id == wireId && (!existing || entry.id != current)) return u"模型 ID 已存在"_s;
        const AgentModelEntry value{wireId, name->text().trimmed(), window->value()};
        if (!existing) editableModels_.append(value);
        else for (auto &entry : editableModels_)
            if (entry.id == current) { entry = value; break; }
        reloadModels(wireId);
        showStatus(providerStatus_, u"保存供应商后应用模型修改"_s);
        return QString();
    };
    AppUi::run(dialog);
}

void AgentSettingsTab::showCandidates(const QList<AgentModelEntry> &models, bool catalogOnly, const QString &message)
{
    fetching_ = false;
    updateControls();
    if (models.isEmpty()) {
        showStatus(providerStatus_, message.isEmpty() ? u"暂无候选模型，可手动添加"_s : message + u"，可手动添加"_s);
        return;
    }
    auto *dialog = new AppUi::FormDialog(this, catalogOnly ? u"目录候选"_s : u"供应商可用模型"_s, u"添加所选"_s);
    if (catalogOnly)
        dialog->message(message + u"。以下来自 models.dev，可用性以连接测试为准。"_s);
    QStandardItemModel *rows;
    auto *view = list(dialog, rows, u"候选模型"_s);
    view->setMinimumHeight(170);
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (const auto &model : models) {
        if (std::any_of(editableModels_.cbegin(), editableModels_.cend(),
                        [&](const AgentModelEntry &saved) { return saved.id == model.id; })) continue;
        addItem(rows, model.name == model.id ? model.id : u"%1 · %2"_s.arg(model.name, model.id), model.id);
    }
    if (!rows->rowCount()) dialog->message(u"所有返回的模型都已添加。"_s);
    dialog->field(u"选择要添加的模型（按住 Ctrl 可多选）"_s, view);
    dialog->submit = [this, rows, view, models] {
        QString selected;
        for (int row = 0; row < rows->rowCount(); ++row) {
            if (!view->selectionModel()->isSelected(rows->index(row, 0))) continue;
            const auto id = rows->item(row)->data(Qt::UserRole).toString();
            for (const auto &model : models)
                if (model.id == id) { editableModels_.append(model); selected = id; break; }
        }
        if (selected.isEmpty() && rows->rowCount()) return u"请先选择模型"_s;
        reloadModels(selected);
        if (!selected.isEmpty()) showStatus(providerStatus_, u"保存供应商后应用模型修改"_s);
        return QString();
    };
    AppUi::run(dialog);
}

QWidget *AgentSettingsTab::makeWorkspaces()
{
    auto *page = new QWidget;
    auto *layout = section(page, u"工作区"_s);
    layout->addWidget(AppUi::text(u"会话可从这里配置的目录中选择工作区，新会话默认使用默认工作区（首次为用户目录）。读取内容、写入内容和命令输出会发送给该会话的模型。"_s, page, 12));
    workspaces_ = list(page, workspacesModel_, u"工作区目录"_s);
    workspaces_->setMinimumHeight(132);
    layout->addWidget(workspaces_);
    auto *buttons = new QHBoxLayout;
    auto *add = AppUi::button(u"添加目录"_s, page);
    auto *makeDefault = AppUi::button(u"设为默认"_s, page);
    auto *remove = AppUi::button(u"移除目录"_s, page);
    for (auto *button : {add, makeDefault, remove}) buttons->addWidget(button);
    buttons->addStretch();
    layout->addLayout(buttons);
    workspaceStatus_ = AppUi::text({}, page, 12);
    layout->addWidget(workspaceStatus_);
    layout->addWidget(AppUi::text(u"会话历史"_s, page, 16));
    auto *historyRow = new QHBoxLayout;
    historyRow->addWidget(AppUi::text(u"将会话历史保存到本机"_s, page, 13));
    auto *history = new ElaToggleSwitch(page);
    history->setIsToggled(service_->saveHistory());
    historyRow->addWidget(history);
    historyRow->addStretch();
    layout->addLayout(historyRow);
    auto *clear = AppUi::button(u"清除本机会话历史"_s, page);
    layout->addWidget(clear, 0, Qt::AlignLeft);
    layout->addStretch();
    connect(add, &ElaPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, u"添加工作区"_s);
        if (path.isEmpty()) return;
        const auto canonical = QFileInfo(path).canonicalFilePath();
        auto paths = service_->workspaces();
        if (canonical.isEmpty() || std::any_of(paths.cbegin(), paths.cend(), [&](const QString &item) {
                return item.compare(canonical, Qt::CaseInsensitive) == 0;
            })) {
            showStatus(workspaceStatus_, u"目录不可用或已添加"_s);
            return;
        }
        paths.append(canonical);
        QString error;
        if (service_->setWorkspaces(paths, service_->defaultWorkspace(), &error)) {
            reloadWorkspaces();
            selectId(workspaces_, workspacesModel_, canonical);
        }
        showStatus(workspaceStatus_, error);
    });
    connect(makeDefault, &ElaPushButton::clicked, this, [this] {
        const auto path = currentId(workspaces_);
        if (path.isEmpty()) return;
        QString error;
        if (service_->setWorkspaces(service_->workspaces(), path, &error)) reloadWorkspaces();
        showStatus(workspaceStatus_, error);
    });
    connect(remove, &ElaPushButton::clicked, this, [this] {
        const auto path = currentId(workspaces_);
        auto paths = service_->workspaces();
        if (path.isEmpty() || !paths.removeAll(path)) return;
        QString error;
        const auto previous = service_->defaultWorkspace();
        if (service_->setWorkspaces(paths, previous == path ? QString() : previous, &error)) reloadWorkspaces();
        showStatus(workspaceStatus_, error);
    });
    connect(history, &ElaToggleSwitch::toggled, this, [this, history](bool enabled) {
        QString error;
        if (!service_->setSaveHistory(enabled, &error)) {
            QSignalBlocker block(history);
            history->setIsToggled(service_->saveHistory());
        }
        showStatus(workspaceStatus_, error);
    });
    connect(clear, &ElaPushButton::clicked, this, [this] {
        if (service_->busy()) {
            showStatus(workspaceStatus_, u"请先结束当前任务"_s);
        } else if (AppUi::confirm(this, u"清除会话历史"_s, u"清除所有本机会话记录？插件草稿保留。"_s)) {
            showStatus(workspaceStatus_, {});
            service_->clearHistory();
        }
    });
    return page;
}

void AgentSettingsTab::reloadWorkspaces()
{
    const auto selected = currentId(workspaces_);
    workspacesModel_->clear();
    const auto defaultPath = service_->defaultWorkspace();
    for (const auto &path : service_->workspaces())
        addItem(workspacesModel_, QDir::toNativeSeparators(path) + (path == defaultPath ? u" · 默认"_s : QString()), path);
    selectId(workspaces_, workspacesModel_, selected);
}

QWidget *AgentSettingsTab::makeDrafts()
{
    auto *page = new QWidget;
    auto *layout = section(page, u"旧草稿恢复"_s);
    layout->addWidget(AppUi::text(u"插件现已直接编辑源文件。本页仅用于将旧草稿恢复到插件源目录；恢复前自动备份，不自动启用。"_s, page, 13));
    auto *row = new QHBoxLayout;
    drafts_ = list(page, draftsModel_, u"插件草稿列表"_s);
    drafts_->setMinimumHeight(92);
    row->addWidget(drafts_, 1);
    layout->addLayout(row);
    draftStatus_ = AppUi::text({}, page, 13);
    draftStatus_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(draftStatus_);
    auto *tabs = new ElaTabWidget(page);
    const auto makeDiff = [tabs](const QString &title) {
        auto *text = new ElaPlainTextEdit(tabs);
        text->setReadOnly(true);
        text->setLineWrapMode(QPlainTextEdit::NoWrap);
        tabs->addTab(text, title);
        return text;
    };
    manifestDiff_ = makeDiff(u"manifest.json"_s);
    scriptDiff_ = makeDiff(u"main.mjs"_s);
    permissionDiff_ = makeDiff(u"权限"_s);
    tabs->setMinimumHeight(240);
    layout->addWidget(tabs, 1);
    publish_ = AppUi::button(u"恢复旧草稿到源文件"_s, page, true);
    layout->addWidget(publish_, 0, Qt::AlignRight);
    connect(drafts_->selectionModel(), &QItemSelectionModel::currentChanged, this, &AgentSettingsTab::showDraft);
    connect(publish_, &ElaPushButton::clicked, this, [this] {
        QString error;
        if (!AppUi::confirm(this, u"恢复旧草稿"_s, u"已显示代码差异。恢复将直接修改源文件，并先创建回滚检查点；保留其他资源和私有数据。"_s)) return;
        if (service_->recoverDraft(currentId(drafts_), &error)) reloadDrafts();
        showStatus(draftStatus_, error.isEmpty() ? u"源文件已恢复，可在对话中选择插件继续修改。"_s : error);
    });
    return page;
}

void AgentSettingsTab::openDraft(const QString &session)
{
    cancelProviderRequests();
    pivot_->setCurrentIndex(2);
    pages_->setCurrentIndex(2);
    reloadDrafts(session);
}

void AgentSettingsTab::openProviders()
{
    pivot_->setCurrentIndex(0);
    pages_->setCurrentIndex(0);
}

void AgentSettingsTab::hideEvent(QHideEvent *event)
{
    cancelProviderRequests();
    ElaScrollPage::hideEvent(event);
}

void AgentSettingsTab::cancelProviderRequests()
{
    if (!fetching_ && !testing_) return;
    catalog_.cancel();
    service_->cancelTest();
    fetching_ = testing_ = false;
    showStatus(providerStatus_, u"模型请求已取消"_s);
    updateControls();
}

void AgentSettingsTab::reloadDrafts(const QString &selected)
{
    const auto current = selected.isEmpty() ? currentId(drafts_) : selected;
    const QSignalBlocker guard(drafts_->selectionModel());
    draftsModel_->clear();
    for (const auto &id : service_->draftSessions()) {
        const auto *item = service_->session(id);
        const auto name = service_->draftName(id);
        addItem(draftsModel_, item ? name + u" · "_s + item->title : name, id);
    }
    selectId(drafts_, draftsModel_, current);
    if (!drafts_->currentIndex().isValid() && draftsModel_->rowCount())
        drafts_->setCurrentIndex(draftsModel_->index(0, 0));
    showDraft();
}

void AgentSettingsTab::showDraft()
{
    QString error;
    const auto id = currentId(drafts_);
    review_ = id.isEmpty() ? AgentService::Review{} : service_->review(id, &error);
    manifestDiff_->setPlainText(id.isEmpty() ? QString() : diff(review_.beforeManifest, review_.manifest));
    scriptDiff_->setPlainText(id.isEmpty() ? QString() : diff(review_.beforeScript, review_.script));
    permissionDiff_->setPlainText(id.isEmpty() ? QString() : diff(
        QString::fromUtf8(QJsonDocument(review_.beforePermissions).toJson()),
        QString::fromUtf8(QJsonDocument(review_.package.manifest.permissions).toJson())));
    publish_->setText(u"恢复旧草稿到源文件"_s);
    if (error.isEmpty()) {
        if (id.isEmpty()) error = u"没有待恢复的旧草稿。直接在对话中选择插件进行修改。"_s;
        else if (!service_->canPublish(id)) error = u"当前任务或权限状态下只可审阅草稿。"_s;
        else error = u"请审阅差异；恢复前将备份当前源文件，原草稿保留。"_s;
    }
    showStatus(draftStatus_, error);
    updateControls();
}

void AgentSettingsTab::showStatus(ElaText *label, const QString &message)
{
    label->setText(message);
    label->setVisible(!message.isEmpty());
}

void AgentSettingsTab::updateControls()
{
    const bool busy = service_->busy();
    save_->setEnabled(!busy && !fetching_ && !testing_);
    removeProvider_->setEnabled(!busy && !fetching_ && !testing_ && !selectedProvider_.id.isEmpty());
    fetch_->setEnabled(!fetching_ && !testing_);
    const bool canTest = !fetching_ && !testing_ && !currentId(models_).isEmpty();
    test_->setEnabled(canTest);
    testTool_->setEnabled(canTest);
    for (auto *field : std::initializer_list<QWidget *>{preset_, name_, address_, key_, models_, providers_})
        field->setEnabled(!fetching_ && !testing_);
    if (pages_ && pages_->count() == 3) {
        publish_->setEnabled(!busy && !publishing_ && !currentId(drafts_).isEmpty() && service_->canPublish(currentId(drafts_)));
    }
}

