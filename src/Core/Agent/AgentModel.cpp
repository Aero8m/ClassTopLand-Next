#include "AgentModel.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <utility>

namespace
{
// SSE repeats metadata for every token. Its wire size is not the model output size.
constexpr qsizetype maxWireBytes = 128 * 1024 * 1024;
constexpr qsizetype maxPayloadBytes = 16 * 1024 * 1024;
constexpr qsizetype maxEventBytes = 4 * 1024 * 1024;
constexpr qsizetype readChunkBytes = 64 * 1024;
}

const QList<AgentPreset> &agentPresets()
{
    static const QList<AgentPreset> presets{
        {QStringLiteral("deepseek"), QStringLiteral("DeepSeek"), QStringLiteral("https://api.deepseek.com"), QStringLiteral("deepseek")},
        {QStringLiteral("kimi"), QStringLiteral("Kimi"), QStringLiteral("https://api.moonshot.cn/v1"), QStringLiteral("moonshotai-cn")},
        {QStringLiteral("glm"), QStringLiteral("GLM"), QStringLiteral("https://open.bigmodel.cn/api/paas/v4"), QStringLiteral("zhipuai")},
        {QStringLiteral("opencode-go"), QStringLiteral("OpenCode Go"),
         QStringLiteral("https://opencode.ai/inference/go/openai/v1"), QStringLiteral("opencode-go")},
        {QStringLiteral("qwen"), QStringLiteral("千问百炼"),
         QStringLiteral("https://dashscope.aliyuncs.com/compatible-mode/v1"), QStringLiteral("alibaba-cn")},
        {QStringLiteral("openai"), QStringLiteral("OpenAI"), QStringLiteral("https://api.openai.com/v1"), QStringLiteral("openai")},
        {QStringLiteral("custom"), QStringLiteral("自定义 OpenAI 兼容"), {}, {}}
    };
    return presets;
}

QUrl AgentConnection::endpoint() const
{
    auto url = QUrl(baseUrl.trimmed(), QUrl::StrictMode);
    const auto scheme = url.scheme();
    const auto host = url.host();
    const bool loopback = host == QStringLiteral("localhost") || host == QStringLiteral("127.0.0.1") ||
                          host == QStringLiteral("::1");
    if (!url.isValid() || host.isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() ||
        (scheme != QStringLiteral("https") &&
         !(scheme == QStringLiteral("http") && loopback && preset == QStringLiteral("custom"))))
        return {};
    auto path = url.path(QUrl::FullyEncoded);
    while (path.endsWith(QLatin1Char('/'))) path.chop(1);
    const auto suffix = QStringLiteral("/chat/completions");
    if (!path.endsWith(suffix)) path += suffix;
    url.setPath(path, QUrl::StrictMode);
    return url;
}

AgentConnection AgentProvider::connection(const QString &model) const
{
    return {id, name, preset, baseUrl, model};
}

QStringList AgentConnection::reasoningEfforts() const
{
    if (preset == QStringLiteral("deepseek") || endpoint().host() == QStringLiteral("api.deepseek.com"))
        return {QStringLiteral("none"), QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")};
    return {};
}

QString AgentConnection::effectiveReasoningEffort() const
{
    const auto supported = reasoningEfforts();
    if (supported.isEmpty()) return {};
    return supported.contains(reasoningEffort) ? reasoningEffort : QStringLiteral("high");
}

QJsonObject AgentConnection::requestBody(const QJsonArray &messages, const QJsonArray &tools) const
{
    QJsonObject body{{QStringLiteral("model"), model}, {QStringLiteral("messages"), messages},
                     {QStringLiteral("stream"), true}};
    if (!tools.isEmpty()) body.insert(QStringLiteral("tools"), tools);
    const auto effort = effectiveReasoningEffort();
    if (!effort.isEmpty()) {
        body.insert(QStringLiteral("thinking"), QJsonObject{{QStringLiteral("type"),
                    effort == QStringLiteral("none") ? QStringLiteral("disabled") : QStringLiteral("enabled")}});
        if (effort != QStringLiteral("none")) body.insert(QStringLiteral("reasoning_effort"), effort);
    }
    return body;
}

QJsonObject AgentProvider::toJson() const
{
    QJsonArray entries;
    for (const auto &model : models)
        entries.append(QJsonObject{{QStringLiteral("id"), model.id}, {QStringLiteral("name"), model.name},
                                   {QStringLiteral("contextWindow"), model.contextWindow}});
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name}, {QStringLiteral("preset"), preset},
            {QStringLiteral("baseUrl"), baseUrl}, {QStringLiteral("models"), entries}};
}

