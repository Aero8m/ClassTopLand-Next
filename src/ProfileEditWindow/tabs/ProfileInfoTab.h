#pragma once
#include <ElaScrollPage.h>
class ProfileEditSession;
class ElaLineEdit;
class ElaCalendarPicker;
class ElaToggleSwitch;
class ElaText;

class ProfileInfoTab final : public ElaScrollPage {
    Q_OBJECT
public:
    explicit ProfileInfoTab(ProfileEditSession* session, QWidget* parent = nullptr);
    void focusName();
private:
    void refresh();
    ProfileEditSession* session_;
    ElaLineEdit* name_;
    ElaToggleSwitch* semesterEnabled_;
    ElaCalendarPicker* semesterStart_;
    ElaText* semesterHint_;
    ElaText* counts_;
};
