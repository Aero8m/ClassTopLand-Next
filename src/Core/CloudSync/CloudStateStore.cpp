#include "CloudStateStore.h"

#include "CloudEndpoint.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QSaveFile>

namespace {
constexpr int kSchemaVersion = 1;

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

bool knownKind(const QString& kind)
{
    return kind == Cloud::profileKey() || kind == Cloud::displayKey();
}

QJsonObject channelToJson(const Cloud::ChannelState& channel)
{
    return {{QStringLiteral("applied_generation"), channel.appliedGeneration},
            {QStringLiteral("applied_version_id"), channel.appliedVersionId},
            {QStringLiteral("last_generation"), channel.lastGeneration},
            {QStringLiteral("last_result"), channel.lastResult},
            {QStringLiteral("last_reason"), channel.lastReason},
            {QStringLiteral("last_at"), channel.lastAt.isValid()
                                           ? channel.lastAt.toString(Qt::ISODate)
                                           : QString()}};
}

QJsonObject journalToJson(const Cloud::Journal& journal)
{
    return {{QStringLiteral("kind"), journal.kind},
            {QStringLiteral("assignment_id"), journal.assignmentId},
            {QStringLiteral("generation"), journal.generation},
            {QStringLiteral("version_id"), journal.versionId},
            {QStringLiteral("sha256"), journal.sha256},
            {QStringLiteral("format_version"), journal.formatVersion},
            {QStringLiteral("attempt"), journal.attempt},
            {QStringLiteral("stage"), journal.stage},
            {QStringLiteral("content_file"), journal.contentFile}};
}

QJsonObject toJson(const Cloud::State& state)
{
    QJsonObject channels;
    for (auto it = state.channels.cbegin(); it != state.channels.cend(); ++it)
        if (knownKind(it.key())) channels.insert(it.key(), channelToJson(it.value()));
    QJsonObject attempts;
    for (auto it = state.attempts.cbegin(); it != state.attempts.cend(); ++it)
        attempts.insert(it.key(), it.value());
    QJsonArray receipts;
    for (const auto& receipt : state.pendingReceipts) receipts.append(receipt);
    QJsonObject journal;
    for (auto it = state.journal.cbegin(); it != state.journal.cend(); ++it)
        if (knownKind(it.key()) && it->active) journal.insert(it.key(), journalToJson(it.value()));
    return {{QStringLiteral("schema_version"), kSchemaVersion},
            {QStringLiteral("origin"), state.origin},
            {QStringLiteral("enabled"), state.enabled},
            {QStringLiteral("device_secret"), state.deviceSecret},
            {QStringLiteral("claim_id"), state.claimId},
            {QStringLiteral("pair_code_hash"), state.pairCodeHash},
            {QStringLiteral("device_id"), state.deviceId},
            {QStringLiteral("device_name"), state.deviceName},
            {QStringLiteral("cloud_profile_id"), state.cloudProfileId},
            {QStringLiteral("channels"), channels},
            {QStringLiteral("attempts"), attempts},
            {QStringLiteral("pending_receipts"), receipts},
            {QStringLiteral("journal"), journal},
            {QStringLiteral("last_sync_at"), state.lastSyncAt.isValid()
                                                 ? state.lastSyncAt.toString(Qt::ISODate)
                                                 : QString()},
            {QStringLiteral("last_error"), state.lastError}};
}

bool readString(const QJsonObject& object, const QString& key, QString& value, QString* error)
{
    const QJsonValue item = object.value(key);
    if (item.isUndefined()) return true;
    if (!item.isString()) return fail(error, QStringLiteral("cloud state: %1 must be a string").arg(key));
    value = item.toString();
    return true;
}

bool readInt(const QJsonObject& object, const QString& key, int& value, QString* error)
{
    const QJsonValue item = object.value(key);
    if (item.isUndefined()) return true;
    if (!item.isDouble() || item.toDouble() != item.toInt())
        return fail(error, QStringLiteral("cloud state: %1 must be an integer").arg(key));
    value = item.toInt();
    return true;
}

bool channelFromJson(const QJsonObject& object, Cloud::ChannelState& channel, QString* error)
{
    const QJsonValue applied = object.value(QStringLiteral("applied_generation"));
    if (!applied.isUndefined()) {
        if (!applied.isDouble() || applied.toDouble() != applied.toInt())
            return fail(error, QStringLiteral("cloud state: applied_generation must be an integer"));
        channel.appliedGeneration = applied.toInt();
    }
    if (!readString(object, QStringLiteral("applied_version_id"), channel.appliedVersionId, error))
        return false;
    if (!readInt(object, QStringLiteral("last_generation"), channel.lastGeneration, error)) return false;
    if (!readString(object, QStringLiteral("last_result"), channel.lastResult, error)) return false;
    if (!readString(object, QStringLiteral("last_reason"), channel.lastReason, error)) return false;
    const QString at = object.value(QStringLiteral("last_at")).toString();
    if (!at.isEmpty()) channel.lastAt = QDateTime::fromString(at, Qt::ISODate);
    return true;
}

bool journalFromJson(const QJsonObject& object, Cloud::Journal& journal, QString* error)
{
    journal.active = true;
    if (!readString(object, QStringLiteral("kind"), journal.kind, error)) return false;
    if (!readString(object, QStringLiteral("assignment_id"), journal.assignmentId, error)) return false;
    if (!readInt(object, QStringLiteral("generation"), journal.generation, error)) return false;
    if (!readString(object, QStringLiteral("version_id"), journal.versionId, error)) return false;
    if (!readString(object, QStringLiteral("sha256"), journal.sha256, error)) return false;
    if (!readInt(object, QStringLiteral("format_version"), journal.formatVersion, error)) return false;
    if (!readInt(object, QStringLiteral("attempt"), journal.attempt, error)) return false;
    if (!readString(object, QStringLiteral("stage"), journal.stage, error)) return false;
    if (!readString(object, QStringLiteral("content_file"), journal.contentFile, error)) return false;
    if (journal.assignmentId.isEmpty() || journal.attempt <= 0 ||
        (journal.stage != QStringLiteral("attempting") && journal.stage != QStringLiteral("downloaded")))
        return fail(error, QStringLiteral("cloud state: invalid apply journal"));
    return true;
}

bool fromJson(const QJsonObject& root, Cloud::State& state, QString* error)
{
    const QJsonValue version = root.value(QStringLiteral("schema_version"));
    if (!version.isDouble() || version.toInt() != kSchemaVersion)
        return fail(error, QStringLiteral("Unsupported cloud state version"));
    Cloud::State loaded;
    if (!readString(root, QStringLiteral("origin"), loaded.origin, error)) return false;
    if (!readString(root, QStringLiteral("device_secret"), loaded.deviceSecret, error)) return false;
    if (!readString(root, QStringLiteral("claim_id"), loaded.claimId, error)) return false;
    if (!readString(root, QStringLiteral("pair_code_hash"), loaded.pairCodeHash, error)) return false;
    if (!readString(root, QStringLiteral("device_id"), loaded.deviceId, error)) return false;
    if (!readString(root, QStringLiteral("device_name"), loaded.deviceName, error)) return false;
    if (!readString(root, QStringLiteral("cloud_profile_id"), loaded.cloudProfileId, error)) return false;
    if (!readString(root, QStringLiteral("last_error"), loaded.lastError, error)) return false;
    const QJsonValue enabled = root.value(QStringLiteral("enabled"));
    if (!enabled.isUndefined()) {
        if (!enabled.isBool()) return fail(error, QStringLiteral("cloud state: enabled must be a boolean"));
        loaded.enabled = enabled.toBool();
    }
    const QJsonValue channels = root.value(QStringLiteral("channels"));
    if (!channels.isUndefined()) {
        if (!channels.isObject()) return fail(error, QStringLiteral("cloud state: channels must be an object"));
        const QJsonObject channelObject = channels.toObject();
        for (auto it = channelObject.constBegin(); it != channelObject.constEnd(); ++it) {
            if (!knownKind(it.key()) || !it.value().isObject()) continue;
            Cloud::ChannelState channel;
            if (!channelFromJson(it.value().toObject(), channel, error)) return false;
            loaded.channels.insert(it.key(), channel);
        }
    }
    const QJsonValue attempts = root.value(QStringLiteral("attempts"));
    if (!attempts.isUndefined()) {
        if (!attempts.isObject()) return fail(error, QStringLiteral("cloud state: attempts must be an object"));
        const QJsonObject attemptObject = attempts.toObject();
        for (auto it = attemptObject.constBegin(); it != attemptObject.constEnd(); ++it) {
            if (!it.value().isDouble() || it.value().toDouble() != it.value().toInt()) continue;
            loaded.attempts.insert(it.key(), it.value().toInt());
        }
    }
    const QJsonValue receipts = root.value(QStringLiteral("pending_receipts"));
    if (!receipts.isUndefined()) {
        if (!receipts.isArray())
            return fail(error, QStringLiteral("cloud state: pending_receipts must be an array"));
        for (const auto& entry : receipts.toArray()) {
            if (!entry.isObject())
                return fail(error, QStringLiteral("cloud state: pending receipt must be an object"));
            loaded.pendingReceipts.append(entry.toObject());
        }
    }
    const QJsonValue journal = root.value(QStringLiteral("journal"));
    if (!journal.isUndefined()) {
        if (!journal.isObject()) return fail(error, QStringLiteral("cloud state: journal must be an object"));
        const QJsonObject journalObject = journal.toObject();
        for (auto it = journalObject.constBegin(); it != journalObject.constEnd(); ++it) {
            if (!knownKind(it.key())) continue;
            if (!it.value().isObject())
                return fail(error, QStringLiteral("cloud state: journal entry must be an object"));
            Cloud::Journal entry;
            if (!journalFromJson(it.value().toObject(), entry, error)) return false;
            if (entry.kind != it.key())
                return fail(error, QStringLiteral("cloud state: journal kind mismatch"));
            loaded.journal.insert(it.key(), entry);
        }
    }
    const QString lastSync = root.value(QStringLiteral("last_sync_at")).toString();
    if (!lastSync.isEmpty()) loaded.lastSyncAt = QDateTime::fromString(lastSync, Qt::ISODate);
    if (loaded.origin.trimmed().isEmpty()) loaded.origin = QString::fromLatin1(Cloud::kDefaultOrigin);
    loaded.ensureChannels();
    state = std::move(loaded);
    return true;
}
} // namespace

