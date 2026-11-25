#include "AboutDialog.h"

#include <ElaImageCard.h>
#include <ElaPlainTextEdit.h>
#include <ElaProgressBar.h>
#include <ElaPushButton.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <QApplication>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QImage>
#include <QUrl>
#include <QVBoxLayout>

#include "../Core/CloudSync/CloudOperations.h"
#include "../Core/Update/UpdateService.h"

AboutDialog::AboutDialog(UpdateService* update, QWidget* parent) : ElaDialog(parent)
{
    setWindowTitle(QStringLiteral("关于"));
    setWindowIcon(QApplication::windowIcon());
    setWindowModality(Qt::NonModal);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setIsStayTop(false);
    setIsFixedSize(true);
    setWindowButtonFlags(ElaAppBarType::CloseButtonHint);

    auto* icon = new ElaImageCard(this);
    icon->setFixedSize(64, 64);
    icon->setIsPreserveAspectCrop(false);
    icon->setCardImage(QImage(QStringLiteral(":/res/images/icon.png")));

    auto* iconLayout = new QVBoxLayout;
    iconLayout->addWidget(icon);
    iconLayout->addStretch();

    auto* textLayout = new QVBoxLayout;
    textLayout->setSpacing(18);
    const auto addText = [this, textLayout](const QString& value, int size = 14) {
        auto* label = new ElaText(value, size, this);
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        label->setWordWrap(true);
        textLayout->addWidget(label);
        return label;
    };

    auto* title = addText(QApplication::applicationName(), 22);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    addText(QStringLiteral("基于 Qt6 和 ElaWidgetTools 的\n桌面课程显示组件"));
    const int build = UpdateService::currentBuild();
    addText(build > 0 ? QStringLiteral("构建 %1 · %2").arg(build).arg(CloudOperations::buildId())
                      : QStringLiteral("build ") + CloudOperations::buildId());
    addText(QStringLiteral("GNU General Public License v2"));
    addText(QStringLiteral("Qt 运行版本：%1").arg(QString::fromLatin1(qVersion())));

    auto* repository = addText({});
    repository->setTextFormat(Qt::RichText);
    repository->setTextInteractionFlags(Qt::TextBrowserInteraction);
    const auto updateRepository = [repository] {
        const auto color = eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::PrimaryNormal);
        repository->setText(QStringLiteral(
            "<a href=\"https://github.com/Aero8m/ClassTopLand-Next\" style=\"color:%1\">"
            "GitHub：Aero8m/ClassTopLand-Next</a>").arg(color.name()));
    };
    updateRepository();
    connect(eTheme, &ElaTheme::themeModeChanged, repository, updateRepository);
    connect(repository, &QLabel::linkActivated, this, [](const QString& link) {
        QDesktopServices::openUrl(QUrl(link));
    });
    textLayout->addStretch();

    auto* contentLayout = new QHBoxLayout;
    contentLayout->setSpacing(24);
    contentLayout->addLayout(iconLayout);
    contentLayout->addLayout(textLayout, 1);

    auto* layout = new QVBoxLayout(this);
    // ElaAppBar already reserves its height in the window contents margins.
    layout->setContentsMargins(30, 30, 30, 30);
    layout->addLayout(contentLayout);
    if (update && update->supported()) {
        auto* status = new ElaText(this);
        status->setWordWrap(true);
        status->setTextFormat(Qt::PlainText);
        auto* notes = new ElaPlainTextEdit(this);
        notes->setReadOnly(true);
        notes->setAccessibleName(QStringLiteral("更新日志"));
        notes->setFixedHeight(86);
        auto* progress = new ElaProgressBar(this);
        auto* check = new ElaPushButton(QStringLiteral("检查更新"), this);
        auto* install = new ElaPushButton(QStringLiteral("下载并安装"), this);
        auto* cancel = new ElaPushButton(QStringLiteral("取消下载"), this);
        auto* actions = new QHBoxLayout;
        actions->addWidget(check);
        actions->addWidget(install);
        actions->addWidget(cancel);
        actions->addStretch();
        layout->addWidget(status);
        layout->addWidget(notes);
        layout->addWidget(progress);
        layout->addLayout(actions);

        connect(check, &ElaPushButton::clicked, update, &UpdateService::check);
        connect(install, &ElaPushButton::clicked, update, &UpdateService::downloadAndInstall);
        connect(cancel, &ElaPushButton::clicked, update, &UpdateService::cancelDownload);
        const auto render = [=] {
            using State = UpdateService::State;
            const auto state = update->state();
            status->setText(update->message());
            if (notes->toPlainText() != update->notes()) notes->setPlainText(update->notes());
            progress->setValue(update->percent());
            progress->setVisible(state == State::Downloading);
            cancel->setVisible(state == State::Downloading);
            install->setVisible(state == State::Available || state == State::Ready);
            install->setText(state == State::Ready ? QStringLiteral("安装更新") : QStringLiteral("下载并安装"));
            check->setEnabled(state != State::Checking && state != State::Downloading && state != State::Preparing);
        };
        connect(update, &UpdateService::changed, this, render);
        render();
    } else {
        auto* mirror = new ElaPushButton(QStringLiteral("前往 Autobuild 下载"), this);
        connect(mirror, &ElaPushButton::clicked, this, [] { QDesktopServices::openUrl(UpdateService::mirrorPage()); });
        layout->addWidget(mirror, 0, Qt::AlignLeft);
    }
    setFixedSize(QSize(480, 400).expandedTo(minimumSizeHint()));
}
