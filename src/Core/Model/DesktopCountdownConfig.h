#pragma once

#include <QDateTime>
#include <QString>

struct DesktopCountdownConfig
{
    bool enable = false;
    // Invalid is allowed while disabled; the settings page supplies the first target.
    QDateTime targetDateTime;
    QString labelTag;
    QString englishTag;
    bool showMilliseconds = true;
    QString screenName; // Empty follows the primary screen.
    int rightMargin = 24;
    int bottomMargin = 24;
    int scalePercent = 100;
    static constexpr int MaximumMargin = 10000;
};
