#pragma once

#include "CloudApi.h"
#include "CloudStateStore.h"
#include "CloudTypes.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <array>
#include <functional>

class QTimer;

// Device side of the CTX centralized control protocol.
//
// Responsibilities are intentionally UI-free: pairing, the 20-30s poll loop,
// independent channel application, atomic state journalling and receipts. The
// settings page and AppComposer observe it through signals and snapshot().
class CloudSyncService final : public QObject
{
    Q_OBJECT
public:
    explicit CloudSyncService(const QString& dataDirectory, QObject* parent = nullptr);
    ~CloudSyncService() override;

    // Used to report blocked/local_edit only when the cloud profile is the
    // profile currently being edited.
    void setProfileEditorDirty(std::function<bool()> dirty) { profileEditorDirty_ = std::move(dirty); }

    // Loads the private state, takes the single-instance lock and schedules the
    // first sync. Configuration errors never abort application startup.
    bool start(QString* error = nullptr);
    void stop();
    bool started() const { return started_; }

    Cloud::Snapshot snapshot() const;
    bool busy() const { return inFlight_ || pairing_; }

    void pair(const QString& code);
    void syncNow();
    void setEnabled(bool enabled);
    bool setOrigin(const QString& origin, QString* error = nullptr);
    void unbind();
    // Explicit recovery for a damaged state file; the old file is preserved.
    bool resetState(QString* error = nullptr);

signals:
    void stateChanged();
    void pairingFinished(bool ok, const QString& message);
    void syncFinished(bool ok, const QString& message);
    void displayConfigApplied();
    // Emitted when the managed cloud profile file changed without becoming the
    // current profile, so profile lists can be refreshed.
    void cloudProfileUpdated();
    void cloudProfileSwitchRequested(const QString& profileId);
    void notification(const QString& title, const QString& message);

private:
    using Step = std::function<void()>;

    void scheduleNext(int milliseconds);
    void tick();
    void beginCycle(bool manual);
    void finishCycle(quint64 token, const QString& error, int nextDelayMs);
    void setStatus(Cloud::Status status);
    void report(const QString& message, bool notify = false);
    void notifyOnce(const QString& key, const QString& title, const QString& message);

    void resumeJournals(quint64 token, Step done);
    void flushReceipts(quint64 token, Step done);
    void doSync(quint64 token);
    void processNextChannel(quint64 token, int index,
                            const std::array<Cloud::Target, 2>& targets, Step done);
    void processChannel(quint64 token, const Cloud::Target& target, Step done);
    void startApply(quint64 token, const Cloud::Target& target, int attempt, Step done);
    void downloadTarget(quint64 token, const Cloud::Target& target, int attempt, Step done);
    void applyContent(quint64 token, const Cloud::Target& target, int attempt,
                      const QByteArray& contents, Step done);
    void failChannel(quint64 token, const Cloud::Target& target, int attempt,
                     const QString& result, const QString& reason, Step done);
    void completeChannel(quint64 token, const Cloud::Target& target, int attempt,
                         const QString& result, const QString& reason, bool applied, Step done);

    bool persist(const Cloud::State& state, QString* error);
    void handleUnauthorized();
    QJsonObject appliedBody() const;
    QJsonObject syncBody() const;
    QJsonObject receiptBody(const Cloud::Target& target, int attempt, const QString& result,
                            const QString& reason) const;
    QByteArray bearer() const;
    bool isCurrentCloudProfile() const;
    bool ensureCloudProfile(QString* error);
    void refreshCloudProfileInfo();
    Cloud::Target parseTarget(const QJsonObject& channels, const QString& kind) const;
    int retryDelay(const CloudApi::Reply& reply, int fallback) const;

    CloudStateStore store_;
    CloudApi* api_;
    QTimer* timer_ = nullptr;
    Cloud::State state_;
    Cloud::Status status_ = Cloud::Status::Unbound;
    QString statusError_;
    std::function<bool()> profileEditorDirty_;
    bool started_ = false;
    bool inFlight_ = false;
    bool pairing_ = false;
    bool manual_ = false;
    bool lockHeld_ = false;
    quint64 cycleToken_ = 0;
    int backoffMs_ = 10000;
    int pendingSyncAfterMs_ = 25000;
    QHash<QString, QDateTime> retryAfter_;
    QHash<QString, QString> blockSignature_;
    QSet<QString> notified_;
    QString cloudProfileName_;
    bool cloudProfileExists_ = false;
};
