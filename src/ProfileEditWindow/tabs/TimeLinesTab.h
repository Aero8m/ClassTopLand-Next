#pragma once
#include <ElaScrollPage.h>
#include "../../Core/Model/TimeLine.h"
class ProfileEditSession;
class ElaComboBox;
class ElaPushButton;
class ElaText;
class ElaScrollArea;
class TimeLineCanvas;
class QUndoStack;
class QBoxLayout;
namespace ProfileEditUi { class TimePicker; }

class TimeLinesTab final : public ElaScrollPage {
    Q_OBJECT
public:
    explicit TimeLinesTab(ProfileEditSession* session, QWidget* parent = nullptr);
    void focusItem(int index, int row);
protected:
    void resizeEvent(QResizeEvent*) override;
private:
    void refresh();
    void switchLine();
    void syncSelection();
    void editLine(bool create);
    void removeLine();
    void addPoint(QTime start = {});
    void removePoint();
    bool pushChange(const QList<TimeLinePoint>& points, int selection, const QString& title, QString* error = nullptr);
    void applySnapshot(const QList<TimeLinePoint>& points, int selection);
    void changeSelectedTime();
    void scrollToSelection(bool initial = false);
    void changeZoom(int zoom);
    void updateLayout();
    ProfileEditSession* session_;
    ElaComboBox* lines_;
    TimeLineCanvas* canvas_;
    ElaScrollArea* scroll_;
    QWidget* editor_;
    QBoxLayout* columns_ = nullptr;
    ElaText* empty_;
    ElaText* zoomText_;
    ProfileEditUi::TimePicker* start_;
    ProfileEditUi::TimePicker* end_;
    ElaPushButton* rename_;
    ElaPushButton* removeLine_;
    ElaPushButton* add_;
    ElaPushButton* remove_;
    ElaPushButton* zoomOut_;
    ElaPushButton* zoomIn_;
    QUndoStack* undo_;
    QList<TimeLinePoint> displayed_;
    int currentLine_ = -1;
    bool applying_ = false;
};
