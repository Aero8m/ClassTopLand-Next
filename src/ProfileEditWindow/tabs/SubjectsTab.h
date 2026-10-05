#pragma once
#include <ElaScrollPage.h>
class ProfileEditSession;
class ElaTableView;
class ElaText;
class QStandardItemModel;
class ElaPushButton;

class SubjectsTab final : public ElaScrollPage {
    Q_OBJECT
public:
    explicit SubjectsTab(ProfileEditSession* session, QWidget* parent = nullptr);
    void focusItem(int index);
private:
    void refresh();
    void editSubject(int index);
    void removeSubject();
    ProfileEditSession* session_;
    ElaTableView* table_;
    QStandardItemModel* model_;
    ElaText* empty_;
    ElaPushButton* edit_;
    ElaPushButton* remove_;
};
