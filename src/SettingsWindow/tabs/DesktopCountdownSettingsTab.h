#pragma once

#include <ElaScrollPage.h>
#include "../../Core/Model/DesktopCountdownConfig.h"

class ElaToggleSwitch;
class ElaLineEdit;
class ElaCalendarPicker;
class ElaComboBox;
class ElaSpinBox;
namespace AppUi { class TimePicker; }

class DesktopCountdownSettingsTab final : public ElaScrollPage
{
    Q_OBJECT
public:
    explicit DesktopCountdownSettingsTab(QWidget* parent = nullptr);
    // Reload widgets after the configuration was replaced outside this page.
    void reloadFromConfig();
signals:
    void configChanged();
protected:
    void showEvent(QShowEvent* event) override;
private:
    void refresh();
    void refreshScreens();
    void save();
    bool refreshing_ = false;
    ElaToggleSwitch* enable_;
    ElaLineEdit* label_;
    ElaLineEdit* english_;
    ElaCalendarPicker* date_;
    AppUi::TimePicker* time_;
    ElaComboBox* precision_;
    ElaComboBox* screen_;
    ElaComboBox* placement_;
    ElaSpinBox* right_;
    ElaSpinBox* bottom_;
    ElaSpinBox* scale_;
};
