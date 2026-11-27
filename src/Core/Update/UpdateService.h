#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QObject>
#include <QString>
#include <functional>
#include <memory>

class CurlTransfer;
class QNetworkAccessManager;
class QNetworkReply;
class QTemporaryDir;
class QTimer;
class QUrl;

struct UpdateInstaller {
    QString path;
    QByteArray sha256;
    qint64 size = 0;
};

class UpdateService final : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Checking, Current, Available, Downloading, Ready, Preparing, Failed };
    explicit UpdateService(QObject* parent = nullptr);
    ~UpdateService() override;

    static int currentBuild();
    static QUrl mirrorPage();
    static QString installationError(const QString& stageName);
    bool supported() const;
    State state() const { return state_; }
    int availableBuild() const { return build_; }
    QString notes() const { return notes_; }
    QString message() const { return message_; }
    int percent() const { return percent_; }
    const UpdateInstaller& installer() const { return installer_; }

    bool automaticUpdates() const { return automaticUpdates_; }
    bool readyToInstallOnQuit() const { return !managedUpdate_ && automaticUpdates_ && state_ == State::Ready; }
    void setAutomaticUpdates(bool enabled);
    void installOnQuit();
    void check();
    void downloadAndInstall();
    void cancelDownload();
    void installationDeferred(const QString& message);
    void transferInstaller();
    bool managedUpdate() const { return managedUpdate_; }
    bool checkManaged();
    void downloadManaged();
    void installManaged();
    void finishManaged();

signals:
    void changed();
    void installRequested(bool restartAfterInstall);

private:
    struct Download;
    using JsonHandler = std::function<void(const QByteArray&)>;
    void resolve(const QString& filename, std::function<void(const QUrl&)> handler);
    void readManifest(const QByteArray& contents);
    void getSmall(const QUrl& url, const QByteArray& body, JsonHandler handler);
    void requestDownload();
    void download(const QUrl& url);
    bool readDownload(int status, const QByteArray& contentRange, QByteArrayView bytes);
    bool beginDownloadResponse(int status, const QByteArray& contentRange);
    void finishDownload(const QUrl& url, int status, const QUrl& redirect, bool stalled);
    void scheduleDownloadProgress();
    void retryDownload(const QString& reason);
    void discardDownload();
    void fail(const QString& message);
    void setState(State state, const QString& message = {});
    void checkForUpdate(bool automatic);
    void startDownload(bool automatic);
    void stopRequest();
    void requestInstall(bool restartAfterInstall);
    QString readyMessage() const;

    QNetworkAccessManager* network_;
    QTimer* automaticTimer_;
    QTimer* retryTimer_;
    QTimer* downloadDeadline_;
    QNetworkReply* reply_ = nullptr;
    std::unique_ptr<QTemporaryDir> stage_;
    std::unique_ptr<Download> download_;
    std::unique_ptr<CurlTransfer> transfer_;
    UpdateInstaller installer_;
    QString filename_;
    QString notes_;
    QString message_;
    State state_ = State::Idle;
    int build_ = 0;
    int percent_ = 0;
    bool progressPending_ = false;
    bool automaticUpdates_ = false;
    bool automaticRequest_ = false;
    bool managedUpdate_ = false;
};
