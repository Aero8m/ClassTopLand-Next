#pragma once

#include <ElaScrollPage.h>
#include "../../Core/WindowInterception/WindowInterceptionService.h"

class ElaLineEdit;
class ElaPushButton;
class ElaTableView;
class ElaText;
class QStandardItemModel;

class WindowInterceptionSettingsTab final : public ElaScrollPage
{
    Q_OBJECT
public:
    explicit WindowInterceptionSettingsTab(WindowInterceptionService* service, QWidget* parent = nullptr);
protected:
    void showEvent(QShowEvent* event) override;
private:
    void refresh();
    void scheduleRefresh();
    void updateActions();
    void act(const std::function<bool(QString*)>& action);
    std::optional<InterceptedWindow> selectedWindow() const;
    WindowInterceptionService* service_;
    QVector<InterceptedWindow> windows_;
    QVector<WindowInterceptionRule> rules_;
    ElaTableView* current_ = nullptr;
    ElaTableView* saved_ = nullptr;
    QStandardItemModel* currentModel_ = nullptr;
    QStandardItemModel* savedModel_ = nullptr;
    ElaLineEdit* title_ = nullptr;
    ElaText* error_ = nullptr;
    ElaText* monitor_ = nullptr;
    ElaText* currentEmpty_ = nullptr;
    ElaText* rulesEmpty_ = nullptr;
    ElaPushButton* hideOnce_ = nullptr;
    ElaPushButton* hideSave_ = nullptr;
    ElaPushButton* restore_ = nullptr;
    ElaPushButton* disable_ = nullptr;
    ElaPushButton* enable_ = nullptr;
    ElaPushButton* delete_ = nullptr;
    bool refreshPending_ = false;
};
