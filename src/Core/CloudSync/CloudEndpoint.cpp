#include "CloudEndpoint.h"

namespace {
bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}
}

bool Cloud::isLoopbackHost(const QString& host)
{
    const QString value = host.toLower();
    return value == QStringLiteral("localhost") || value == QStringLiteral("127.0.0.1") ||
           value == QStringLiteral("::1") || value == QStringLiteral("[::1]");
}

QString Cloud::normalizeOrigin(const QString& input, QString* error)
{
    if (error) error->clear();
    const QString text = input.trimmed();
    if (text.isEmpty()) {
        fail(error, QStringLiteral("服务器地址不能为空"));
        return {};
    }
    const QUrl url(text, QUrl::StrictMode);
    if (!url.isValid()) {
        fail(error, QStringLiteral("服务器地址无效：%1").arg(url.errorString()));
        return {};
    }
    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        fail(error, QStringLiteral("服务器地址必须以 https:// 或 http:// 开头"));
        return {};
    }
    if (url.authority().contains(QLatin1Char('@'))) {
        fail(error, QStringLiteral("服务器地址不能包含用户名或密码"));
        return {};
    }
    if (url.hasQuery() || url.hasFragment()) {
        fail(error, QStringLiteral("服务器地址不能包含查询参数或片段"));
        return {};
    }
    if (!url.path().isEmpty() && url.path() != QStringLiteral("/")) {
        fail(error, QStringLiteral("服务器地址不能包含路径"));
        return {};
    }
    const QString host = url.host();
    if (host.isEmpty()) {
        fail(error, QStringLiteral("服务器地址缺少主机名"));
        return {};
    }
    if (scheme == QStringLiteral("http") && !isLoopbackHost(host)) {
        fail(error, QStringLiteral("非本机服务器必须使用 https://"));
        return {};
    }
    QString hostText = host;
    if (hostText.contains(QLatin1Char(':'))) hostText = QLatin1Char('[') + hostText + QLatin1Char(']');
    QString result = scheme + QStringLiteral("://") + hostText;
    if (url.port() != -1) result += QLatin1Char(':') + QString::number(url.port());
    return result;
}

QUrl Cloud::apiUrl(const QString& origin, const QString& apiPath)
{
    QString path = apiPath;
    if (!path.startsWith(QLatin1Char('/'))) path.prepend(QLatin1Char('/'));
    return QUrl(origin + QStringLiteral("/api/v1") + path);
}

bool Cloud::isSameOrigin(const QString& origin, const QUrl& url)
{
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) return false;
    const QString candidate = normalizeOrigin(url.scheme() + QStringLiteral("://") + url.authority());
    const QString expected = normalizeOrigin(origin);
    return !candidate.isEmpty() && !expected.isEmpty() && candidate == expected;
}

bool Cloud::resolveApiPath(const QString& origin, const QString& relativePath, QUrl& result,
                           QString* error)
{
    if (error) error->clear();
    const QString text = relativePath.trimmed();
    if (text.isEmpty()) return fail(error, QStringLiteral("下载地址为空"));
    const QUrl raw(text);
    QUrl target;
    if (raw.isValid() && !raw.scheme().isEmpty()) {
        target = raw;
    } else {
        QString path = text;
        if (!path.startsWith(QLatin1Char('/'))) path.prepend(QLatin1Char('/'));
        target = QUrl(origin + path);
    }
    if (!target.isValid()) return fail(error, QStringLiteral("下载地址无效"));
    if (target.authority().contains(QLatin1Char('@')))
        return fail(error, QStringLiteral("下载地址不能包含凭证"));
    if (target.hasFragment()) return fail(error, QStringLiteral("下载地址不能包含片段"));
    if (!isSameOrigin(origin, target))
        return fail(error, QStringLiteral("下载地址不属于已配置的服务器"));
    if (!target.path().startsWith(QStringLiteral("/api/v1/")))
        return fail(error, QStringLiteral("下载地址不属于设备接口"));
    result = target;
    return true;
}
