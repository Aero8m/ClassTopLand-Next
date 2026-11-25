#include "UpdateService.h"

#include <BuildInfo.h>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <chrono>

#ifdef Q_OS_WIN
#include "../../Utils/Update/UpdatePlatform.h"
#endif

namespace {
constexpr auto FirstAutomaticCheck = std::chrono::seconds(15);
constexpr auto AutomaticCheckInterval = std::chrono::hours(6);
constexpr qint64 MaxJsonBytes = 64 * 1024;
constexpr qint64 MaxInstallerBytes = 256 * 1024 * 1024;
constexpr auto Host = "autobuild.aero8m.cn";
const QString Directory = QStringLiteral("/ClassTopLand-Next/");

QNetworkRequest requestFor(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ClassTopLand-Next-Updater/1"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setMaximumRedirectsAllowed(3);
    request.setTransferTimeout(15000);
    return request;
}

bool ok(QNetworkReply* reply)
{
    return reply->error() == QNetworkReply::NoError
        && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
}
}

UpdateService::UpdateService(QObject* parent)
    : QObject(parent), network_(new QNetworkAccessManager(this)), automaticTimer_(new QTimer(this))
{
    automaticTimer_->setSingleShot(true);
    connect(automaticTimer_, &QTimer::timeout, this, [this] {
        if (!automaticUpdates_) return;
        automaticTimer_->start(AutomaticCheckInterval);
        checkForUpdate(true);
    });
}

UpdateService::~UpdateService()
{
    stopRequest();
}

int UpdateService::currentBuild()
{
    return CTX_UPDATE_BUILD_NUMBER;
}

QUrl UpdateService::mirrorPage()
{
    return QUrl(QStringLiteral("https://autobuild.aero8m.cn/ClassTopLand-Next"));
}

bool UpdateService::supported() const
{
#ifdef Q_OS_WIN
    return currentBuild() > 0 && UpdatePlatform::installed();
#else
    return false;
#endif
}

void UpdateService::setState(State state, const QString& message)
{
    state_ = state;
    message_ = message;
    emit changed();
}

void UpdateService::fail(const QString& message)
{
    stage_.reset();
    installer_.path.clear();
    setState(state_ == State::Downloading ? State::Available : State::Failed, message);
}

void UpdateService::setAutomaticUpdates(bool enabled)
{
    if (automaticUpdates_ == enabled || (enabled && !supported())) return;
    automaticUpdates_ = enabled;
    if (enabled) {
        automaticTimer_->start(FirstAutomaticCheck);
    } else {
        automaticTimer_->stop();
        if (automaticRequest_ && state_ == State::Downloading) {
            cancelDownload();
            return;
        }
        if (automaticRequest_ && state_ == State::Checking) {
            stopRequest();
            setState(State::Idle);
            return;
        }
    }
    if (state_ == State::Ready) setState(State::Ready, readyMessage());
    else emit changed();
}

QString UpdateService::readyMessage() const
{
    return automaticUpdates_ ? tr("安装包已校验，将在退出时安装。")
                             : tr("安装包已校验，可以安装更新。");
}

void UpdateService::installOnQuit()
{
    if (readyToInstallOnQuit()) requestInstall(false);
}

void UpdateService::check()
{
    checkForUpdate(false);
}

void UpdateService::checkForUpdate(bool automatic)
{
    if (!supported() || reply_ || state_ == State::Preparing || (automatic && state_ == State::Ready)) return;
    automaticRequest_ = automatic;
    stage_.reset();
    installer_ = {};
    filename_.clear();
    notes_.clear();
    build_ = 0;
    setState(State::Checking, tr("正在检查更新…"));
    resolve(QStringLiteral("update.json"), [this](const QUrl& url) {
        getSmall(url, {}, [this](const QByteArray& data) { readManifest(data); });
    });
}

