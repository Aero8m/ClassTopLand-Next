#ifndef CLASSTOPLAND_NEXT_COURSEBARSETTINGSTAB_H
#define CLASSTOPLAND_NEXT_COURSEBARSETTINGSTAB_H

#include <ElaScrollPage.h>
#include <QHash>
#include <QPointer>
#include "../../Core/Model/CourseBarConfig.h"

class CourseBarComponentRegistry;
class CourseBarComponentList;
class ElaComboBox;
class ElaDialog;
class ElaSpinBox;
class ElaText;
class ElaToggleSwitch;

class CourseBarSettingsTab final : public ElaScrollPage
{
    Q_OBJECT
public:
    explicit CourseBarSettingsTab(QWidget* parent = nullptr, CourseBarComponentRegistry* registry = nullptr);
    void setUiAccessSwitching(bool switching);
    bool defaultsResetPending() const { return resetPending_; }
    bool hasUnsavedInput() const { return !pendingTexts_.isEmpty(); }
    bool hasPendingOperations() const { return uiAccessSwitching_ || preparingRemoval_; }
signals:
    void configChanged();
    void uiAccessChangeRequested(bool enabled);
private:
    void refresh();
    void refreshControls();
    int componentIndex(const QString& id) const;
    void refreshTheme();
    void selectComponent(int index);
    void moveComponent(int from, int to);
    void showComponentSettings(int index);
    bool save(const CourseBarConfig& candidate, int selection, bool resetAll = false, bool prepared = false);
    void scheduleRefresh();
    CourseBarComponentRegistry* registry_ = nullptr;
    CourseBarConfig config_;
    QHash<QString, QString> pendingTexts_;
    QPointer<ElaDialog> componentSettingsDialog_;
    int selected_ = -1;
    bool refreshPending_ = false;
    bool uiAccessSwitching_ = false;
    bool resetPending_ = false;
    bool preparingRemoval_ = false;
    ElaToggleSwitch* enableSwitch_;
    ElaToggleSwitch* uiAccessSwitch_;
    ElaText* uiAccessStatus_;
    ElaSpinBox* heightSpin_;
    ElaSpinBox* componentSpacingSpin_;
    ElaComboBox* typeCombo_;
    ElaText* previewStatus_;
    ElaText* emptyLabel_;
    CourseBarComponentList* preview_;
    CourseBarComponentList* components_;
};


#endif //CLASSTOPLAND_NEXT_COURSEBARSETTINGSTAB_H
