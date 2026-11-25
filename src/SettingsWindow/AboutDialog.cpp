#include "AboutDialog.h"

#include <ElaImageCard.h>
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

AboutDialog::AboutDialog(QWidget* parent) : ElaDialog(parent)
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

    const auto addLink = [this, &addText](const QString& name, const QUrl& url) {
        auto* label = addText({});
        label->setTextFormat(Qt::RichText);
        label->setTextInteractionFlags(Qt::TextBrowserInteraction);
        const auto updateLink = [label, name, url] {
            const auto color = eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::PrimaryNormal);
            label->setText(QStringLiteral("<a href=\"%1\" style=\"color:%2\">%3</a>")
                               .arg(url.toString().toHtmlEscaped(), color.name(), name.toHtmlEscaped()));
        };
        updateLink();
        connect(eTheme, &ElaTheme::themeModeChanged, label, updateLink);
        connect(label, &QLabel::linkActivated, this, [url] { QDesktopServices::openUrl(url); });
    };
    addLink(QStringLiteral("GitHub：Aero8m/ClassTopLand-Next"),
            QUrl(QStringLiteral("https://github.com/Aero8m/ClassTopLand-Next")));
    addLink(QStringLiteral("Aero8m Project Autobuild"), QUrl(QStringLiteral("https://autobuild.aero8m.cn")));
    textLayout->addStretch();

    auto* contentLayout = new QHBoxLayout;
    contentLayout->setSpacing(24);
    contentLayout->addLayout(iconLayout);
    contentLayout->addLayout(textLayout, 1);

    auto* layout = new QVBoxLayout(this);
    // ElaAppBar already reserves its height in the window contents margins.
    layout->setContentsMargins(30, 30, 30, 30);
    layout->addLayout(contentLayout);
    setFixedSize(QSize(480, 400).expandedTo(minimumSizeHint()));
}
