#include "SettingsLayout.h"
#include "UiWidgets.h"
#include <ElaPushButton.h>
#include <ElaScrollPageArea.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <ElaWidgetToolsDef.h>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QBoxLayout>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QResizeEvent>
#include <QSlider>
#include <QVBoxLayout>

namespace AppUi {
QVBoxLayout* pageContent(QWidget* parent, const QString& title, int titleSize)
{
    auto* body = new QVBoxLayout(parent);
    body->setContentsMargins(24, 20, 24, 20);
    body->setSpacing(16);
    if (!title.isEmpty()) {
        auto* heading = text(title, parent, titleSize);
        heading->setObjectName(QStringLiteral("_ctlPageTitle"));
        auto font = heading->font(); font.setWeight(QFont::Medium); heading->setFont(font);
        body->addWidget(heading);
    }
    return body;
}
void configureCard(ElaScrollPageArea* area)
{
    area->setBorderRadius(8);
    area->setMinimumHeight(0);
    area->setMaximumHeight(QWIDGETSIZE_MAX);
    area->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
}
void configureCardLayout(QVBoxLayout* body)
{
    body->setSizeConstraint(QLayout::SetMinimumSize);
    body->setContentsMargins(16, 14, 16, 14);
    body->setSpacing(12);
}
void configureActionButton(ElaPushButton* button, bool accent)
{
    sizeActionButton(button);
    button->setBorderRadius(6);
    button->setCursor(Qt::PointingHandCursor);
    button->setProperty("appUiPrimary", accent);
    if (accent) primary(button);
    else {
        button->setLightDefaultColor(ElaThemeColor(ElaThemeType::Light, BasicBase));
        button->setDarkDefaultColor(ElaThemeColor(ElaThemeType::Dark, BasicBase));
        button->setLightHoverColor(ElaThemeColor(ElaThemeType::Light, BasicHover));
        button->setDarkHoverColor(ElaThemeColor(ElaThemeType::Dark, BasicHover));
        button->setLightPressColor(ElaThemeColor(ElaThemeType::Light, BasicPress));
        button->setDarkPressColor(ElaThemeColor(ElaThemeType::Dark, BasicPress));
        button->setLightTextColor(ElaThemeColor(ElaThemeType::Light, BasicText));
        button->setDarkTextColor(ElaThemeColor(ElaThemeType::Dark, BasicText));
    }
    button->update();
}
SettingsSection::SettingsSection(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(8);
    heading_ = text({}, this, 16);
    heading_->setObjectName(QStringLiteral("_ctlSectionTitle")); heading_->hide();
    outer->addWidget(heading_);
    auto* area = new ElaScrollPageArea(this);
    area->setObjectName(QStringLiteral("_ctlCardSurface")); configureCard(area);
    body_ = new QVBoxLayout(area); configureCardLayout(body_);
    outer->addWidget(area);
}
QString SettingsSection::title() const { return heading_->text(); }
void SettingsSection::setTitle(const QString& value) { heading_->setText(value); heading_->setVisible(!value.isEmpty()); }
SettingsRow::SettingsRow(QWidget* parent) : QWidget(parent)
{
    body_ = new QBoxLayout(QBoxLayout::LeftToRight, this);
    body_->setContentsMargins(0, 0, 0, 0); body_->setSpacing(20);
    labels_ = new QWidget(this); labels_->setObjectName(QStringLiteral("_ctlRowLabels"));
    auto* labels = new QVBoxLayout(labels_); labels->setContentsMargins(0, 0, 0, 0); labels->setSpacing(6);
    title_ = text({}, labels_, 14); title_->setObjectName(QStringLiteral("_ctlRowTitle"));
    description_ = text({}, labels_, 12); description_->setObjectName(QStringLiteral("_ctlRowDescription")); description_->hide();
    labels->addWidget(title_); labels->addWidget(description_);
    body_->addWidget(labels_, 1, Qt::AlignVCenter);
}
QString SettingsRow::title() const { return title_->text(); }
void SettingsRow::setTitle(const QString& value) { title_->setText(value); updateGeometry(); }
QString SettingsRow::description() const { return description_->text(); }
void SettingsRow::setDescription(const QString& value) { description_->setText(value); description_->setVisible(!value.isEmpty()); updateGeometry(); }
bool SettingsRow::attachEditor(QWidget* editor)
{
    if (editor_ && editor_ != editor) return false;
    if (editor_ == editor) return true;
    editor_ = editor;
    if (qobject_cast<QAbstractButton*>(editor) || qobject_cast<QLineEdit*>(editor) ||
        qobject_cast<QComboBox*>(editor) || qobject_cast<QAbstractSpinBox*>(editor)) {
        editor->setFixedHeight(ElaWidgetToolsSize::DefaultControlHeight);
    } else if (const auto* slider = qobject_cast<QSlider*>(editor);
               slider && slider->orientation() == Qt::Horizontal && slider->tickPosition() == QSlider::NoTicks) {
        editor->setMinimumHeight(qMax(editor->minimumHeight(), ElaWidgetToolsSize::DefaultControlHeight));
    }
    expanding_ = qobject_cast<QLineEdit*>(editor) || editor->metaObject()->className() == QByteArray("QWidget");
    editorWidth_ = expanding_ ? 240 : qobject_cast<QComboBox*>(editor) ? 180 :
        qobject_cast<QDateTimeEdit*>(editor) ? 180 : qobject_cast<QAbstractSpinBox*>(editor) ? 150 : 0;
    if (!editorWidth_) editorWidth_ = qMax(1, editor->sizeHint().width());
    body_->addWidget(editor, expanding_ ? 1 : 0, Qt::AlignVCenter);
    arrange(); return true;
}
void SettingsRow::detachEditor(QWidget* editor)
{
    if (editor_ != editor) return;
    body_->removeWidget(editor); editor_.clear(); updateGeometry();
}
int SettingsRow::heightForWidth(int width) const
{
    // Layout queries precede resizeEvent: predict the direction from the
    // requested width so ancestors reserve the stacked row's full height.
    const bool narrow = editor_ && width < 160 + 20 + editorWidth_;
    const int editorWidth = expanding_ ? qMax(editorWidth_, (width - 20) / 2) : editorWidth_;
    const int labelWidth = qMax(1, !editor_ || narrow ? width : width - 20 - editorWidth);
    const int labelHeight = labels_->hasHeightForWidth() ? labels_->heightForWidth(labelWidth) : labels_->sizeHint().height();
    if (!editor_) return labelHeight;
    const int controlWidth = narrow ? width : editorWidth;
    const int preferredHeight = editor_->hasHeightForWidth() ?
        editor_->heightForWidth(controlWidth) : editor_->sizeHint().height();
    const int controlHeight = qBound(editor_->minimumHeight(), preferredHeight, editor_->maximumHeight());
    return narrow ? labelHeight + 20 + controlHeight : qMax(labelHeight, controlHeight);
}
void SettingsRow::arrange()
{
    if (!editor_) return;
    const bool narrow = width() < 160 + 20 + editorWidth_;
    body_->setDirection(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    const int minimum = narrow ? 0 : editorWidth_;
    const int maximum = expanding_ ? QWIDGETSIZE_MAX : qMax(1, qMin(editorWidth_, width()));
    editor_->setMinimumWidth(minimum); editor_->setMaximumWidth(maximum);
    editor_->setSizePolicy(expanding_ ? QSizePolicy::Expanding : QSizePolicy::Preferred, QSizePolicy::Fixed);
}
void SettingsRow::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); arrange(); }
SettingsActions::SettingsActions(QWidget* parent) : QWidget(parent)
{
    auto* body = new QVBoxLayout(this); body->setContentsMargins(0, 0, 0, 0); body->setSpacing(8);
    message_ = text({}, this); message_->setObjectName(QStringLiteral("_ctlActionMessage")); message_->hide();
    body->addWidget(message_);
    buttons_ = new QHBoxLayout; buttons_->setSpacing(8); buttons_->addStretch(); body->addLayout(buttons_);
}
QString SettingsActions::message() const { return message_->text(); }
void SettingsActions::setMessage(const QString& value) { message_->setText(value); message_->setVisible(!value.isEmpty()); }
bool SettingsActions::canSetPrimary(ElaPushButton* button, bool primary) const
{
    if (!primary) return true;
    for (const auto& other : actions_)
        if (other && other != button && other->property("primary").toBool()) return false;
    return true;
}
int SettingsActions::actionCount() const { int count = 0; for (const auto& action : actions_) if (action) ++count; return count; }
bool SettingsActions::insertAction(ElaPushButton* button, int index)
{
    if (!canSetPrimary(button, button->property("primary").toBool())) return false;
    removeAction(button);
    actions_.removeAll(nullptr);
    const int at = index < 0 ? actions_.size() : qMin(index, int(actions_.size()));
    actions_.insert(at, button); buttons_->insertWidget(at + 1, button);
    configureActionButton(button, button->property("primary").toBool()); return true;
}
void SettingsActions::removeAction(ElaPushButton* button) { buttons_->removeWidget(button); actions_.removeAll(button); }
void SettingsActions::setPrimary(ElaPushButton* button, bool primary)
{
    button->setProperty("primary", primary); configureActionButton(button, primary);
}
}
