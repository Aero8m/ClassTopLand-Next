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
    addText(QStringLiteral("开发实验阶段"));
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
    setFixedSize(QSize(480, 400).expandedTo(minimumSizeHint()));
}
