#pragma once

#include "CloudTypes.h"

#include <QByteArray>
#include <QString>

class QLockFile;

// Owns <data>/cloud/state.json plus the crash-recovery content files under
// <data>/cloud/pending/. Writes are atomic; a damaged state file is reported so
// the caller can stop instead of silently replacing credentials.
class CloudStateStore
{
public:
    explicit CloudStateStore(QString dataDirectory);
    ~CloudStateStore();

    CloudStateStore(const CloudStateStore&) = delete;
    CloudStateStore& operator=(const CloudStateStore&) = delete;

    QString directory() const { return directory_; }
    QString statePath() const;
    QString pendingPath(const QString& kind) const;
    bool ensureDirectories(QString* error = nullptr) const;

    // A missing file yields defaults. A damaged file returns false and sets
    // corrupt; it is never overwritten by this call.
    bool load(Cloud::State& state, bool* corrupt, QString* error = nullptr);
    bool save(const Cloud::State& state, QString* error = nullptr);

    bool writePending(const QString& kind, const QByteArray& contents, QString* error = nullptr);
    bool readPending(const QString& kind, QByteArray& contents, QString* error = nullptr) const;
    void removePending(const QString& kind);

    // Prevents two application instances from applying cloud content at once.
    bool acquireLock(QString* error = nullptr);
    void releaseLock();
    bool locked() const;

private:
    QString directory_;
    QLockFile* lock_ = nullptr;
};
