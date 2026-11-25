#include "CurlTransfer.h"

#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <utility>

namespace {
struct CurlRuntime {
    bool initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    ~CurlRuntime() { if (initialized) curl_global_cleanup(); }
};

CURLcode configureProxy(CURL* easy, const QUrl& url)
{
    const auto proxies = QNetworkProxyFactory::proxyForQuery(QNetworkProxyQuery(url));
    for (const auto& proxy : proxies) {
        const auto type = proxy.type();
        if (type == QNetworkProxy::NoProxy) break;
        if (type != QNetworkProxy::HttpProxy && type != QNetworkProxy::HttpCachingProxy
            && type != QNetworkProxy::Socks5Proxy) continue;
        const long mode = type == QNetworkProxy::Socks5Proxy ? CURLPROXY_SOCKS5_HOSTNAME : CURLPROXY_HTTP;
        const auto host = proxy.hostName().toUtf8();
        const auto user = proxy.user().toUtf8();
        const auto password = proxy.password().toUtf8();
        if (curl_easy_setopt(easy, CURLOPT_PROXY, host.constData()) != CURLE_OK
            || curl_easy_setopt(easy, CURLOPT_PROXYPORT, long(proxy.port())) != CURLE_OK
            || curl_easy_setopt(easy, CURLOPT_PROXYTYPE, mode) != CURLE_OK
            || (!user.isEmpty() && curl_easy_setopt(easy, CURLOPT_PROXYUSERNAME, user.constData()) != CURLE_OK)
            || (!password.isEmpty() && curl_easy_setopt(easy, CURLOPT_PROXYPASSWORD, password.constData()) != CURLE_OK))
            return CURLE_FAILED_INIT;
        break;
    }
    return CURLE_OK;
}
}

CurlTransfer::CurlTransfer()
{
    static const CurlRuntime runtime;
    // A synchronous resolver can block the UI even on curl's multi interface.
    if (runtime.initialized && (curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_ASYNCHDNS)) {
        multi_ = curl_multi_init();
        easy_ = curl_easy_init();
    }
    pollTimer_.setInterval(25);
    QObject::connect(&pollTimer_, &QTimer::timeout, &pollTimer_, [this] { poll(); });
}

CurlTransfer::~CurlTransfer()
{
    cancel();
    if (easy_) curl_easy_cleanup(easy_);
    if (multi_) curl_multi_cleanup(multi_);
}

bool CurlTransfer::start(const QUrl& url, const QByteArray& range, bool reconnect, int idleTimeoutMs,
                         Body body, Completion completion)
{
    if (!easy_ || !multi_ || completion_) return false;
    curl_easy_reset(easy_);
    const auto address = url.toEncoded();
    if (curl_easy_setopt(easy_, CURLOPT_URL, address.constData()) != CURLE_OK
        || curl_easy_setopt(easy_, CURLOPT_PROTOCOLS_STR, "http,https") != CURLE_OK
        || curl_easy_setopt(easy_, CURLOPT_RANGE, range.constData()) != CURLE_OK
        || curl_easy_setopt(easy_, CURLOPT_USERAGENT, "ClassTopLand-Next-Updater/1") != CURLE_OK
        || curl_easy_setopt(easy_, CURLOPT_HTTP_VERSION, long(CURL_HTTP_VERSION_1_1)) != CURLE_OK
        || curl_easy_setopt(easy_, CURLOPT_ACCEPT_ENCODING, "identity") != CURLE_OK
        || curl_easy_setopt(easy_, CURLOPT_CONNECTTIMEOUT_MS, 15000L) != CURLE_OK
        || curl_easy_setopt(easy_, CURLOPT_NOSIGNAL, 1L) != CURLE_OK
        || curl_easy_setopt(easy_, CURLOPT_FRESH_CONNECT, reconnect ? 1L : 0L) != CURLE_OK
        || curl_easy_setopt(easy_, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L) != CURLE_OK
        || curl_easy_setopt(easy_, CURLOPT_HEADERFUNCTION, &CurlTransfer::header) != CURLE_OK
        || curl_easy_setopt(easy_, CURLOPT_HEADERDATA, this) != CURLE_OK
        || curl_easy_setopt(easy_, CURLOPT_WRITEFUNCTION, &CurlTransfer::receive) != CURLE_OK
        || curl_easy_setopt(easy_, CURLOPT_WRITEDATA, this) != CURLE_OK
        || configureProxy(easy_, url) != CURLE_OK
        || curl_multi_add_handle(multi_, easy_) != CURLM_OK)
        return false;

    status_ = 0;
    contentRange_.clear();
    body_ = std::move(body);
    completion_ = std::move(completion);
    idleTimeoutMs_ = idleTimeoutMs;
    idle_.start();
    pollTimer_.start();
    return true;
}

void CurlTransfer::cancel()
{
    pollTimer_.stop();
    if (completion_) curl_multi_remove_handle(multi_, easy_);
    body_ = {};
    completion_ = {};
}

size_t CurlTransfer::header(char* data, size_t size, size_t count, void* context)
{
    auto& transfer = *static_cast<CurlTransfer*>(context);
    const size_t length = size * count;
    const QByteArrayView line(data, qsizetype(length));
    if (line.startsWith("HTTP/")) {
        const auto space = line.indexOf(' ');
        transfer.status_ = space >= 0 ? line.sliced(space + 1).first(qMin(qsizetype(3), line.size() - space - 1)).toInt() : 0;
        transfer.contentRange_.clear();
    } else if (line.size() >= 14 && line.first(14).compare("Content-Range:", Qt::CaseInsensitive) == 0) {
        transfer.contentRange_ = line.sliced(14).trimmed().toByteArray();
    }
    return length;
}

size_t CurlTransfer::receive(char* data, size_t size, size_t count, void* context)
{
    auto& transfer = *static_cast<CurlTransfer*>(context);
    const size_t length = size * count;
    if (!length) return 0;
    if (transfer.status_ != 200 && transfer.status_ != 206) return 0;
    if (!transfer.body_(int(transfer.status_), transfer.contentRange_, QByteArrayView(data, qsizetype(length))))
        return 0;
    transfer.idle_.restart();
    return length;
}

void CurlTransfer::poll()
{
    int running = 0;
    if (curl_multi_perform(multi_, &running) != CURLM_OK || !running) {
        finish();
    } else if (idle_.elapsed() >= idleTimeoutMs_) {
        finish(true);
    }
}

void CurlTransfer::finish(bool stalled)
{
    Result result;
    result.stalled = stalled;
    long status = 0;
    curl_easy_getinfo(easy_, CURLINFO_RESPONSE_CODE, &status);
    result.status = int(status);
    char* redirect = nullptr;
    if (curl_easy_getinfo(easy_, CURLINFO_REDIRECT_URL, &redirect) == CURLE_OK && redirect)
        result.redirect = QUrl::fromEncoded(redirect, QUrl::StrictMode);
    // Remove the completed easy handle and its queued result before the callback may start another range.
    curl_multi_remove_handle(multi_, easy_);
    pollTimer_.stop();
    body_ = {};
    auto completion = std::exchange(completion_, {});
    completion(result);
}
