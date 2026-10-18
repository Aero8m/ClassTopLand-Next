#pragma once
#include <QJsonObject>
#include <QString>
class QUrl;

namespace Plugins
{
constexpr int MaxMessageBytes = 256 * 1024;
constexpr int MaxPlugins = 16;
QJsonObject error(const QString &code, const QString &message);
bool validId(const QString &id);
bool within(const QString &root, const QString &path);
QString origin(const QUrl &url);
QString validateView(const QJsonObject &definition);
struct Manifest
{
    QString id, name, version, directory, entry, description, failure;
    QJsonObject permissions, contributions;
    QByteArray fingerprint;
    bool valid() const
    {
        return failure.isEmpty();
    }
    static Manifest read(const QString &directory);
};
} // namespace Plugins
