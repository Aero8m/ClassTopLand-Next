#pragma once

#include "../../Core/Agent/AgentCatalog.h"
#include "../../Core/Agent/AgentService.h"
#include <ElaScrollPage.h>

class ElaComboBox;
class ElaLineEdit;
class ElaListView;
class ElaPivot;
class ElaPlainTextEdit;
class ElaPushButton;
class ElaText;
class QStackedWidget;
class QStandardItemModel;
class QVBoxLayout;

class AgentSettingsTab final : public ElaScrollPage
{
    Q_OBJECT
public:
    explicit AgentSettingsTab(AgentService *service, QWidget *parent = nullptr);
    void openDraft(const QString &session);
    void openProviders();

protected:
    void hideEvent(QHideEvent *event) override;

private:
    QWidget *makeProviders();
    QWidget *makeWorkspaces();
    QWidget *makeDrafts();
    void reloadProviders(const QString &selected = {});
    void selectProvider(const AgentProvider &provider);
    AgentProvider editedProvider() const;
    bool canLeaveProvider();
    void reloadModels(const QString &selected = {});
    void editModel(bool existing);
    void showCandidates(const QList<AgentModelEntry> &models, bool catalogOnly, const QString &message);
    void reloadWorkspaces();
    void reloadDrafts(const QString &selected = {});
    void showDraft();
    void showStatus(ElaText *label, const QString &message);
    void updateControls();
    void cancelProviderRequests();

    AgentService *service_;
    AgentCatalog catalog_;
    ElaPivot *pivot_;
    QStackedWidget *pages_;
    ElaListView *providers_, *models_, *workspaces_, *drafts_;
    QStandardItemModel *providersModel_, *modelsModel_, *workspacesModel_, *draftsModel_;
    ElaComboBox *preset_;
    ElaLineEdit *name_, *address_, *key_;
    ElaText *providerStatus_, *workspaceStatus_, *draftStatus_;
    ElaPushButton *save_, *removeProvider_, *fetch_, *test_, *testTool_, *publish_;
    ElaPlainTextEdit *manifestDiff_, *scriptDiff_, *permissionDiff_;
    AgentProvider selectedProvider_;
    QList<AgentModelEntry> editableModels_;
    AgentService::Review review_;
    bool loadingProvider_ = false, fetching_ = false, testing_ = false, publishing_ = false;
};

