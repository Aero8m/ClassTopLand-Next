#ifndef CLASSTOPLAND_NEXT_COUNTDOWN_H
#define CLASSTOPLAND_NEXT_COUNTDOWN_H

#include "../../ICourseBarComponent.h"
#include "../../../Core/CourseRefreshService/CourseRefreshService.h"

class QLabel;

class Countdown final : public ICourseBarComponent
{
    Q_OBJECT

public:
    explicit Countdown(QWidget* parent = nullptr);
    explicit Countdown(CourseRefreshService::Clock clock, QWidget* parent = nullptr);
    explicit Countdown(CourseRefreshService* service, QWidget* parent);
    QString getName() override { return QStringLiteral("Countdown"); }
    CourseRefreshService* refreshService() const noexcept { return service_; }
    bool hasCountdown() const noexcept { return hasCountdown_; }
    bool isAvailable() const override { return hasCountdown_; }
    bool visibleWhenCollapsed() const override { return true; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    Countdown(CourseRefreshService::Clock clock, CourseRefreshService* service, QWidget* parent);
    void applyState(const CourseRefreshService::State& state);
    void updateMetrics() override;
    CourseRefreshService* service_;
    QLabel* label_;
    bool hasCountdown_ = false;
    int preferredWidth_ = 1;
};

#endif // CLASSTOPLAND_NEXT_COUNTDOWN_H
