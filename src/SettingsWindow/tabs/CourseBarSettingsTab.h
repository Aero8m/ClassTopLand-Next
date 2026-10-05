#ifndef CLASSTOPLAND_NEXT_COURSEBARSETTINGSTAB_H
#define CLASSTOPLAND_NEXT_COURSEBARSETTINGSTAB_H

#include <ElaScrollPage.h>
#include "../../Core/Model/CourseBarConfig.h"

class CourseBarComponentList;
class ElaComboBox;
class ElaSpinBox;
class ElaText;
class ElaToggleSwitch;

class CourseBarSettingsTab final : public ElaScrollPage
{
    Q_OBJECT
public:
    explicit CourseBarSettingsTab(QWidget* parent = nullptr);
signals:
    void configChanged();
protected:
    void showEvent(QShowEvent* event) override;
private:
    void refresh();
    void refreshTheme();
    void selectComponent(int index);
    void moveComponent(int from, int to);
    void save(const CourseBarConfig& candidate, int selection);
    void scheduleRefresh();
    CourseBarConfig config_;
    int selected_ = -1;
    bool refreshPending_ = false;
    ElaToggleSwitch* enableSwitch_;
    ElaSpinBox* heightSpin_;
    ElaComboBox* typeCombo_;
    ElaText* previewStatus_;
    ElaText* emptyLabel_;
    CourseBarComponentList* preview_;
    CourseBarComponentList* components_;
};


#endif //CLASSTOPLAND_NEXT_COURSEBARSETTINGSTAB_H
