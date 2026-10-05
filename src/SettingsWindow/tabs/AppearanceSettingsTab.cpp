#include "AppearanceSettingsTab.h"

#include "../../Core/ThemeManager/ThemeManager.h"
#include "../../ProfileEditWindow/ProfileEditWidgets.h"
#include <ElaColorDialog.h>
#include <ElaComboBox.h>
#include <ElaPushButton.h>
#include <ElaText.h>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QPalette>
#include <QSignalBlocker>
#include <QVBoxLayout>

AppearanceSettingsTab::AppearanceSettingsTab(QWidget* parent) : ElaScrollPage(parent)
{
    using namespace ProfileEditUi;
    setObjectName(QStringLiteral("appearanceSettingsTab"));
    auto* layout = page(this, tr("外观"));
    auto* body = card(layout);
    auto* row = new QHBoxLayout;
    row->setSpacing(20);
    auto* description = new QVBoxLayout;
    description->setSpacing(6);
    description->addWidget(text(tr("主题色"), this, 18));
    description->addWidget(text(tr("设置课表和应用控件的主题色，修改后立即生效。"), this));
    row->addLayout(description, 1);

    auto* systemColor = button(tr("使用系统主题色"), this);
    systemColor->setObjectName(QStringLiteral("useSystemThemeColor"));
    systemColor->setToolTip(tr("将当前系统强调色保存为主题色"));
    row->addWidget(systemColor, 0, Qt::AlignVCenter);

    colorButton_ = new ElaPushButton(this);
    colorButton_->setObjectName(QStringLiteral("themeColorButton"));
    colorButton_->setAccessibleName(tr("选择主题色"));
    colorButton_->setFixedSize(64, 40);
    colorButton_->setBorderRadius(6);
    colorButton_->setCursor(Qt::PointingHandCursor);
    row->addWidget(colorButton_, 0, Qt::AlignVCenter);
    body->addLayout(row);

    auto* modeBody = card(layout);
    auto* modeRow = new QHBoxLayout;
    modeRow->setSpacing(20);
    auto* modeDescription = new QVBoxLayout;
    modeDescription->setSpacing(6);
    modeDescription->addWidget(text(tr("深浅模式"), this, 18));
    modeDescription->addWidget(text(tr("跟随系统或固定使用浅色、深色外观，修改后立即生效。"), this));
    modeRow->addLayout(modeDescription, 1);
    modeCombo_ = new ElaComboBox(this);
    modeCombo_->setObjectName(QStringLiteral("themeModeCombo"));
    modeCombo_->setAccessibleName(tr("深浅模式"));
    modeCombo_->setFixedWidth(180);
    modeCombo_->addItem(tr("跟随系统"), static_cast<int>(AppearanceThemeMode::System));
    modeCombo_->addItem(tr("浅色"), static_cast<int>(AppearanceThemeMode::Light));
    modeCombo_->addItem(tr("深色"), static_cast<int>(AppearanceThemeMode::Dark));
    modeRow->addWidget(modeCombo_, 0, Qt::AlignVCenter);
    modeBody->addLayout(modeRow);
    layout->addStretch();

    connect(colorButton_, &ElaPushButton::clicked, this, &AppearanceSettingsTab::chooseColor);
    connect(systemColor, &ElaPushButton::clicked, this, &AppearanceSettingsTab::useSystemColor);
    connect(&ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &AppearanceSettingsTab::refreshColor);
    connect(modeCombo_, &ElaComboBox::currentIndexChanged, this, &AppearanceSettingsTab::changeMode);
    connect(&ThemeManager::instance(), &ThemeManager::themeModeChanged,
            this, &AppearanceSettingsTab::refreshMode);
    refreshColor();
    refreshMode();
}

void AppearanceSettingsTab::refreshColor()
{
    const QColor color = ThemeManager::instance().themeColor();
    colorButton_->setLightDefaultColor(color);
    colorButton_->setDarkDefaultColor(color);
    colorButton_->setLightHoverColor(color.lighter(110));
    colorButton_->setDarkHoverColor(color.lighter(110));
    colorButton_->setLightPressColor(color.darker(110));
    colorButton_->setDarkPressColor(color.darker(110));
    colorButton_->setToolTip(color.name(QColor::HexRgb));
    colorButton_->update();
}

void AppearanceSettingsTab::chooseColor()
{
    auto* dialog = new ElaColorDialog(window());
    dialog->setObjectName(QStringLiteral("themeColorDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setCurrentColor(ThemeManager::instance().themeColor());
    colorButton_->setEnabled(false);
    connect(dialog, &QObject::destroyed, colorButton_, [button = colorButton_] { button->setEnabled(true); });
    connect(dialog, &ElaColorDialog::colorSelected, this, [this](const QColor& color) {
        QString error;
        if (!ThemeManager::instance().setThemeColor(color, &error))
            ProfileEditUi::error(this, tr("主题色保存失败：%1").arg(error));
    });
    dialog->show();
}

void AppearanceSettingsTab::useSystemColor()
{
    const QColor color = QGuiApplication::palette().color(QPalette::Active, QPalette::Accent);
    QString error;
    if (!ThemeManager::instance().setThemeColor(color, &error))
        ProfileEditUi::error(this, tr("主题色保存失败：%1").arg(error));
}

void AppearanceSettingsTab::refreshMode()
{
    const QSignalBlocker blocker(modeCombo_);
    modeCombo_->setCurrentIndex(modeCombo_->findData(static_cast<int>(ThemeManager::instance().themeMode())));
}

void AppearanceSettingsTab::changeMode()
{
    const auto mode = static_cast<AppearanceThemeMode>(modeCombo_->currentData().toInt());
    QString error;
    if (!ThemeManager::instance().setThemeMode(mode, &error)) {
        refreshMode();
        ProfileEditUi::error(this, tr("深浅模式保存失败：%1").arg(error));
    }
}
