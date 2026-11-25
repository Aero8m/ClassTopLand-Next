#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QElapsedTimer>
#include <QTimer>
#include <QUrl>
#include <curl/curl.h>
#include <functional>

class CurlTransfer final {
public:
    struct Result {
        int status = 0;
        QUrl redirect;
        bool stalled = false;
    };
    using Body = std::function<bool(int, const QByteArray&, QByteArrayView)>;
    using Completion = std::function<void(const Result&)>;

    CurlTransfer();
    ~CurlTransfer();
    CurlTransfer(const CurlTransfer&) = delete;
    CurlTransfer& operator=(const CurlTransfer&) = delete;

    bool start(const QUrl& url, const QByteArray& range, bool reconnect, int idleTimeoutMs,
               Body body, Completion completion);
    void cancel();

private:
    static size_t header(char* data, size_t size, size_t count, void* context);
    static size_t receive(char* data, size_t size, size_t count, void* context);
    void poll();
    void finish(bool stalled = false);

    CURL* easy_ = nullptr;
    CURLM* multi_ = nullptr;
    QTimer pollTimer_;
    QElapsedTimer idle_;
    int idleTimeoutMs_ = 0;
    long status_ = 0;
    QByteArray contentRange_;
    Body body_;
    Completion completion_;
};
