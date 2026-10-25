#pragma once

#include <ElaScrollPage.h>

class ElaLineEdit;
class ElaPushButton;
class ElaText;
class ElaToggleSwitch;
class CloudSyncService;

// Settings page for the CTX centralized control service. It only talks to
// CloudSyncService; no protocol logic lives in the widget.
class CloudSettingsTab final : public ElaScrollPage
{
    Q_OBJECT
public:
    explicit CloudSettingsTab(CloudSyncService* cloud, QWidget* parent = nullptr);

signals:
    // Routed to AppComposer so the existing profile switch/restart flow is used.
    void profileSwitchRequested(const QString& id);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void refresh();
    void pair();
    void saveOrigin();
    void unbind();
    void resetState();
    void switchProfile();
    QString statusText(int status) const;

    CloudSyncService* cloud_ = nullptr;
    ElaLineEdit* origin_ = nullptr;
    ElaPushButton* originSave_ = nullptr;
    ElaLineEdit* code_ = nullptr;
    ElaPushButton* pair_ = nullptr;
    ElaToggleSwitch* enable_ = nullptr;
    ElaPushButton* syncNow_ = nullptr;
    ElaText* status_ = nullptr;
    ElaText* detail_ = nullptr;
    ElaText* profile_ = nullptr;
    ElaPushButton* switch_ = nullptr;
    ElaPushButton* unbind_ = nullptr;
    ElaPushButton* reset_ = nullptr;
    bool refreshing_ = false;
    bool originEdited_ = false;
};
