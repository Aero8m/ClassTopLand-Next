#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>

// Device-side state and wire types for the CTX centralized control service.
// Nothing in this header touches widgets so the sync engine can run headless.
namespace Cloud {

constexpr char kDefaultOrigin[] = "https://ctx.aero8m.cn";
constexpr int kProfileFormatVersion = 1;
constexpr int kDisplayFormatVersion = 1;

// Channel identifiers exactly as used by the HTTP contract.
inline const QString& profileKey()
{
    static const QString key = QStringLiteral("profile");
    return key;
}
inline const QString& displayKey()
{
    static const QString key = QStringLiteral("display_config");
    return key;
}
inline const QString& channelKeyForIndex(int index)
{
    return index == 0 ? profileKey() : displayKey();
}
inline bool isKnownChannel(const QString& key)
{
    return key == profileKey() || key == displayKey();
}

// Persisted per-channel progress. Generations are only advanced after a
// successful atomic save, never when a download merely succeeded.
struct ChannelState
{
    int appliedGeneration = 0;
    QString appliedVersionId;
    int lastGeneration = 0;
    QString lastResult;
    QString lastReason;
    QDateTime lastAt;
};

// Application journal. It survives a crash between "content saved" and
// "applied marker + receipt saved", which is what makes the apply idempotent.
struct Journal
{
    bool active = false;
    QString kind;
    QString assignmentId;
    int generation = 0;
    QString versionId;
    QString sha256;
    int formatVersion = 1;
    int attempt = 0;
    // "attempting": attempt persisted, no content yet.
    // "downloaded": raw content saved under cloud/pending/<kind>.json.
    QString stage;
    QString contentFile;
};

struct State
{
    QString origin = QString::fromLatin1(kDefaultOrigin);
    bool enabled = true;
    QString deviceSecret;
    QString claimId;
    QString pairCodeHash;
    QString deviceId;
    QString deviceName;
    QString cloudProfileId;
    QHash<QString, ChannelState> channels;
    QHash<QString, int> attempts;
    QList<QJsonObject> pendingReceipts;
    QHash<QString, Journal> journal;
    QDateTime lastSyncAt;
    QString lastError;

    void ensureChannels()
    {
        channels.insert(profileKey(), channels.value(profileKey()));
        channels.insert(displayKey(), channels.value(displayKey()));
    }
    ChannelState& channel(const QString& key)
    {
        return channels[key];
    }
    bool bound() const { return !deviceId.isEmpty(); }
};

// Normalized view of one channel from POST /device/sync.
struct Target
{
    bool present = false;
    QString kind;
    QString assignmentId;
    int generation = 0;
    int appliedGeneration = 0;
    QString appliedVersionId;
    int attempt = 0;
    QString status;
    QString reason;
    QString versionId;
    QString sha256;
    int formatVersion = 1;
    bool compatible = false;
    // Path exactly as returned by the server, resolved against the origin.
    QString downloadPath;
};

enum class Status
{
    Unbound,   // no device credential
    Pairing,
    Connected,
    Paused,    // credential valid, automatic sync disabled
    Syncing,
    Revoked,   // server rejected the credential (401)
    Corrupt,   // state file unreadable; never silently reset
    Locked,    // another instance owns the sync lock
    Error
};

struct ChannelSnapshot
{
    int appliedGeneration = 0;
    QString appliedVersionId;
    int lastGeneration = 0;
    QString result;
    QString reason;
    QDateTime at;
};

struct Snapshot
{
    Status status = Status::Unbound;
    QString origin;
    bool enabled = true;
    bool busy = false;
    QString deviceName;
    QString deviceId;
    bool hasCredential = false;
    QString cloudProfileId;
    QString cloudProfileName;
    bool cloudProfileExists = false;
    bool cloudProfileCurrent = false;
    QDateTime lastSyncAt;
    QString lastError;
    QHash<QString, ChannelSnapshot> channels;
};

} // namespace Cloud
