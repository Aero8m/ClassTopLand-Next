#pragma once

#include <QDir>
#include <QString>
#include <QVector>

inline QString normalizedWindowPath(QString path)
{
    return QDir::cleanPath(path.replace(QLatin1Char('\\'), QLatin1Char('/')));
}

struct WindowInterceptionRule
{
    QString exePath;
    QString className;
    QString titleContains;
    bool enabled = true;
};

struct WindowInterceptionConfig
{
    QVector<WindowInterceptionRule> rules;
};
