#include "AgentCatalog.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QSet>
#include <algorithm>

using namespace Qt::StringLiterals;

AgentCatalog::AgentCatalog(QObject *parent) : QObject(parent) {}

void AgentCatalog::fetch(const AgentProvider &provider, const QString &key)
{
    cancel();
    officialIds_.clear();
    officialError_.clear();
    catalogId_.clear();
    for (const auto &preset : agentPresets())
        if (preset.id == provider.preset) catalogId_ = preset.catalogId;
    auto endpoint = provider.connection().endpoint();
    if (endpoint.isEmpty()) {
        emit fetched({}, false, u"请填写有效的 Base URL"_s);
        return;
    }
    auto path = endpoint.path(QUrl::FullyEncoded);
    path.chop(QStringLiteral("/chat/completions").size());
    path += u"/models"_s;
    if (provider.preset == "opencode-go"_L1 && endpoint.host() == "opencode.ai"_L1 &&
        path == "/inference/go/openai/v1/models"_L1)
        path = u"/inference/go/v1/models"_s;
    endpoint.setPath(path, QUrl::StrictMode);
    request(endpoint, key, true, 4 * 1024 * 1024);
}

void AgentCatalog::cancel()
{
    if (!reply_) return;
    auto *reply = reply_;
    reply_ = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
    buffer_.clear();
}

void AgentCatalog::request(const QUrl &url, const QString &key, bool official, int limit)
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "ClassTopLand-Next-Agent/1.0");
    if (official && !key.isEmpty()) request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    buffer_.clear();
    auto *reply = network_.get(request);
    reply_ = reply;
    const auto drain = [this, reply, limit] {
        if (reply->bytesAvailable() > limit - buffer_.size()) {
            reply->setProperty("tooLarge", true);
            reply->abort();
        } else {
            buffer_ += reply->readAll();
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, drain);
    connect(reply, &QNetworkReply::finished, this, [this, reply, official, limit] {
        const bool oversized = reply->property("tooLarge").toBool() ||
                               reply->bytesAvailable() > limit - buffer_.size();
        if (!oversized && reply->isOpen()) buffer_ += reply->readAll();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonObject value;
        QString error;
        if (oversized) error = u"模型列表响应过大"_s;
        else if (status && (status < 200 || status >= 300))
            error = u"模型列表请求失败（HTTP %1）"_s.arg(status);
        else if (reply->error() != QNetworkReply::NoError) error = u"模型列表网络请求失败"_s;
        else {
            const auto document = QJsonDocument::fromJson(buffer_);
            if (!document.isObject()) error = u"模型列表格式无法识别"_s;
            else value = document.object();
        }
        reply_ = nullptr;
        buffer_.clear();
        reply->deleteLater();
        if (official) officialFinished(value, error);
        else {
            if (error.isEmpty()) catalogCache_ = value;
            catalogFinished(value);
        }
    });
}

void AgentCatalog::officialFinished(const QJsonObject &value, const QString &error)
{
    officialError_ = error;
    QSet<QString> seen;
    for (const auto &entry : value.value(u"data"_s).toArray()) {
        const auto id = entry.toObject().value(u"id"_s).toString();
        if (!id.trimmed().isEmpty() && id.size() <= 200 && !id.contains(QChar::Null) && !seen.contains(id)) {
            seen.insert(id);
            officialIds_.append(id);
        }
    }
    if (officialIds_.isEmpty() && officialError_.isEmpty()) officialError_ = u"供应商未返回模型"_s;
    if (catalogId_.isEmpty() || !catalogCache_.isEmpty()) {
        catalogFinished(catalogCache_);
        return;
    }
    request(QUrl(u"https://models.dev/api.json"_s), {}, false, 16 * 1024 * 1024);
}

void AgentCatalog::catalogFinished(const QJsonObject &value)
{
    const auto metadata = catalogId_.isEmpty() ? QJsonObject()
        : value.value(catalogId_).toObject().value(u"models"_s).toObject();
    const bool catalogOnly = officialIds_.isEmpty();
    auto ids = catalogOnly ? metadata.keys() : officialIds_;
    ids.sort(Qt::CaseInsensitive);
    QList<AgentModelEntry> models;
    for (const auto &id : std::as_const(ids)) {
        if (id.isEmpty() || id.size() > 200 || id.contains(QChar::Null)) continue;
        const auto data = metadata.value(id).toObject();
        models.append({id, data.value(u"name"_s).toString(id),
                       qMax(0, data.value(u"limit"_s).toObject().value(u"context"_s).toInt())});
    }
    emit fetched(models, catalogOnly, officialError_);
}

