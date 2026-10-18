#ifndef CLASSTOPLAND_NEXT_COURSEVIEW_H
#define CLASSTOPLAND_NEXT_COURSEVIEW_H

#include "../../ICourseBarComponent.h"
#include "../../../Core/CourseRefreshService/CourseRefreshService.h"

class CourseBlock;
class QLabel;
class QStackedLayout;

class CourseView final : public ICourseBarComponent
{
    Q_OBJECT

public:
    explicit CourseView(QWidget* parent = nullptr);
    // The alternate clock is useful for previews and deterministic checks.
    explicit CourseView(CourseRefreshService::Clock clock, QWidget* parent = nullptr);
    explicit CourseView(CourseRefreshService* service, QWidget* parent);
    QString getName() override { return QStringLiteral("CourseView"); }
    CourseRefreshService* refreshService() const noexcept { return service_; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private slots:
    void applyTable(const CourseRefreshService::TableSnapshot& table);
    void applyState(const CourseRefreshService::State& state);
    void logError(const QString& message);

private:
    CourseView(CourseRefreshService::Clock clock, CourseRefreshService* service, QWidget* parent);
    QString shortName(const QString& subject) const;
    void updateMetrics() override;
    void updateContentSize();
    void updateDisplayPage();
    void refreshThemeColor();

    CourseRefreshService* service_;
    QStackedLayout* pages_;
    QWidget* content_;
    QLabel* status_;
    QList<CourseBlock*> blocks_;
    CourseBlock* breakBlock_;
    CourseBlock* activeBlock_ = nullptr;
    CourseRefreshService::TableSnapshot table_;
    CourseRefreshService::State visualState_;
    bool hasVisualState_ = false;
    bool showingStatus_ = true;
    int preferredWidth_ = 1;
};

#endif // CLASSTOPLAND_NEXT_COURSEVIEW_H