CloudStateStore::CloudStateStore(QString dataDirectory)
    : directory_(QDir(std::move(dataDirectory)).absoluteFilePath(QStringLiteral("cloud")))
{
}

CloudStateStore::~CloudStateStore()
{
    releaseLock();
}

QString CloudStateStore::statePath() const
{
    return QDir(directory_).filePath(QStringLiteral("state.json"));
}

QString CloudStateStore::pendingPath(const QString& kind) const
{
    if (!knownKind(kind)) return {};
    return QDir(directory_).filePath(QStringLiteral("pending/%1.json").arg(kind));
}

bool CloudStateStore::ensureDirectories(QString* error) const
{
    if (!QDir().mkpath(directory_) || !QDir().mkpath(QDir(directory_).filePath(QStringLiteral("pending"))))
        return fail(error, QStringLiteral("无法创建集控数据目录：%1").arg(directory_));
    return true;
}

bool CloudStateStore::load(Cloud::State& state, bool* corrupt, QString* error)
{
    if (error) error->clear();
    if (corrupt) *corrupt = false;
    const QString path = statePath();
    if (!QFileInfo::exists(path)) {
        state = Cloud::State{};
        state.ensureChannels();
        return true;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("无法读取集控状态文件：%1").arg(file.errorString()));
    const QByteArray contents = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return fail(error, QStringLiteral("无法读取集控状态文件：%1").arg(file.errorString()));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (corrupt) *corrupt = true;
        return fail(error, QStringLiteral("集控状态文件已损坏：%1").arg(parseError.errorString()));
    }
    Cloud::State loaded;
    if (!fromJson(document.object(), loaded, error)) {
        if (corrupt) *corrupt = true;
        return false;
    }
    state = std::move(loaded);
    return true;
}

