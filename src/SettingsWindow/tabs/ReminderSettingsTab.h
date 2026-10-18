#pragma once

#include <ElaScrollPage.h>
#include "../../Core/Model/ReminderConfig.h"
#include <array>
#include <functional>

class ElaToggleSwitch;
class ElaSpinBox;
class ElaLineEdit;
class ElaText;
class ElaPushButton;
class ElaTableView;
class QStandardItemModel;

class ReminderSettingsTab final : public ElaScrollPage
{
    Q_OBJECT
public:
    explicit ReminderSettingsTab(QWidget* parent = nullptr);
signals:
    void previewRequested(const QString& text);
private:
    void refreshConfig();
    void refreshRules();
    void updateActions();
    bool saveConfig(const std::function<void(ReminderConfig&)>& update);
    void editRule(bool add);
    void preview(const QString& text);
    bool saveRules(const QList<ReminderRule>& rules);
    ElaToggleSwitch* enable_;
    ElaSpinBox* advance_;
    std::array<ElaToggleSwitch*, 3> switches_{};
    std::array<ElaLineEdit*, 3> texts_{};
    std::array<ElaText*, 3> examples_{};
    std::array<bool, 3> pending_{};
    bool refreshing_ = false;
    ElaTableView* table_;
    QStandardItemModel* model_;
    ElaText* empty_;
    ElaText* profile_;
    ElaPushButton* edit_;
    ElaPushButton* remove_;
    ElaPushButton* toggle_;
    ElaPushButton* test_;
};