AgentProvider AgentProvider::fromJson(const QJsonObject &value)
{
    AgentProvider provider{value.value(QStringLiteral("id")).toString(), value.value(QStringLiteral("name")).toString(),
                           value.value(QStringLiteral("preset")).toString(), value.value(QStringLiteral("baseUrl")).toString(), {}};
    for (const auto &entry : value.value(QStringLiteral("models")).toArray()) {
        const auto model = entry.toObject();
        provider.models.append({model.value(QStringLiteral("id")).toString(),
                                model.value(QStringLiteral("name")).toString(),
                                model.value(QStringLiteral("contextWindow")).toInt()});
    }
    const auto legacy = value.value(QStringLiteral("model")).toString();
    if (!value.contains(QStringLiteral("models")) && !legacy.isEmpty())
        provider.models.append({legacy, legacy});
    return provider;
}

AgentModel::AgentModel(QObject *parent) : QObject(parent) {}

void AgentModel::request(const AgentConnection &connection, const QString &key, const QString &session,
                         const QJsonArray &messages, const QJsonArray &tools)
{
    cancel();
    buffer_.clear();
    event_.clear();
    content_.clear();
    reasoning_.clear();
    const auto effort = connection.effectiveReasoningEffort();
    reasoningPresent_ = !effort.isEmpty() && effort != QStringLiteral("none");
    reason_.clear();
    error_.clear();
    calls_.clear();
    bytes_ = 0;
    payloadBytes_ = 0;
    readScheduled_ = false;
    ended_ = false;
    QNetworkRequest request(connection.endpoint());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "text/event-stream");
    request.setRawHeader("User-Agent", "ClassTopLand-Next-Agent/1.0");
    if (!key.isEmpty()) request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    if (connection.preset == QStringLiteral("opencode-go"))
        request.setRawHeader("x-opencode-session", session.toUtf8());
    request.setTransferTimeout(90000);

    reply_ = network_.post(request, QJsonDocument(connection.requestBody(messages, tools)).toJson(QJsonDocument::Compact));
    reply_->setReadBufferSize(readChunkBytes);
    connect(reply_, &QNetworkReply::readyRead, this, &AgentModel::consume);
    connect(reply_, &QNetworkReply::finished, this, &AgentModel::consume);
}

void AgentModel::cancel()
{
    if (!reply_) return;
    auto *reply = reply_;
    reply_ = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void AgentModel::consume()
{
    if (!reply_) return;
    const QPointer<QNetworkReply> reading = reply_;
    const auto status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status < 200 || status >= 300) {
        reply_->readAll();
        if (reply_->isFinished()) finish();
        return;
    }
    qsizetype processed = 0;
    while (reply_->bytesAvailable() > 0 && processed < 4 * readChunkBytes && error_.isEmpty() && !ended_) {
        const auto data = reply_->read(readChunkBytes);
        if (data.isEmpty()) break;
        processed += data.size();
        bytes_ += data.size();
        if (bytes_ > maxWireBytes) {
            error_ = QStringLiteral("模型流式传输超过 128 MiB，请拆分任务后重试");
            break;
        }
        buffer_ += data;
        for (auto newline = buffer_.indexOf('\n'); newline >= 0 && !ended_ && error_.isEmpty();
             newline = buffer_.indexOf('\n')) {
            auto line = buffer_.left(newline);
            buffer_.remove(0, newline + 1);
            if (line.endsWith('\r')) line.chop(1);
            if (line.size() + event_.size() > maxEventBytes) {
                error_ = QStringLiteral("单个模型流式事件超过 4 MiB");
                break;
            }
            if (line.isEmpty()) {
                receiveEvent();
                if (!reading || reply_ != reading) return;
            } else if (line.startsWith("data:")) {
                line.remove(0, 5);
                if (line.startsWith(' ')) line.remove(0, 1);
                if (!event_.isEmpty()) event_ += '\n';
                event_ += line;
            }
        }
        if (buffer_.size() + event_.size() > maxEventBytes)
            error_ = QStringLiteral("单个模型流式事件超过 4 MiB");
    }
    if (!error_.isEmpty() || ended_ || (reply_->isFinished() && reply_->bytesAvailable() == 0)) {
        finish();
    } else if (reply_->bytesAvailable() > 0 && !readScheduled_) {
        readScheduled_ = true;
        const QPointer<QNetworkReply> expected = reply_;
        QTimer::singleShot(0, this, [this, expected] {
            if (expected && reply_ == expected) {
                readScheduled_ = false;
                consume();
            }
        });
    }
}

