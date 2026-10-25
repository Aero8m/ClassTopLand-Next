#pragma once

#include <QString>
#include <QUrl>

// Origin handling for the device endpoints. The server origin is configured by
// the user, so every URL that carries the device credential is derived here and
// checked for same-origin before it is used.
namespace Cloud {

// Rejects paths, queries, fragments and credentials. Plain HTTP is accepted only
// for loopback hosts so local CTX-Server instances remain usable. Returns an
// empty string and fills error on failure.
QString normalizeOrigin(const QString& input, QString* error = nullptr);
bool isLoopbackHost(const QString& host);
// Absolute URL for an API path such as "/device/sync" (prefixed with /api/v1).
QUrl apiUrl(const QString& origin, const QString& apiPath);
// Resolves a server supplied download path against the origin. Only URLs on the
// configured origin whose path starts with /api/v1/ are accepted.
bool resolveApiPath(const QString& origin, const QString& relativePath, QUrl& result,
                    QString* error = nullptr);
bool isSameOrigin(const QString& origin, const QUrl& url);

} // namespace Cloud
