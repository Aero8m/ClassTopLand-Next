#include "CloudSettingsTab.h"

#include "../../Core/CloudSync/CloudEndpoint.h"
#include "../../Core/CloudSync/CloudSyncService.h"
#include "../../Ui/UiWidgets.h"

#include <ElaLineEdit.h>
#include <ElaPushButton.h>
#include <ElaText.h>
#include <ElaToggleSwitch.h>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QScopedValueRollback>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStringList>
#include <QVBoxLayout>

CloudSettingsTab::CloudSettingsTab(CloudSyncService* cloud, QWidget* parent)
    : ElaScrollPage(parent), cloud_(cloud)
{
    using namespace AppUi;
    setObjectName(QStringLiteral("cloudSettingsTab"));
    auto* layout = page(this, tr("集控"), 24);
    const auto row = [this](QVBoxLayout* body, const QString& title, QWidget* editor,
                            const QString& name) {
        auto* line = new QHBoxLayout;
        line->setSpacing(16);
        line->addWidget(text(title, this), 1);
        editor->setObjectName(name);
        editor->setAccessibleName(title);
        line->addWidget(editor);
        body->addLayout(line);
    };

    auto* server = section(layout, tr("服务器与配对"));
    auto* originBox = new QWidget(this);
    auto* originLayout = new QHBoxLayout(originBox);
    originLayout->setContentsMargins(0, 0, 0, 0);
    originLayout->setSpacing(8);
    origin_ = new ElaLineEdit(originBox);
    origin_->setFixedWidth(320);
    origin_->setPlaceholderText(QString::fromLatin1(Cloud::kDefaultOrigin));
    originSave_ = button(tr("保存"), originBox);
    originLayout->addWidget(origin_);
    originLayout->addWidget(originSave_);
    row(server, tr("服务器地址"), originBox, QStringLiteral("cloudOriginBox"));

    enable_ = new ElaToggleSwitch(this);
    row(server, tr("自动同步"), enable_, QStringLiteral("cloudEnabled"));

    auto* codeBox = new QWidget(this);
    auto* codeLayout = new QHBoxLayout(codeBox);
    codeLayout->setContentsMargins(0, 0, 0, 0);
    codeLayout->setSpacing(8);
    code_ = new ElaLineEdit(codeBox);
    code_->setFixedWidth(220);
    code_->setMaxLength(12);
    code_->setPlaceholderText(tr("12 位配对码"));
    pair_ = button(tr("配对"), codeBox);
    primary(pair_);
    codeLayout->addWidget(code_);
    codeLayout->addWidget(pair_);
    row(server, tr("配对码"), codeBox, QStringLiteral("cloudPairBox"));
    auto* serverHint = text(
        tr("配对码由管理员在集控网页生成，有效期 10 分钟。配对成功后会自动创建云端档案并切换为当前档案（需重启一次）。"),
        this, 12);
    serverHint->setWordWrap(true);
    server->addWidget(serverHint);

    auto* statusCard = section(layout, tr("同步状态"));
    status_ = text(tr("未配对"), this, 16);
    statusCard->addWidget(status_);
    detail_ = text(QString(), this, 12);
    detail_->setWordWrap(true);
    statusCard->addWidget(detail_);
    auto* profileLine = new QHBoxLayout;
    profile_ = text(QString(), this, 12);
    profile_->setWordWrap(true);
    switch_ = button(tr("切换为当前档案"), this);
    profileLine->addWidget(profile_, 1);
    profileLine->addWidget(switch_);
    statusCard->addLayout(profileLine);
    auto* syncLine = new QHBoxLayout;
    syncNow_ = button(tr("立即同步"), this, true);
    syncLine->addWidget(syncNow_);
    syncLine->addStretch();
    statusCard->addLayout(syncLine);

    auto* management = section(layout, tr("设备管理"));
    auto* managementLine = new QHBoxLayout;
    unbind_ = button(tr("解除绑定"), this);
    reset_ = button(tr("重置集控状态"), this);
    managementLine->addWidget(unbind_);
    managementLine->addWidget(reset_);
    managementLine->addStretch();
    management->addLayout(managementLine);
    reset_->hide();

    const auto addHint = [this, layout](const QString& value) {
        auto* hint = text(value, this, 12);
        hint->setWordWrap(true);
        layout->addWidget(hint);
    };
    addHint(tr("设备凭证只保存在本机 cloud/state.json，不会写入主配置或导出档案；非本机服务器必须使用 https://，"
               "本机联调可使用 http://127.0.0.1。"));
    addHint(tr("云端只管理课表档案、课程条组件、外观和桌面倒计时。未保存的档案编辑、插件课程条组件会让对应通道"
               "显示为阻塞，处理后会以新的尝试自动重试。"));
    layout->addStretch();

    connect(originSave_, &ElaPushButton::clicked, this, &CloudSettingsTab::saveOrigin);
    connect(origin_, &QLineEdit::returnPressed, this, &CloudSettingsTab::saveOrigin);
    connect(pair_, &ElaPushButton::clicked, this, &CloudSettingsTab::pair);
    connect(code_, &QLineEdit::returnPressed, this, &CloudSettingsTab::pair);
    connect(origin_, &QLineEdit::textEdited, this, [this] { originEdited_ = true; });
    connect(switch_, &ElaPushButton::clicked, this, &CloudSettingsTab::switchProfile);
    connect(unbind_, &ElaPushButton::clicked, this, &CloudSettingsTab::unbind);
    connect(reset_, &ElaPushButton::clicked, this, &CloudSettingsTab::resetState);
    connect(enable_, &ElaToggleSwitch::toggled, this, [this](bool checked) {
        if (!refreshing_ && cloud_) cloud_->setEnabled(checked);
    });
    connect(syncNow_, &ElaPushButton::clicked, this, [this] {
        if (cloud_) cloud_->syncNow();
    });
    if (cloud_) {
        connect(cloud_, &CloudSyncService::stateChanged, this, &CloudSettingsTab::refresh);
        connect(cloud_, &CloudSyncService::pairingFinished, this,
                [this](bool ok, const QString& message) {
                    if (ok) {
                        code_->clear();
                        refresh();
                    } else {
                        AppUi::error(this, tr("配对失败：%1").arg(message));
                    }
                });
        connect(cloud_, &CloudSyncService::syncFinished, this,
                [this](bool, const QString&) { refresh(); });
    }
    refresh();
}

