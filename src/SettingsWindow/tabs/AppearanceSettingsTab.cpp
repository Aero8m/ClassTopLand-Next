#include "AppearanceSettingsTab.h"

#include "../../Core/ThemeManager/ThemeManager.h"
#include "../../Ui/UiWidgets.h"
#include <ElaColorDialog.h>
#include <ElaComboBox.h>
#include <ElaPushButton.h>
#include <ElaText.h>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QPalette>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>

AppearanceSettingsTab::AppearanceSettingsTab(QWidget* parent) : ElaScrollPage(parent)
{
    using namespace AppUi;
    setObjectName(QStringLiteral("appearanceSettingsTab"));
    auto* layout = page(this, tr("外观"), 24);
    auto* body = card(layout);
    auto* row = new QHBoxLayout;
    row->setSpacing(20);
    row->addWidget(text(tr("主题色"), this), 1);

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
    modeRow->addWidget(text(tr("深浅模式"), this), 1);
    modeCombo_ = new ElaComboBox(this);
    modeCombo_->setObjectName(QStringLiteral("themeModeCombo"));
    modeCombo_->setAccessibleName(tr("深浅模式"));
    modeCombo_->setFixedWidth(180);
    modeCombo_->addItem(tr("跟随系统"), static_cast<int>(AppearanceThemeMode::System));
    modeCombo_->addItem(tr("浅色"), static_cast<int>(AppearanceThemeMode::Light));
    modeCombo_->addItem(tr("深色"), static_cast<int>(AppearanceThemeMode::Dark));
    modeRow->addWidget(modeCombo_, 0, Qt::AlignVCenter);
    modeBody->addLayout(modeRow);

    auto* effectBody = card(layout);
    auto* effectRow = new QHBoxLayout;
    effectRow->setSpacing(20);
    auto* effectDescription = new QVBoxLayout;
    effectDescription->setSpacing(6);
    effectDescription->addWidget(text(tr("窗口效果"), this));
    windowEffectDescription_ = text({}, this, 12);
    windowEffectDescription_->setObjectName(QStringLiteral("windowEffectDescription"));
    effectDescription->addWidget(windowEffectDescription_);
    effectRow->addLayout(effectDescription, 1);
    windowEffectCombo_ = new ElaComboBox(this);
    windowEffectCombo_->setObjectName(QStringLiteral("windowEffectCombo"));
    windowEffectCombo_->setAccessibleName(tr("窗口效果"));
    windowEffectCombo_->setFixedWidth(180);
    windowEffectCombo_->setMaxVisibleItems(6);
    windowEffectCombo_->addItem(QStringLiteral("Normal"), static_cast<int>(AppearanceWindowEffect::Normal));
    windowEffectCombo_->addItem(QStringLiteral("ElaMica"), static_cast<int>(AppearanceWindowEffect::ElaMica));
    windowEffectCombo_->setItemData(1, tr("使用桌面壁纸生成模糊背景，读取失败时使用默认背景。"), Qt::ToolTipRole);
    windowEffectCombo_->addItem(QStringLiteral("Mica"), static_cast<int>(AppearanceWindowEffect::Mica));
    windowEffectCombo_->addItem(QStringLiteral("Mica-Alt"), static_cast<int>(AppearanceWindowEffect::MicaAlt));
    windowEffectCombo_->addItem(QStringLiteral("Acrylic"), static_cast<int>(AppearanceWindowEffect::Acrylic));
    windowEffectCombo_->addItem(QStringLiteral("Dwm-Blur"), static_cast<int>(AppearanceWindowEffect::DwmBlur));
    auto* effectModel = qobject_cast<QStandardItemModel*>(windowEffectCombo_->model());
    for (int i = 0; i < windowEffectCombo_->count(); ++i) {
        const auto effect = static_cast<AppearanceWindowEffect>(windowEffectCombo_->itemData(i).toInt());
        const QString reason = ThemeManager::instance().windowEffectUnavailableReason(effect);
        if (!reason.isEmpty()) windowEffectCombo_->setItemData(i, reason, Qt::ToolTipRole);
        if (effectModel && !reason.isEmpty()) effectModel->item(i)->setEnabled(false);
    }
    effectRow->addWidget(windowEffectCombo_, 0, Qt::AlignVCenter);
    effectBody->addLayout(effectRow);
    layout->addStretch();

    connect(colorButton_, &ElaPushButton::clicked, this, &AppearanceSettingsTab::chooseColor);
    connect(systemColor, &ElaPushButton::clicked, this, &AppearanceSettingsTab::useSystemColor);
    connect(&ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &AppearanceSettingsTab::refreshColor);
    connect(modeCombo_, &ElaComboBox::currentIndexChanged, this, &AppearanceSettingsTab::changeMode);
    connect(&ThemeManager::instance(), &ThemeManager::themeModeChanged,
            this, &AppearanceSettingsTab::refreshMode);
    connect(windowEffectCombo_, &ElaComboBox::currentIndexChanged,
            this, &AppearanceSettingsTab::changeWindowEffect);
    connect(&ThemeManager::instance(), &ThemeManager::windowEffectChanged,
            this, &AppearanceSettingsTab::refreshWindowEffect);
    refreshColor();
    refreshMode();
    refreshWindowEffect();
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
            AppUi::error(this, tr("主题色保存失败：%1").arg(error));
    });
    dialog->show();
}

void AppearanceSettingsTab::useSystemColor()
{
    const QColor color = QGuiApplication::palette().color(QPalette::Active, QPalette::Accent);
    QString error;
    if (!ThemeManager::instance().setThemeColor(color, &error))
        AppUi::error(this, tr("主题色保存失败：%1").arg(error));
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
        AppUi::error(this, tr("深浅模式保存失败：%1").arg(error));
    }
}

void AppearanceSettingsTab::refreshWindowEffect()
{
    const auto& manager = ThemeManager::instance();
    const QSignalBlocker blocker(windowEffectCombo_);
    windowEffectCombo_->setCurrentIndex(windowEffectCombo_->findData(static_cast<int>(manager.windowEffect())));
    const QString reason = manager.windowEffectUnavailableReason(manager.windowEffect());
    windowEffectDescription_->setText(reason.isEmpty()
        ? QString() : tr("当前效果不可用，已使用 Normal。"));
    windowEffectDescription_->setVisible(!reason.isEmpty());
    windowEffectCombo_->setToolTip(!reason.isEmpty() ? reason
        : windowEffectCombo_->currentData(Qt::ToolTipRole).toString());
}

void AppearanceSettingsTab::changeWindowEffect()
{
    const auto effect = static_cast<AppearanceWindowEffect>(windowEffectCombo_->currentData().toInt());
    QString error;
    if (!ThemeManager::instance().setWindowEffect(effect, &error)) {
        refreshWindowEffect();
        AppUi::error(this, tr("窗口效果设置失败：%1").arg(error));
    }
}
