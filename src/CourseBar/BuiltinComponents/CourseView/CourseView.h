#ifndef CLASSTOPLAND_NEXT_COURSEVIEW_H
#define CLASSTOPLAND_NEXT_COURSEVIEW_H

#include "../../ICourseBarComponent.h"
#include "../../../Core/CourseRefreshService/CourseRefreshService.h"

class CourseBlock;
class QLabel;
class QHBoxLayout;
class QStackedLayout;

class CourseView final : public ICourseBarComponent
{
    Q_OBJECT

public:
    explicit CourseView(QWidget* parent = nullptr);
    // The alternate clock is useful for previews and deterministic checks.
    explicit CourseView(CourseRefreshService::Clock clock, QWidget* parent = nullptr);
    QString getName() override { return QStringLiteral("CourseView"); }
    CourseRefreshService* refreshService() const noexcept { return service_; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void setCompactMode(bool compact);
    bool hasCompactContent() const noexcept;
    QSize expandedSizeHint() const;
    QSize compactSizeHint() const;

signals:
    void preferredSizeChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void applyTable(const CourseRefreshService::TableSnapshot& table);
    void applyState(const CourseRefreshService::State& state);
    void logError(const QString& message);

private:
    QString shortName(const QString& subject) const;
    void updateMetrics(int height);
    void updateContentSize();
    void updateDisplayPage();

    CourseRefreshService* service_;
    QStackedLayout* pages_;
    QWidget* content_;
    QHBoxLayout* courseLayout_;
    QLabel* status_;
    QList<CourseBlock*> blocks_;
    CourseBlock* breakBlock_;
    QWidget* compactContent_;
    CourseBlock* compactBlock_;
    CourseBlock* activeBlock_ = nullptr;
    CourseRefreshService::TableSnapshot table_;
    CourseRefreshService::State visualState_;
    bool hasVisualState_ = false;
    bool showingStatus_ = true;
    bool compactMode_ = false;
    int componentHeight_ = 49;
    int preferredWidth_ = 1;
    int expandedWidth_ = 1;
    int compactWidth_ = 0;
};

#endif // CLASSTOPLAND_NEXT_COURSEVIEW_H