void UpdateService::resolve(const QString& filename, std::function<void(const QUrl&)> handler)
{
    const QString path = Directory + filename;
    const auto body = QJsonDocument(QJsonObject{{QStringLiteral("path"), path}}).toJson(QJsonDocument::Compact);
    const QUrl endpoint(QStringLiteral("https://autobuild.aero8m.cn/api/fs/get"));
    getSmall(endpoint, body, [this, path, handler = std::move(handler)](const QByteArray& contents) {
        const auto response = QJsonDocument::fromJson(contents).object();
        const int code = response.value(QStringLiteral("code")).toInt();
        if (code == 404 || (code == 500 && response.value(QStringLiteral("message")).toString()
                            .endsWith(QStringLiteral("object not found"), Qt::CaseInsensitive))) {
            fail(tr("镜像暂未提供所请求的更新文件。"));
            return;
        }
        const auto data = response.value(QStringLiteral("data")).toObject();
        const QUrl url(data.value(QStringLiteral("raw_url")).toString(), QUrl::StrictMode);
        const QString returnedPath = url.path();
        if (code != 200 || !url.isValid()
            || url.scheme() != QStringLiteral("https") || url.host() != QLatin1String(Host)
            || url.port(443) != 443 || !url.userInfo().isEmpty() || url.hasFragment()
            || (returnedPath != QStringLiteral("/p") + path && returnedPath != QStringLiteral("/d") + path)) {
            fail(tr("镜像返回了无效的下载地址。"));
            return;
        }
        handler(url);
    });
}

void UpdateService::getSmall(const QUrl& url, const QByteArray& body, JsonHandler handler)
{
    auto request = requestFor(url);
    request.setRawHeader("Cache-Control", "no-cache");
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    auto* reply = body.isEmpty() ? network_->get(request) : network_->post(request, body);
    reply->setReadBufferSize(MaxJsonBytes + 1);
    reply_ = reply;
    auto data = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [reply, data] {
        data->append(reply->read(MaxJsonBytes + 1 - data->size()));
        if (data->size() > MaxJsonBytes) reply->abort();
    });
    QTimer::singleShot(30000, reply, [reply] { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, data, handler = std::move(handler)] {
        reply_ = nullptr;
        reply->deleteLater();
        if (data->size() > MaxJsonBytes) {
            fail(tr("镜像返回的数据过大。"));
        } else if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404) {
            fail(tr("镜像暂未提供所请求的更新文件。"));
        } else if (!ok(reply)) {
            fail(tr("无法读取 Autobuild 镜像，请稍后重试。"));
        } else {
            handler(*data);
        }
    });
}

void UpdateService::readManifest(const QByteArray& contents)
{
    const auto value = QJsonDocument::fromJson(contents).object();
    const QString commit = value.value(QStringLiteral("commit")).toString();
    const QString filename = value.value(QStringLiteral("installerFile")).toString();
    const QString digest = value.value(QStringLiteral("sha256")).toString();
    const auto notes = value.value(QStringLiteral("releaseNotes"));
    const int build = value.value(QStringLiteral("buildNumber")).toInt();
    const auto size = value.value(QStringLiteral("installerSize")).toInteger();
    static const QRegularExpression commitPattern(QStringLiteral("\\A[0-9a-f]{7,40}\\z"));
    static const QRegularExpression digestPattern(QStringLiteral("\\A[0-9a-f]{64}\\z"));
    if (value.value(QStringLiteral("schemaVersion")).toInt() != 1 || build <= 0
        || size <= 0 || size > MaxInstallerBytes || !notes.isString()
        || !commitPattern.match(commit).hasMatch() || !digestPattern.match(digest).hasMatch()
        || filename != QStringLiteral("ClassTopLand-Next-setup_build_%1-amd64.exe").arg(commit)) {
        fail(tr("更新清单格式无效或版本不受支持。"));
        return;
    }
    if (build <= currentBuild()) {
        setState(State::Current, tr("当前已是最新版本。"));
        return;
    }
    build_ = build;
    filename_ = filename;
    notes_ = notes.toString();
    installer_ = {{}, QByteArray::fromHex(digest.toLatin1()), size};
    setState(State::Available, tr("发现新版本，构建 %1。").arg(build));
    if (automaticRequest_ && automaticUpdates_) startDownload(true);
}

