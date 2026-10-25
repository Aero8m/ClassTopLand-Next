#ifndef CLASSTOPLAND_NEXT_APPEARANCESETTINGSTAB_H
#define CLASSTOPLAND_NEXT_APPEARANCESETTINGSTAB_H

#include <ElaScrollPage.h>

class ElaPushButton;
class ElaComboBox;
class ElaText;

class AppearanceSettingsTab final : public ElaScrollPage
{
    Q_OBJECT
public:
    explicit AppearanceSettingsTab(QWidget* parent = nullptr);
    // Reload widgets after the configuration was replaced outside this page.
    void reloadFromConfig();

private:
    void chooseColor();
    void useSystemColor();
    void refreshColor();
    void refreshMode();
    void changeMode();
    void refreshWindowEffect();
    void changeWindowEffect();
    ElaPushButton* colorButton_;
    ElaComboBox* modeCombo_;
    ElaComboBox* windowEffectCombo_;
    ElaText* windowEffectDescription_;
};


#endif //CLASSTOPLAND_NEXT_APPEARANCESETTINGSTAB_H
