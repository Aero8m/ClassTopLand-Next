#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// Thin async HTTP layer for the device endpoints. Redirects are never followed
// automatically: a redirect on a request that carries the device credential is
// treated as a server error instead of leaking the secret to another origin.
class CloudApi final : public QObject
{
    Q_OBJECT
public:
    struct Reply
    {
        int status = 0;
        QByteArray body;
        // Header names are lower-case.
        QHash<QString, QString> headers;
        // Set for transport failures, oversized responses and unexpected redirects.
        QString transportError;
        // Server error.message from the unified error envelope, when present.
        QString message;

        bool ok() const { return transportError.isEmpty() && status >= 200 && status < 300; }
        QString header(const QString& name) const { return headers.value(name.toLower()); }
    };
    using Handler = std::function<void(const Reply&)>;

    explicit CloudApi(QObject* parent = nullptr);

    // Drops pending replies without invoking their handlers.
    void abortAll();
    bool hasPending() const { return !replies_.isEmpty(); }

    void postJson(const QUrl& url, const QByteArray& bearer, const QJsonObject& body, Handler handler);
    void get(const QUrl& url, const QByteArray& bearer, Handler handler);

private:
    void send(QNetworkReply* reply, Handler handler);

    QNetworkAccessManager* manager_;
    QSet<QNetworkReply*> replies_;
};
