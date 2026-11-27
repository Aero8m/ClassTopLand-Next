#ifndef CLASSTOPLAND_NEXT_PROFILEEDITWINDOW_H
#define CLASSTOPLAND_NEXT_PROFILEEDITWINDOW_H
#include "ElaWindow.h"
#include <memory>

class ProfileEditSession;
namespace AppUi { class LazyPages; }
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
    QString currentPage() const;
    void restorePage(const QString& id);
signals:
    void closed();
    void profileSaved();
protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
private:
    void initUI();
    void updateState();
    std::unique_ptr<AppUi::LazyPages> pages_;
    ProfileEditSession* session_;
    ElaText* state_;
    ElaPushButton* save_;
    ElaPushButton* discard_;
    bool resolvingClose_ = false;
};


#endif //CLASSTOPLAND_NEXT_PROFILEEDITWINDOW_H
