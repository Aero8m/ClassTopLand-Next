#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QString>

namespace Plugins
{
enum class UiKind { Page, Component, ComponentSettings };
struct PreparedUi
{
    QByteArray xml;
    QString error;
    QJsonArray diagnostics;
};
PreparedUi prepareUi(const QString &path, const QString &package, UiKind kind, const QString &layoutPreset = {});
// Strict at runtime; compatibility mode drops unsupported legacy declarations.
QString restrictedStyle(const QString &style, const QString &object, bool compatibility,
                        QString *error, QJsonArray *diagnostics = nullptr);
bool singleLineText(const QString &text);
bool hasLayoutStyle(const QString &style);
}
