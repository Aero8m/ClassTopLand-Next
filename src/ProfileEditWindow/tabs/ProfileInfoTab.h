#pragma once
#include <ElaScrollPage.h>
class ProfileEditSession;
class ElaLineEdit;
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
    ElaText* counts_;
};
