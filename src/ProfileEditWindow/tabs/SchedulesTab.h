#pragma once
#include <ElaScrollPage.h>
class ProfileEditSession;
class ElaComboBox;
class ElaPushButton;
class ElaTableView;
class ElaText;
class QStandardItemModel;

class SchedulesTab final : public ElaScrollPage {
    Q_OBJECT
public:
    explicit SchedulesTab(ProfileEditSession* session, QWidget* parent = nullptr);
    void focusItem(int index, int weekday, int row);
private:
    void refresh();
    void refreshCourses();
    int dayIndex() const;
    void editWeek(bool create);
    void removeWeek();
    void editDay();
    void removeDay();
    void editCourse(int index);
    void removeCourse();
    void applyLine();
    ProfileEditSession* session_;
    ElaComboBox* weeks_;
    ElaComboBox* weekdays_;
    ElaComboBox* lines_;
    ElaText* mode_;
    ElaText* dayName_;
    ElaText* empty_;
    ElaTableView* table_;
    QStandardItemModel* model_;
    ElaPushButton* editWeek_;
    ElaPushButton* removeWeek_;
    ElaPushButton* editDay_;
    ElaPushButton* removeDay_;
    ElaPushButton* add_;
    ElaPushButton* edit_;
    ElaPushButton* remove_;
    ElaPushButton* apply_;
};