void AgentModel::receiveEvent()
{
    if (event_.isEmpty()) return;
    const auto data = std::exchange(event_, QByteArray());
    if (data == "[DONE]") {
        ended_ = true;
        return;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(data, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject() ||
        document.object().contains(QStringLiteral("error"))) {
        error_ = QStringLiteral("模型返回无效的流式事件或服务错误");
        return;
    }
    const auto choices = document.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) return;
    const auto choice = choices.first().toObject();
    const auto delta = choice.value(QStringLiteral("delta")).toObject();
    const auto reason = choice.value(QStringLiteral("finish_reason")).toString();
    if (!reason.isEmpty()) reason_ = reason;
    const auto text = delta.value(QStringLiteral("content")).toString();
    const auto reasoning = delta.value(QStringLiteral("reasoning_content")).toString();
    qsizetype added = text.toUtf8().size() + reasoning.toUtf8().size();
    for (const auto &entry : delta.value(QStringLiteral("tool_calls")).toArray()) {
        const auto call = entry.toObject();
        const auto function = call.value(QStringLiteral("function")).toObject();
        added += call.value(QStringLiteral("id")).toString().toUtf8().size() +
                 function.value(QStringLiteral("name")).toString().toUtf8().size() +
                 function.value(QStringLiteral("arguments")).toString().toUtf8().size();
    }
    if (added > maxPayloadBytes - payloadBytes_) {
        error_ = QStringLiteral("模型正文、思考和工具参数合计超过 16 MiB，请拆分任务后重试");
        return;
    }
    payloadBytes_ += added;
    if (delta.contains(QStringLiteral("reasoning_content"))) reasoningPresent_ = true;
    if (!reasoning.isEmpty()) {
        reasoning_ += reasoning;
        const QPointer<QNetworkReply> reading = reply_;
        emit reasoningReceived(reasoning);
        if (!reading || reply_ != reading) return;
    }
    for (const auto &entry : delta.value(QStringLiteral("tool_calls")).toArray()) {
        const auto call = entry.toObject();
        const auto index = call.value(QStringLiteral("index")).toInt(-1);
        if (index < 0 || index >= 16) {
            error_ = QStringLiteral("模型返回无效的工具调用序号");
            return;
        }
        auto &pending = calls_[index];
        const auto id = call.value(QStringLiteral("id")).toString();
        const auto type = call.value(QStringLiteral("type")).toString();
        const auto previousId = pending.value(QStringLiteral("id")).toString();
        if ((!type.isEmpty() && type != QStringLiteral("function")) ||
            (!id.isEmpty() && !previousId.isEmpty() && id != previousId)) {
            error_ = QStringLiteral("模型返回冲突或不支持的工具调用");
            return;
        }
        if (!id.isEmpty()) pending.insert(QStringLiteral("id"), id);
        const auto function = call.value(QStringLiteral("function")).toObject();
        for (const auto &field : {QStringLiteral("name"), QStringLiteral("arguments")})
            pending.insert(field, pending.value(field).toString() + function.value(field).toString());
    }
    if (!text.isEmpty()) {
        content_ += text;
        emit textReceived(text);
    }
}

void AgentModel::finish()
{
    if (!reply_) return;
    auto *reply = std::exchange(reply_, nullptr);
    reply->disconnect(this);
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (error_.isEmpty()) {
        if (status && (status < 200 || status >= 300))
            error_ = QStringLiteral("模型服务请求失败（HTTP %1）").arg(status);
        else if (!ended_ && (reply->error() != QNetworkReply::NoError || reason_.isEmpty()))
            error_ = QStringLiteral("连接中断或流式响应未完成");
        else if (reason_ == QStringLiteral("length") || reason_ == QStringLiteral("content_filter"))
            error_ = QStringLiteral("模型输出被截断或过滤");
        else if (reason_ == QStringLiteral("tool_calls") && calls_.isEmpty())
            error_ = QStringLiteral("模型未提供声明的工具调用");
    }
    if (!reply->isFinished()) reply->abort();
    reply->deleteLater();
    QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")},
                        {QStringLiteral("content"), content_}};
    if (reasoningPresent_) message.insert(QStringLiteral("reasoning_content"), reasoning_);
    if (!calls_.isEmpty() && error_.isEmpty()) {
        QJsonArray calls;
        QSet<QString> ids;
        for (const auto &call : std::as_const(calls_)) {
            const auto id = call.value(QStringLiteral("id")).toString();
            const auto name = call.value(QStringLiteral("name")).toString();
            if (id.isEmpty() || name.isEmpty() || ids.contains(id) || reason_ != QStringLiteral("tool_calls")) {
                error_ = QStringLiteral("工具调用缺少标识或未完整结束");
                break;
            }
            ids.insert(id);
            calls.append(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("type"), QStringLiteral("function")},
                                     {QStringLiteral("function"),
                                      QJsonObject{{QStringLiteral("name"), name},
                                                  {QStringLiteral("arguments"), call.value(QStringLiteral("arguments"))}}}});
        }
        message.insert(QStringLiteral("tool_calls"), calls);
    }
    emit completed(message, error_);
}

