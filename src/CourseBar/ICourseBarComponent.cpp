#include "ICourseBarComponent.h"

#include "../Core/ConfigManager/ConfigManager.h"
#include <QResizeEvent>

ICourseBarComponent::ICourseBarComponent(QWidget* parent)
    : QWidget(parent), metrics_(ConfigManager::instance().config().courseBarConfig)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
}

void ICourseBarComponent::applyMetrics(const CourseBarMetrics& metrics)
{
    if (metrics_.height == metrics.height && metrics_.baseCourseSpacing == metrics.baseCourseSpacing &&
        metrics_.baseComponentSpacing == metrics.baseComponentSpacing) return;
    metrics_ = metrics;
    updateMetrics();
    notifyPreferredSizeChanged();
}

void ICourseBarComponent::notifyPreferredSizeChanged()
{
    updateGeometry();
    emit preferredSizeChanged();
}

void ICourseBarComponent::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (height() > 0)
        applyMetrics(CourseBarMetrics(height(), metrics_.baseCourseSpacing, metrics_.baseComponentSpacing));
}
