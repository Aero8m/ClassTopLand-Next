#include "PluginIo.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QSaveFile>
#include <QTimer>
#include <QFileInfo>
#include <QNetworkProxyFactory>

QString PluginIo::key(const QString &id, quint64 generation, const QString &requestId) const
{
    return id + QLatin1Char('/') + QString::number(generation) + QLatin1Char('/') + requestId;
}
void PluginIo::attach(const QString &id, quint64 generation, const QJsonObject &permissions)
{
    if (sessions_.contains(id))
        detach(id, sessions_[id].generation);
    auto *manager = new QNetworkAccessManager(this);
    sessions_.insert(id, {generation, permissions, manager});
}
void PluginIo::detach(const QString &id, quint64 generation)
{
    if (!sessions_.contains(id) || sessions_[id].generation != generation)
        return;
    const auto keys = transfers_.keys();
    for (const auto &k : keys)
    {
        auto transfer = transfers_.value(k);
        if (transfer->id == id && transfer->generation == generation)
            cancel(id, generation, transfer->requestId);
    }
    auto session = sessions_.take(id);
    delete session.network;
}
void PluginIo::cancel(const QString &id, quint64 generation, const QString &requestId)
{
    auto transfer = transfers_.take(key(id, generation, requestId));
    if (!transfer)
        return;
    if (transfer->reply)
    {
        transfer->reply->disconnect(this);
        transfer->reply->abort();
        transfer->reply->deleteLater();
    }
    emit completed(id, generation, requestId,
                   Plugins::error(QStringLiteral("CANCELED"), QStringLiteral("请求已取消")), {});
}
void PluginIo::request(const QString &id, quint64 generation, const QString &requestId, const QString &method,
                       const QJsonObject &args)
{
    if (!sessions_.contains(id) || sessions_[id].generation != generation)
        return;
    auto finish = [&](const QJsonObject &error, const QJsonValue &value = QJsonValue())
    { emit completed(id, generation, requestId, error, value); };
    if (method == QStringLiteral("http.request"))
    {
        const auto timeoutValue = args.value(QStringLiteral("timeoutMs"));
        if (!args.value(QStringLiteral("url")).isString() ||
            (args.contains(QStringLiteral("method")) && !args.value(QStringLiteral("method")).isString()) ||
            (args.contains(QStringLiteral("headers")) && !args.value(QStringLiteral("headers")).isObject()) ||
            (!timeoutValue.isUndefined() &&
             (!timeoutValue.isDouble() || timeoutValue.toDouble() != timeoutValue.toInt() ||
              timeoutValue.toInt() < 100 || timeoutValue.toInt() > 60000)))
        {
            finish(Plugins::error(QStringLiteral("INVALID_ARGUMENT"),
                                  QStringLiteral("HTTP 参数类型或超时范围无效")));
            return;
        }
        int count = 0;
        for (const auto &t : transfers_)
            if (t->id == id)
                ++count;
        if (count >= 8)
        {
            finish(Plugins::error(QStringLiteral("RATE_LIMITED"), QStringLiteral("最多 8 个并发请求")));
            return;
        }
        auto transfer = std::make_shared<Transfer>();
        transfer->id = id;
        transfer->generation = generation;
        transfer->requestId = requestId;
        transfer->options = args;
        transfers_.insert(key(id, generation, requestId), transfer);
        const int timeout = qBound(100, args.value(QStringLiteral("timeoutMs")).toInt(10000), 60000);
        QTimer::singleShot(timeout, this,
                           [this, transfer]
                           {
                               const auto k = key(transfer->id, transfer->generation, transfer->requestId);
                               if (!transfers_.contains(k))
                                   return;
                               transfers_.remove(k);
                               if (transfer->reply)
                               {
                                   transfer->reply->disconnect(this);
                                   transfer->reply->abort();
                                   transfer->reply->deleteLater();
                               }
                               emit completed(
                                   transfer->id, transfer->generation, transfer->requestId,
                                   Plugins::error(QStringLiteral("TIMEOUT"), QStringLiteral("请求超时")), {});
                           });
        http(transfer);
        return;
    }
    if (!method.startsWith(QStringLiteral("storage.")))
    {
        finish(Plugins::error(QStringLiteral("NOT_FOUND"), method));
        return;
    }
    const auto field = args.value(QStringLiteral("key")).toString();
    if (field.isEmpty() || field.size() > 128)
    {
        finish(
            Plugins::error(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("存储 key 长度必须为 1～128")));
        return;
    }
    const auto dataRoot = QDir(directory_).filePath(QStringLiteral("plugin-data"));
    const auto root = QDir(dataRoot).filePath(id);
    const auto path = QDir(root).filePath(QStringLiteral("storage.json"));
    if (!Plugins::validId(id) || QFileInfo(dataRoot).isSymLink() || QFileInfo(root).isSymLink() ||
        QFileInfo(path).isSymLink() || !QDir().mkpath(root) || !Plugins::within(directory_, root) ||
        !Plugins::within(dataRoot, root))
    {
        finish(Plugins::error(QStringLiteral("SAVE_FAILED"), QStringLiteral("插件数据目录不可用或路径越界")));
        return;
    }
    QJsonObject store;
    if (QFileInfo::exists(path))
    {
        if (!Plugins::within(root, path))
        {
            finish(Plugins::error(QStringLiteral("PERMISSION_DENIED"), QStringLiteral("存储路径越界")));
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        {
            finish(Plugins::error(QStringLiteral("SAVE_FAILED"), QStringLiteral("插件存储无法读取或超额")));
            return;
        }
        QJsonParseError parse;
        auto document = QJsonDocument::fromJson(file.readAll(), &parse);
        if (parse.error != QJsonParseError::NoError || !document.isObject() ||
            document.object().value(QStringLiteral("schemaVersion")).toInt() != 1 ||
            !document.object().value(QStringLiteral("values")).isObject())
        {
            finish(Plugins::error(QStringLiteral("SAVE_FAILED"),
                                  QStringLiteral("插件存储损坏或版本不受支持，原文件已保留")));
            return;
        }
        store = document.object().value(QStringLiteral("values")).toObject();
    }
    if (method == QStringLiteral("storage.get"))
    {
        finish({}, store.value(field));
        return;
    }
    if (args.contains(QStringLiteral("expected")) &&
        args.value(QStringLiteral("expected")) !=
            (store.contains(field) ? store.value(field) : QJsonValue(QJsonValue::Null)))
    {
        finish(Plugins::error(QStringLiteral("CONFLICT"),
                              QStringLiteral("设置已由其他任务修改，请重新打开窗口")));
        return;
    }
    if (method == QStringLiteral("storage.set"))
        store.insert(field, args.value(QStringLiteral("value")));
    else if (method == QStringLiteral("storage.remove"))
        store.remove(field);
    else
    {
        finish(Plugins::error(QStringLiteral("NOT_FOUND"), method));
        return;
    }
    const auto bytes =
        QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("values"), store}})
            .toJson();
    if (bytes.size() > 1024 * 1024)
    {
        finish(Plugins::error(QStringLiteral("RATE_LIMITED"), QStringLiteral("插件存储超过 1 MiB")));
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        finish(Plugins::error(QStringLiteral("SAVE_FAILED"), file.errorString()));
        return;
    }
    finish({}, true);
}
void PluginIo::http(const std::shared_ptr<Transfer> &transfer)
{
    const auto k = key(transfer->id, transfer->generation, transfer->requestId);
    auto finish = [this, transfer, k](const QJsonObject &error, const QJsonValue &result = QJsonValue())
    {
        if (!transfers_.remove(k))
            return;
        if (transfer->reply)
        {
            transfer->reply->disconnect(this);
            transfer->reply->abort();
            transfer->reply->deleteLater();
        }
        emit completed(transfer->id, transfer->generation, transfer->requestId, error, result);
    };
    const auto &session = sessions_[transfer->id];
    const QUrl url(transfer->options.value(QStringLiteral("url")).toString());
    const auto allowed = session.permissions.value(QStringLiteral("network"))
                             .toObject()
                             .value(QStringLiteral("origins"))
                             .toArray();
    if (Plugins::origin(url).isEmpty() || !allowed.contains(Plugins::origin(url)))
    {
        finish(Plugins::error(QStringLiteral("PERMISSION_DENIED"), QStringLiteral("目标 origin 未授权")));
        return;
    }
    const auto verb = transfer->options.value(QStringLiteral("method"))
                          .toString(QStringLiteral("GET"))
                          .toUpper()
                          .toLatin1();
    if (!QList<QByteArray>{"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"}.contains(verb))
    {
        finish(Plugins::error(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("不支持的 HTTP 方法")));
        return;
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    const auto headers = transfer->options.value(QStringLiteral("headers")).toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it)
    {
        const auto name = it.key().toLatin1(), value = it.value().toString().toUtf8();
        if (name.contains('\r') || name.contains('\n') || value.contains('\r') || value.contains('\n') ||
            !it.value().isString())
        {
            finish(Plugins::error(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("无效请求头")));
            return;
        }
        if (name.toLower() == "host" || name.toLower() == "content-length")
            continue;
        request.setRawHeader(name, value);
    }
    QByteArray body;
    const auto bodyValue = transfer->options.value(QStringLiteral("body"));
    if (bodyValue.isObject() || bodyValue.isArray())
    {
        body = bodyValue.isObject() ? QJsonDocument(bodyValue.toObject()).toJson(QJsonDocument::Compact)
                                    : QJsonDocument(bodyValue.toArray()).toJson(QJsonDocument::Compact);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    }
    else
        body = bodyValue.toString().toUtf8();
    if (body.size() > Plugins::MaxMessageBytes)
    {
        finish(Plugins::error(QStringLiteral("RATE_LIMITED"), QStringLiteral("请求体过大")));
        return;
    }
    auto *reply = session.network->sendCustomRequest(request, verb, body);
    transfer->reply = reply;
    transfer->body.clear();
    reply->setReadBufferSize(Plugins::MaxMessageBytes + 1);
    connect(reply, &QNetworkReply::readyRead, this,
            [transfer, reply, finish]
            {
                transfer->body += reply->readAll();
                if (transfer->body.size() > Plugins::MaxMessageBytes)
                    finish(
                        Plugins::error(QStringLiteral("RATE_LIMITED"), QStringLiteral("响应超过 256 KiB")));
            });
    connect(reply, &QNetworkReply::finished, this,
            [this, transfer, reply, finish, k, url, verb]
            {
                if (!transfers_.contains(k))
                    return;
                transfer->body += reply->readAll();
                if (transfer->body.size() > Plugins::MaxMessageBytes)
                {
                    finish(
                        Plugins::error(QStringLiteral("RATE_LIMITED"), QStringLiteral("响应超过 256 KiB")));
                    return;
                }
                const auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
                if (!redirect.isEmpty())
                {
                    if (++transfer->redirects > 5)
                    {
                        finish(Plugins::error(QStringLiteral("NETWORK_ERROR"), QStringLiteral("重定向过多")));
                        return;
                    }
                    const auto target = url.resolved(redirect);
                    if (Plugins::origin(url) != Plugins::origin(target))
                    {
                        auto headers = transfer->options.value(QStringLiteral("headers")).toObject();
                        const auto keys = headers.keys();
                        for (const auto &name : keys)
                            if (name.compare(QStringLiteral("Authorization"), Qt::CaseInsensitive) == 0 ||
                                name.compare(QStringLiteral("Cookie"), Qt::CaseInsensitive) == 0 ||
                                name.compare(QStringLiteral("Proxy-Authorization"), Qt::CaseInsensitive) == 0)
                                headers.remove(name);
                        transfer->options.insert(QStringLiteral("headers"), headers);
                    }
                    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    if (status == 303 || ((status == 301 || status == 302) && verb == "POST"))
                    {
                        transfer->options.insert(QStringLiteral("method"), QStringLiteral("GET"));
                        transfer->options.remove(QStringLiteral("body"));
                    }
                    transfer->options.insert(QStringLiteral("url"), target.toString());
                    reply->deleteLater();
                    transfer->reply.clear();
                    http(transfer);
                    return;
                }
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (reply->error() != QNetworkReply::NoError && !status)
                {
                    finish(Plugins::error(QStringLiteral("NETWORK_ERROR"), reply->errorString()));
                    return;
                }
                QJsonObject headers;
                for (const auto &pair : reply->rawHeaderPairs())
                    headers.insert(QString::fromLatin1(pair.first), QString::fromLatin1(pair.second));
                finish({}, QJsonObject{{QStringLiteral("status"), status},
                                       {QStringLiteral("headers"), headers},
                                       {QStringLiteral("body"), QString::fromUtf8(transfer->body)}});
            });
}
