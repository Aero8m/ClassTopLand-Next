#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QTemporaryDir;
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
    bool supported() const;
    State state() const { return state_; }
    int availableBuild() const { return build_; }
    QString notes() const { return notes_; }
    QString message() const { return message_; }
    int percent() const { return percent_; }
    const UpdateInstaller& installer() const { return installer_; }

    void check();
    void downloadAndInstall();
    void cancelDownload();
    void installationDeferred(const QString& message);
    void transferInstaller();

signals:
    void changed();
    void installRequested();

private:
    using JsonHandler = std::function<void(const QByteArray&)>;
    void resolve(const QString& filename, std::function<void(const QUrl&)> handler);
    void readManifest(const QByteArray& contents);
    void getSmall(const QUrl& url, const QByteArray& body, JsonHandler handler);
    void download(const QUrl& url);
    void fail(const QString& message);
    void setState(State state, const QString& message = {});
    void requestInstall();

    QNetworkAccessManager* network_;
    QNetworkReply* reply_ = nullptr;
    std::unique_ptr<QTemporaryDir> stage_;
    UpdateInstaller installer_;
    QString filename_;
    QString notes_;
    QString message_;
    State state_ = State::Idle;
    int build_ = 0;
    int percent_ = 0;
};