QString CloudSettingsTab::statusText(int status) const
{
    switch (static_cast<Cloud::Status>(status)) {
    case Cloud::Status::Unbound: return tr("未配对");
    case Cloud::Status::Pairing: return tr("配对中…");
    case Cloud::Status::Connected: return tr("已连接");
    case Cloud::Status::Paused: return tr("已暂停自动同步");
    case Cloud::Status::Syncing: return tr("同步中…");
    case Cloud::Status::Revoked: return tr("凭证已撤销，需要重新配对");
    case Cloud::Status::Corrupt: return tr("状态文件损坏，需要重置");
    case Cloud::Status::Locked: return tr("另一实例正在同步");
    case Cloud::Status::Error: return tr("同步异常");
    }
    return tr("未知状态");
}

void CloudSettingsTab::refresh()
{
    if (!cloud_) {
        status_->setText(tr("集控服务不可用"));
        return;
    }
    QScopedValueRollback<bool> guard(refreshing_, true);
    const Cloud::Snapshot snap = cloud_->snapshot();
    if (!originEdited_ && !origin_->hasFocus()) origin_->setText(snap.origin);
    const bool bound = !snap.deviceId.isEmpty();
    const bool unavailable = snap.status == Cloud::Status::Corrupt ||
                             snap.status == Cloud::Status::Locked;
    origin_->setEnabled(!bound && !snap.busy);
    originSave_->setEnabled(!bound && !snap.busy);
    {
        const QSignalBlocker blocker(enable_);
        enable_->setIsToggled(snap.enabled);
    }
    enable_->setEnabled(bound && !unavailable);
    code_->setEnabled(!snap.busy && !unavailable);
    pair_->setEnabled(!snap.busy && !unavailable);
    syncNow_->setEnabled(bound && !snap.busy && !unavailable);
    unbind_->setEnabled(snap.hasCredential || bound);
    reset_->setVisible(snap.status == Cloud::Status::Corrupt);
    status_->setText(statusText(static_cast<int>(snap.status)));

    QStringList lines;
    if (bound) {
        const QString name = snap.deviceName.isEmpty() ? snap.deviceId : snap.deviceName;
        lines << tr("设备：%1（%2）").arg(name, snap.deviceId);
    } else {
        lines << tr("尚未绑定设备");
    }
    lines << (snap.lastSyncAt.isValid()
                  ? tr("最后同步：%1").arg(snap.lastSyncAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                  : tr("最后同步：尚未同步"));
    const auto channelLine = [&snap](const QString& key, const QString& label) {
        const Cloud::ChannelSnapshot channel = snap.channels.value(key);
        if (channel.appliedGeneration <= 0 && channel.result.isEmpty())
            return tr("%1：尚未应用云端内容").arg(label);
        QString text = tr("%1：已应用代次 %2").arg(label).arg(channel.appliedGeneration);
        if (!channel.result.isEmpty()) {
            QString result = channel.result;
            if (result == QStringLiteral("applied")) result = tr("成功");
            else if (result == QStringLiteral("blocked")) result = tr("被阻塞");
            else if (result == QStringLiteral("failed")) result = tr("失败");
            text += tr("；最近结果 %1").arg(result);
            if (!channel.reason.isEmpty()) text += tr("（%1）").arg(channel.reason);
        }
        if (channel.at.isValid())
            text += tr("，%1").arg(channel.at.toString(QStringLiteral("MM-dd HH:mm")));
        return text;
    };
    lines << channelLine(Cloud::profileKey(), tr("课表档案"));
    lines << channelLine(Cloud::displayKey(), tr("展示配置"));
    if (!snap.lastError.isEmpty()) lines << tr("最近错误：%1").arg(snap.lastError);
    detail_->setText(lines.join(QLatin1Char('\n')));

    if (snap.cloudProfileId.isEmpty()) {
        profile_->setText(tr("云端档案：尚未创建"));
    } else {
        const QString name = snap.cloudProfileName.isEmpty() ? tr("云端档案") : snap.cloudProfileName;
        profile_->setText(snap.cloudProfileCurrent
                              ? tr("云端档案：%1（当前档案）").arg(name)
                              : tr("云端档案：%1（未使用）").arg(name));
    }
    switch_->setVisible(!snap.cloudProfileId.isEmpty() && !snap.cloudProfileCurrent);
    switch_->setEnabled(!snap.busy);
}

void CloudSettingsTab::pair()
{
    if (!cloud_) return;
    QString code = code_->text().trimmed();
    code.remove(QLatin1Char(' '));
    code = code.toUpper();
    code_->setText(code);
    if (code.size() != 12) {
        AppUi::error(this, tr("请输入 12 位配对码"));
        return;
    }
    const Cloud::Snapshot snap = cloud_->snapshot();
    if (!snap.deviceId.isEmpty() &&
        !AppUi::confirm(this, tr("重新配对"),
                        tr("当前设备已绑定。使用配对码重新绑定会清除旧的云端代次和待发回执，"
                           "已应用的本地内容会保留。确定继续？")))
        return;
    cloud_->pair(code);
}

void CloudSettingsTab::saveOrigin()
{
    if (!cloud_) return;
    QString error;
    if (!cloud_->setOrigin(origin_->text(), &error)) {
        AppUi::error(this, tr("服务器地址保存失败：%1").arg(error));
        return;
    }
    originEdited_ = false;
    refresh();
}

void CloudSettingsTab::switchProfile()
{
    if (!cloud_) return;
    const Cloud::Snapshot snap = cloud_->snapshot();
    if (snap.cloudProfileId.isEmpty()) return;
    emit profileSwitchRequested(snap.cloudProfileId);
}

void CloudSettingsTab::unbind()
{
    if (!cloud_) return;
    if (!AppUi::confirm(this, tr("解除绑定"),
                        tr("解除绑定会删除本机的设备凭证和同步状态，服务器上的设备仍需管理员撤销。"
                           "云端档案和已应用的内容会保留。确定继续？")))
        return;
    cloud_->unbind();
    refresh();
}

void CloudSettingsTab::resetState()
{
    if (!cloud_) return;
    if (!AppUi::confirm(this, tr("重置集控状态"),
                        tr("损坏的状态文件会被备份，随后需要重新配对。已应用的云端档案内容不受影响。"
                           "确定继续？")))
        return;
    QString error;
    if (!cloud_->resetState(&error)) AppUi::error(this, tr("重置失败：%1").arg(error));
    refresh();
}

void CloudSettingsTab::showEvent(QShowEvent* event)
{
    ElaScrollPage::showEvent(event);
    originEdited_ = false;
    refresh();
}
