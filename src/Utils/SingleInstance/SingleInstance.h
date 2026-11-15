#pragma once

#include <QDir>
#include <QLockFile>

// Keep the data directory locked until all business objects have been destroyed.
// This also works before QApplication, so duplicate launches cannot migrate or
// save configuration while the original instance is still running.
class SingleInstance final
{
public:
    enum class Result { Acquired, AlreadyRunning, Error };

    explicit SingleInstance(const QString& dataDirectory)
        : directory_(QDir(dataDirectory).absolutePath()),
          lock_(QDir(directory_).filePath(QStringLiteral("instance.lock")))
    {
        // A running tray application must never lose its lock based on age.
        lock_.setStaleLockTime(0);
    }

    Result acquire(QString* error = nullptr)
    {
        if (error) error->clear();
        if (!QDir().mkpath(directory_)) {
            if (error) *error = QStringLiteral("无法创建应用数据目录：%1").arg(directory_);
            return Result::Error;
        }
        if (lock_.tryLock(0)) return Result::Acquired;
        if (lock_.error() == QLockFile::LockFailedError) return Result::AlreadyRunning;
        if (error) *error = QStringLiteral("无法锁定应用数据目录，请检查目录权限和磁盘空间：%1").arg(directory_);
        return Result::Error;
    }

    void release() { lock_.unlock(); }

private:
    QString directory_;
    QLockFile lock_;
};
