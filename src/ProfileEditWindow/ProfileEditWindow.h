#ifndef CLASSTOPLAND_NEXT_PROFILEEDITWINDOW_H
#define CLASSTOPLAND_NEXT_PROFILEEDITWINDOW_H
#include "ElaWindow.h"

class ProfileEditSession;
class ProfileInfoTab;
class SubjectsTab;
class TimeLinesTab;
class SchedulesTab;
class ElaText;
class ElaPushButton;
class QCloseEvent;
class QShowEvent;

class ProfileEditWindow : public ElaWindow
{
    Q_OBJECT
public:
    explicit ProfileEditWindow(QWidget *parent = nullptr);
    ~ProfileEditWindow() override;
    bool prepareToClose();
    ProfileEditSession* session() const { return session_; }
    bool saveProfile();
signals:
    void profileSaved();
protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
private:
    void initUI();
    void updateState();
    ProfileEditSession* session_;
    ProfileInfoTab* info_;
    SubjectsTab* subjects_;
    TimeLinesTab* timeLines_;
    SchedulesTab* schedules_;
    ElaText* state_;
    ElaPushButton* save_;
    ElaPushButton* discard_;
    bool resolvingClose_ = false;
};


#endif //CLASSTOPLAND_NEXT_PROFILEEDITWINDOW_H
