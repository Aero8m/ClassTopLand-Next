#ifndef CLASSTOPLAND_NEXT_PROFILESETTINGSTAB_H
#define CLASSTOPLAND_NEXT_PROFILESETTINGSTAB_H
#include <ElaScrollPage.h>
#include "../../Core/ProfileManager/ProfileManager.h"

class ElaTableView;
class ElaPushButton;
class ElaText;
class QStandardItemModel;
class QShowEvent;

class ProfileSettingsTab : public ElaScrollPage
{
    Q_OBJECT
public:
    explicit ProfileSettingsTab(QWidget* parent = nullptr);
    void refreshProfiles(const QString& selectedId = {});
    void showError(const QString& message);
signals:
    void profileEditorRequested();
    void profileSwitchRequested(const QString& id);
protected:
    void showEvent(QShowEvent* event) override;
private:
    int selectedIndex() const;
    void updateActions();
    void addProfile();
    void deleteProfile();
    void switchProfile();
    void importProfile();
    void exportProfile();
    QList<ProfileManager::Entry> entries_;
    ElaTableView* table_;
    QStandardItemModel* model_;
    ElaText* status_;
    ElaPushButton* add_;
    ElaPushButton* edit_;
    ElaPushButton* switch_;
    ElaPushButton* delete_;
    ElaPushButton* import_;
    ElaPushButton* export_;
    bool busy_ = false;
};


#endif //CLASSTOPLAND_NEXT_PROFILESETTINGSTAB_H
