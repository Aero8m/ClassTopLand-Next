#ifndef CLASSTOPLAND_NEXT_COURSEBARMETRICS_H
#define CLASSTOPLAND_NEXT_COURSEBARMETRICS_H

#include <QFont>
#include <QFontMetrics>
#include <QString>
#include "../Core/Model/CourseBarConfig.h"
#include "CourseBarText.h"

// All dimensions are logical pixels. Qt handles the display's device pixel ratio.
struct CourseBarMetrics
{
    explicit CourseBarMetrics(int barHeight = 49, int courseSpacing = CourseBarConfig::DefaultSpacing,
                              int componentSpacing = CourseBarConfig::DefaultComponentSpacing)
        : height(qMax(1, barHeight)), baseCourseSpacing(qBound(0, courseSpacing, CourseBarConfig::MaximumSpacing)),
          baseComponentSpacing(qBound(0, componentSpacing, CourseBarConfig::MaximumSpacing)) {}
    explicit CourseBarMetrics(const CourseBarConfig& config)
        : CourseBarMetrics(config.height, CourseBarConfig::DefaultSpacing, config.componentSpacing) {}

    int scaled(int base) const { return qMax(1, qRound(double(height) * base / 49.0)); }
    // Preserve the original bar's breathing room without square course cells.
    int courseSpacing() const { return baseCourseSpacing == 0 ? 0 : scaled(baseCourseSpacing); }
    int componentSpacing() const { return baseComponentSpacing == 0 ? 0 : scaled(baseComponentSpacing); }
    int rightMargin() const { return scaled(20); }
    int handleWidth() const { return scaled(19); }
    int leadingSpacing() const { return scaled(15); }
    int textGuard() const { return scaled(1); }
    int progressHeight() const { return qMin(height, scaled(4)); }
    int notificationInset() const { return scaled(12); }
    int notificationIconSize() const { return scaled(35); }

    QFont font(bool notification = false) const
    {
        QFont result(QStringLiteral("Microsoft YaHei UI"));
        result.setPixelSize(scaled(notification ? 24 : 20));
        return result;
    }

    int textWidth(const QString& text) const
    {
        return textWidth(text, font());
    }

    int textWidth(const QString& text, const QFont& textFont) const
    {
        return inkWidth(textFont, text) + 2 * textGuard();
    }

    static int inkWidth(const QFont& font, const QString& text)
    {
        const auto image = courseBarTextImage(font, text).image;
        return qCeil(image.width() / image.devicePixelRatio());
    }

    int height;
    int baseCourseSpacing;
    int baseComponentSpacing;
};

#endif // CLASSTOPLAND_NEXT_COURSEBARMETRICS_H
