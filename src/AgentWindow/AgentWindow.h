#pragma once

#include "../Core/Agent/AgentService.h"
#include <ElaWindow.h>
#include <QTimer>

class ElaCheckBox;
class ElaPlainTextEdit;
class ElaPushButton;
class ElaText;
class ElaToolButton;
class QModelIndex;
class QTreeView;
class QVBoxLayout;
class ActivityGroup;
class ApprovalCard;
class AgentScrollArea;
class ModelButton;
class ReasoningScrollArea;

class AgentWindow final : public ElaWindow
{
    Q_OBJECT
public:
    explicit AgentWindow(AgentService *service, QWidget *parent = nullptr);

signals:
    void settingsRequested(const QString &draftSession);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    struct Node { QWidget *page; QString workspace; };
    struct ToolCard {
        ElaToolButton *header;
        QWidget *contents;
        ElaPushButton *review;
        ActivityGroup *group;
    };
    struct NavTarget {
        enum class Kind { None, NewChat, Workspace, Session };
        Kind kind = Kind::None;
        QString value;
    };
    void setupSidebarManagement();
    void setSidebarMultiSelect(bool enabled);
    void updateNavMultiSelectState();
    void updateNavOverlays();
    NavTarget navTargetForIndex(const QModelIndex &index) const;
    void updateNavHoverAction(const QPoint &viewportPos);
    void showSessionContextMenu(const QString &id, const QPoint &globalPos);
    void showWorkspaceContextMenu(const QString &workspace, const QPoint &globalPos);
    void promptRenameSession(const QString &id);
    void promptDeleteSessions(const QStringList &ids);
    void openSession(const QString &id);
    void syncNavigation();
    void select(const QString &id);
    void refreshControls();
    void refreshSendAvailability();
    void refreshApproval();
    void refreshResume();
    void render(bool resetScroll = true);
    void addEvent(const QJsonObject &event);
    void updateTool(const QJsonObject &event);
    void addToolEvent(const QJsonObject &event);
    void addMessage(const QString &kind, const QString &text, bool final = false);
    void addFinalCopy(ElaText *content);
    void positionLatest();
    void flushStream();
    void flushReasoning();
    void addReasoning(const QString &text, const QString &status);
    ActivityGroup *addActivity(QWidget *widget);
    void addRow(QWidget *card, bool right = false);
    void send();
    void modelMenu();
    void permissionMenu();
    void contextMenu();
    void sessionMenu();
    void notify(const QString &message);

    AgentService *service_;
    QWidget *newPage_, *chat_, *navCentral_ = nullptr, *navStack_ = nullptr;
    QTreeView *navView_ = nullptr;
    ElaToolButton *navActionButton_ = nullptr;
    QWidget *navMultiBar_ = nullptr;
    ElaText *navMultiCount_ = nullptr;
    ElaToolButton *navMultiSelectAll_ = nullptr, *navMultiDelete_ = nullptr, *navMultiDone_ = nullptr;
    QList<ElaCheckBox *> navCheckBoxes_;
    QSet<QString> checkedSessions_;
    bool sidebarMultiSelect_ = false;
    AgentScrollArea *scroll_ = nullptr;
    QVBoxLayout *messages_;
    QWidget *resumeRow_ = nullptr;
    ElaText *title_, *notice_, *streamLabel_ = nullptr;
    ReasoningScrollArea *reasoningScroll_ = nullptr;
    ElaToolButton *reasoningHeader_ = nullptr;
    ActivityGroup *activities_ = nullptr, *reasoningGroup_ = nullptr;
    QWidget *composer_ = nullptr;
    ElaPlainTextEdit *input_ = nullptr;
    ModelButton *model_;
    ElaToolButton *permission_, *context_;
    ElaToolButton *more_, *send_, *latest_ = nullptr;
    ApprovalCard *approvalCard_ = nullptr;
    QHash<QString, Node> nodes_;
    QStringList expanderOrder_;
    QHash<QString, QStringList> sessionOrder_;
    QHash<QString, QString> groups_, unsent_;
    QHash<QString, ToolCard> toolCards_;
    QString session_, settingsKey_, hoveredSession_, pendingWorkspace_;
    AgentConnection pendingModel_;
    AgentService::Permission pendingPermission_ = AgentService::Permission::Ask;
    QString stream_, reasoning_, pendingEffort_;
    QString approvalTool_;
    QTimer streamTimer_;
    bool navigating_ = false, imeComposing_ = false;
    bool pendingModelOverridden_ = false;
    bool modelAvailable_ = false;
};

