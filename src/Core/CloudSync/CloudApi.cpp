#include "CloudApi.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <memory>

namespace {
constexpr qsizetype kMaxResponseBytes = 2 * 1024 * 1024;

struct Transfer
{
    QByteArray body;
    bool tooLarge = false;
};

QString serverMessage(const QByteArray& body)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return {};
    return document.object()
        .value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("message"))
        .toString();
}
} // namespace

CloudApi::CloudApi(QObject* parent)
    : QObject(parent), manager_(new QNetworkAccessManager(this))
{
}

void CloudApi::abortAll()
{
    const QSet<QNetworkReply*> replies = replies_;
    replies_.clear();
    for (auto* reply : replies) {
        // Disconnect first: aborting emits finished(), and a queued callback must
        // not run while the owner is shutting down or cancelling a cycle.
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void CloudApi::postJson(const QUrl& url, const QByteArray& bearer, const QJsonObject& body,
                        Handler handler)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ClassTopLand-Next"));
    if (!bearer.isEmpty()) request.setRawHeader("Authorization", "Bearer " + bearer);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    send(manager_->post(request, payload), std::move(handler));
}

void CloudApi::get(const QUrl& url, const QByteArray& bearer, Handler handler)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ClassTopLand-Next"));
    if (!bearer.isEmpty()) request.setRawHeader("Authorization", "Bearer " + bearer);
    send(manager_->get(request), std::move(handler));
}

void CloudApi::send(QNetworkReply* reply, Handler handler)
{
    replies_.insert(reply);
    reply->setReadBufferSize(kMaxResponseBytes + 1);
    auto transfer = std::make_shared<Transfer>();
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, transfer] {
        if (!replies_.contains(reply)) return;
        transfer->body += reply->readAll();
        if (transfer->body.size() > kMaxResponseBytes) {
            transfer->tooLarge = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, transfer, handler = std::move(handler)]() mutable {
                if (!replies_.contains(reply)) return;
                replies_.remove(reply);
                Reply result;
                result.body = transfer->body + reply->readAll();
                const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
                if (status.isValid()) result.status = status.toInt();
                const auto pairs = reply->rawHeaderPairs();
                for (const auto& pair : pairs) {
                    result.headers.insert(QString::fromLatin1(pair.first).toLower(),
                                          QString::fromLatin1(pair.second));
                }
                const QUrl redirect =
                    reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
                if (transfer->tooLarge || result.body.size() > kMaxResponseBytes) {
                    result.transportError = QStringLiteral("服务器响应超过 2 MiB");
                } else if (!redirect.isEmpty()) {
                    result.transportError = QStringLiteral("服务器返回了未预期的重定向");
                } else if (reply->error() != QNetworkReply::NoError && result.status == 0) {
                    result.transportError = reply->errorString();
                }
                result.message = serverMessage(result.body);
                reply->deleteLater();
                handler(result);
            });
}
