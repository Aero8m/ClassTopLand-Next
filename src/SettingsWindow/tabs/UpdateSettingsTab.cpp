#include "UpdateSettingsTab.h"

#include "../../Core/CloudSync/CloudOperations.h"
#include "../../Core/ConfigManager/ConfigManager.h"
#include "../../Core/Update/UpdateService.h"
#include "../../Ui/SettingsLayout.h"
#include "../../Ui/UiWidgets.h"

#include <ElaMessageBar.h>
#include <ElaPlainTextEdit.h>
#include <ElaProgressBar.h>
#include <ElaPushButton.h>
#include <ElaText.h>
#include <ElaToggleSwitch.h>
#include <QDesktopServices>
#include <QSignalBlocker>
#include <QUrl>
#include <QVBoxLayout>

UpdateSettingsTab::UpdateSettingsTab(UpdateService* update, QWidget* parent) : ElaScrollPage(parent)
{
    using namespace AppUi;
    setObjectName(QStringLiteral("updateSettingsTab"));
    auto* layout = page(this, tr("更新"), 24);
    const bool installed = update->supported();
    auto* version = card(layout, installed ? tr("已安装版本") : tr("当前运行版本"));
    const int build = UpdateService::currentBuild();
    const QString buildId = CloudOperations::buildId();
    const QString versionText = build > 0
        ? tr("Autobuild 构建 #%1 · Git 提交 %2").arg(build).arg(buildId)
        : (buildId == QStringLiteral("local") ? tr("本地构建") : tr("本地构建（%1）").arg(buildId));
    auto* versionLabel = text(versionText, this);
    versionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    version->addWidget(versionLabel);

    if (!installed) {
        version->addWidget(text(tr("应用内更新适用于通过 Autobuild 安装的 Windows x64 版本。"), this));
        auto* mirror = button(tr("前往 Autobuild 下载"), this);
        version->addWidget(mirror, 0, Qt::AlignLeft);
        connect(mirror, &ElaPushButton::clicked, this, [] { QDesktopServices::openUrl(UpdateService::mirrorPage()); });
        layout->addStretch();
        return;
    }

    auto* status = text({}, this);
    status->setObjectName(QStringLiteral("updateStatus"));
    status->setTextFormat(Qt::PlainText);
    version->addWidget(status);

    auto* progress = new ElaProgressBar(this);
    progress->setObjectName(QStringLiteral("updateProgress"));
    version->addWidget(progress);

    auto* actions = new SettingsActions(this);
    auto* check = button(tr("检查更新"), actions);
    auto* install = button(tr("下载并安装"), actions);
    auto* cancel = button(tr("取消下载"), actions);
    actions->insertAction(check);
    actions->insertAction(install);
    actions->setPrimary(install, true);
    actions->insertAction(cancel);
    version->addWidget(actions);

    auto* automatic = new ElaToggleSwitch(this);
    automatic->setObjectName(QStringLiteral("automaticUpdates"));
    automatic->setAccessibleName(tr("自动更新"));
    auto* preference = new SettingsRow(this);
    preference->setTitle(tr("自动更新"));
    preference->setDescription(tr("后台检查并下载新版本，在通过托盘退出时安装。Windows 可能需要管理员授权。"));
    preference->attachEditor(automatic);
    card(layout)->addWidget(preference);

    auto* releaseNotes = new SettingsSection(this);
    releaseNotes->setTitle(tr("更新日志"));
    auto* notes = new ElaPlainTextEdit(releaseNotes);
    notes->setReadOnly(true);
    notes->setAccessibleName(tr("更新日志"));
    notes->setFixedHeight(144);
    releaseNotes->bodyLayout()->addWidget(notes);
    layout->addWidget(releaseNotes);
    layout->addStretch();

    connect(check, &ElaPushButton::clicked, update, &UpdateService::check);
    connect(install, &ElaPushButton::clicked, update, &UpdateService::downloadAndInstall);
    connect(cancel, &ElaPushButton::clicked, update, &UpdateService::cancelDownload);
    connect(automatic, &ElaToggleSwitch::toggled, this, [this, automatic, update](bool enabled) {
        auto& manager = ConfigManager::instance();
        Config candidate = manager.config();
        candidate.automaticUpdates = enabled;
        QString error;
        if (!manager.commit(candidate, &error)) {
            const QSignalBlocker blocker(automatic);
            automatic->setIsToggled(update->automaticUpdates());
            ElaMessageBar::error(ElaMessageBarType::TopRight, tr("自动更新设置保存失败"), error, 5000, window());
        }
    });

    const auto render = [=] {
        using State = UpdateService::State;
        const auto state = update->state();
        const bool downloading = state == State::Downloading;
        status->setText(update->message().isEmpty() ? tr("可以手动检查 Autobuild 上的新版本。") : update->message());
        releaseNotes->setVisible(!update->notes().trimmed().isEmpty());
        if (notes->toPlainText() != update->notes()) notes->setPlainText(update->notes());
        progress->setValue(update->percent());
        progress->setVisible(downloading);
        cancel->setVisible(downloading);
        install->setVisible(state == State::Available || state == State::Ready);
        install->setText(state == State::Ready ? tr("立即安装") : tr("下载并安装"));
        check->setEnabled(state != State::Checking && !downloading && state != State::Preparing);
        automatic->setEnabled(state != State::Preparing);
        const QSignalBlocker blocker(automatic);
        automatic->setIsToggled(update->automaticUpdates());
    };
    connect(update, &UpdateService::changed, this, render);
    render();
}