bool CloudStateStore::save(const Cloud::State& state, QString* error)
{
    if (error) error->clear();
    if (!ensureDirectories(error)) return false;
    QSaveFile file(statePath());
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("无法写入集控状态文件：%1").arg(file.errorString()));
    const QByteArray contents = QJsonDocument(toJson(state)).toJson(QJsonDocument::Indented);
    if (file.write(contents) != contents.size())
        return fail(error, QStringLiteral("无法写入集控状态文件：%1").arg(file.errorString()));
    if (!file.commit())
        return fail(error, QStringLiteral("无法保存集控状态文件：%1").arg(file.errorString()));
#ifndef Q_OS_WIN
    QFile::setPermissions(statePath(), QFile::ReadOwner | QFile::WriteOwner);
#endif
    return true;
}

bool CloudStateStore::writePending(const QString& kind, const QByteArray& contents, QString* error)
{
    if (error) error->clear();
    const QString path = pendingPath(kind);
    if (path.isEmpty()) return fail(error, QStringLiteral("无效的通道"));
    if (!ensureDirectories(error)) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("无法写入待应用内容：%1").arg(file.errorString()));
    if (file.write(contents) != contents.size())
        return fail(error, QStringLiteral("无法写入待应用内容：%1").arg(file.errorString()));
    if (!file.commit())
        return fail(error, QStringLiteral("无法保存待应用内容：%1").arg(file.errorString()));
    return true;
}

bool CloudStateStore::readPending(const QString& kind, QByteArray& contents, QString* error) const
{
    if (error) error->clear();
    const QString path = pendingPath(kind);
    if (path.isEmpty()) return fail(error, QStringLiteral("无效的通道"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("待应用内容不存在：%1").arg(file.errorString()));
    contents = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return fail(error, QStringLiteral("无法读取待应用内容：%1").arg(file.errorString()));
    return true;
}

void CloudStateStore::removePending(const QString& kind)
{
    const QString path = pendingPath(kind);
    if (!path.isEmpty()) QFile::remove(path);
}

bool CloudStateStore::acquireLock(QString* error)
{
    if (lock_) return true;
    if (!ensureDirectories(error)) return false;
    auto* lock = new QLockFile(QDir(directory_).filePath(QStringLiteral("sync.lock")));
    if (!lock->tryLock(0)) {
        delete lock;
        return fail(error, QStringLiteral("另一个 ClassTopLand-Next 实例正在同步"));
    }
    lock_ = lock;
    return true;
}

void CloudStateStore::releaseLock()
{
    if (!lock_) return;
    lock_->unlock();
    delete lock_;
    lock_ = nullptr;
}

bool CloudStateStore::locked() const
{
    return lock_ != nullptr && lock_->isLocked();
}