void UpdateService::downloadAndInstall()
{
    if (state_ == State::Ready) requestInstall(true);
    else if (state_ == State::Available) startDownload(false);
}

void UpdateService::startDownload(bool automatic)
{
    automaticRequest_ = automatic;
    percent_ = 0;
    setState(State::Downloading, tr("正在下载安装包…"));
    resolve(filename_, [this](const QUrl& url) { download(url); });
}

void UpdateService::cancelDownload()
{
    if (state_ != State::Downloading) return;
    stopRequest();
    stage_.reset();
    setState(State::Available, tr("下载已取消。"));
}

void UpdateService::stopRequest()
{
    if (!reply_) return;
    auto* reply = reply_;
    reply_ = nullptr;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void UpdateService::download(const QUrl& url)
{
    stage_ = std::make_unique<QTemporaryDir>(QDir::temp().filePath(QStringLiteral("ClassTopLand-Next-update-XXXXXX")));
    if (!stage_->isValid()) {
        fail(tr("无法为更新创建暂存目录。"));
        return;
    }

    const QString path = stage_->filePath(filename_);
    struct Download {
        explicit Download(const QString& name) : file(name) {}
        QSaveFile file;
        QCryptographicHash hash{QCryptographicHash::Sha256};
        qint64 received = 0;
        bool writeFailed = false;
    };
    auto output = std::make_shared<Download>(path);
    if (!output->file.open(QIODevice::WriteOnly)) {
        fail(tr("无法写入安装包，请检查暂存目录权限和磁盘空间。"));
        return;
    }
    auto request = requestFor(url);
    request.setRawHeader("Accept-Encoding", "identity");
    auto* reply = network_->get(request);
    reply->setReadBufferSize(256 * 1024);
    reply_ = reply;
    connect(reply, &QIODevice::readyRead, this, [this, reply, output] {
        while (reply->bytesAvailable() > 0) {
            const auto chunk = reply->read(128 * 1024);
            if (chunk.isEmpty()) break;
            output->received += chunk.size();
            if (output->received > installer_.size || output->file.write(chunk) != chunk.size()) {
                output->writeFailed = true;
                reply->abort();
                return;
            }
            output->hash.addData(chunk);
        }
        const int percent = int(output->received * 100 / installer_.size);
        if (percent != percent_) {
            percent_ = percent;
            emit changed();
        }
    });
    QTimer::singleShot(15 * 60 * 1000, reply, [reply] { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, output, path] {
        reply_ = nullptr;
        reply->deleteLater();
        const QString error = [this, reply, &output] {
            if (output->writeFailed) return tr("安装包大小超限或磁盘写入失败。");
            if (!ok(reply)) return tr("安装包下载失败，请稍后重试。");
            if (output->received != installer_.size || output->hash.result() != installer_.sha256)
                return tr("安装包大小或 SHA-256 校验失败。");
            if (!output->file.commit()) return tr("无法保存已下载的安装包。");
            return QString();
        }();
        if (!error.isEmpty()) {
            if (output->file.isOpen()) {
                output->file.cancelWriting();
                output->file.commit();
            }
            fail(error);
            return;
        }
        installer_.path = path;
        if (automaticRequest_) setState(State::Ready, readyMessage());
        else requestInstall(true);
    });
}

void UpdateService::requestInstall(bool restartAfterInstall)
{
    setState(State::Preparing, tr("正在准备退出并安装…"));
    emit installRequested(restartAfterInstall);
}

void UpdateService::installationDeferred(const QString& message)
{
    if (state_ == State::Preparing)
        setState(State::Ready, message);
}

void UpdateService::transferInstaller()
{
    stage_->setAutoRemove(false);
}
