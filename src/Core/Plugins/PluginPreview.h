#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <functional>
#include <memory>
class PluginPreview final : public QObject
{
    Q_OBJECT
  public:
    using Completion = std::function<void(const QJsonObject &, const QJsonValue &)>;
    explicit PluginPreview(QObject *parent = nullptr);
    ~PluginPreview() override;
    void start(const QString &source, Completion completion);
    void request(const QJsonObject &arguments, Completion completion);
    void stop();
    QString plugin() const
    {
        return plugin_;
    }
    QByteArray fingerprint() const
    {
        return fingerprint_;
    }
    bool running() const
    {
        return process_.state() != QProcess::NotRunning;
    }

  private:
    void consume();
    void failAll(const QString &message);
    QProcess process_;
    std::unique_ptr<QTemporaryDir> directory_;
    QHash<QString, Completion> pending_;
    Completion started_;
    QString plugin_;
    QByteArray fingerprint_, buffer_;
    quint64 sequence_ = 0, generation_ = 0;
};
int runPluginPreview(int argc, char **argv);
