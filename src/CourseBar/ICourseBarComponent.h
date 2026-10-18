#ifndef CLASSTOPLAND_NEXT_ICOURSEBARCOMPONENT_H
#define CLASSTOPLAND_NEXT_ICOURSEBARCOMPONENT_H
#include<QWidget>
#include<QString>
#include "CourseBarMetrics.h"
class ICourseBarComponent : public QWidget
{
    Q_OBJECT
public:
    explicit ICourseBarComponent(QWidget* parent = nullptr);
    virtual QString getName() = 0;
    virtual bool isAvailable() const { return true; }
    virtual bool visibleWhenCollapsed() const { return false; }
    const CourseBarMetrics& metrics() const { return metrics_; }
    void applyMetrics(const CourseBarMetrics& metrics);

signals:
    // Also emitted for availability changes, even when the width is unchanged.
    void preferredSizeChanged();

protected:
    virtual void updateMetrics() = 0;
    void resizeEvent(QResizeEvent* event) override;
    void notifyPreferredSizeChanged();

private:
    CourseBarMetrics metrics_;
};
#endif //CLASSTOPLAND_NEXT_ICOURSEBARCOMPONENT_H
