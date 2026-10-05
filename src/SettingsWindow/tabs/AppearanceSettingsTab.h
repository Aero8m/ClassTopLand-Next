#ifndef CLASSTOPLAND_NEXT_APPEARANCESETTINGSTAB_H
#define CLASSTOPLAND_NEXT_APPEARANCESETTINGSTAB_H

#include <ElaScrollPage.h>

class ElaPushButton;
class ElaComboBox;

class AppearanceSettingsTab final : public ElaScrollPage
{
    Q_OBJECT
public:
    explicit AppearanceSettingsTab(QWidget* parent = nullptr);

private:
    void chooseColor();
    void useSystemColor();
    void refreshColor();
    void refreshMode();
    void changeMode();
    ElaPushButton* colorButton_;
    ElaComboBox* modeCombo_;
};


#endif //CLASSTOPLAND_NEXT_APPEARANCESETTINGSTAB_H
