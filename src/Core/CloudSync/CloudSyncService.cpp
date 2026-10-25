#include "CloudSyncService.h"

#include "CloudDisplayConfig.h"
#include "CloudEndpoint.h"

#include "../../VERSION.h"
#include "../ConfigManager/ConfigManager.h"
#include "../Logger/Logger.h"
#include "../ProfileManager/ProfileExchange.h"
#include "../ProfileManager/ProfileManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <QStringList>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <array>
#include <utility>

namespace {
constexpr int kInitialDelayMs = 1500;
constexpr int kMinSyncDelayMs = 5000;
constexpr int kMaxSyncDelayMs = 300000;
constexpr int kMinBackoffMs = 10000;
constexpr int kMaxBackoffMs = 120000;
constexpr int kFailureThrottleSeconds = 300;
constexpr int kMaxNotifiedKeys = 200;

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

QString sha256Hex(const QByteArray& contents)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex());
}

QByteArray randomBytes(int count)
{
    QByteArray bytes(count, '\0');
    for (int offset = 0; offset < count; offset += 4) {
        const quint32 value = QRandomGenerator::system()->generate();
        for (int index = 0; index < 4 && offset + index < count; ++index)
            bytes[offset + index] = char((value >> (8 * index)) & 0xff);
    }
    return bytes;
}

QString newDeviceSecret()
{
    return QString::fromLatin1(
        randomBytes(32).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString newClaimId()
{
    return QString::fromLatin1(randomBytes(16).toHex());
}

QString clientVersion()
{
    QString version = QString::fromLatin1(APP_VERSION);
    if (version.isEmpty() || version.startsWith(QLatin1Char('|')) || version.contains(QLatin1Char('|')))
        version = QStringLiteral("0.0.0");
    return QStringLiteral("ClassTopLand-Next/") + version.left(60);
}

QJsonObject dataObject(const QByteArray& body)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return {};
    const QJsonValue data = document.object().value(QStringLiteral("data"));
    return data.isObject() ? data.toObject() : QJsonObject{};
}

QString replyError(const CloudApi::Reply& reply)
{
    if (!reply.message.isEmpty()) return reply.message;
    if (!reply.transportError.isEmpty()) return reply.transportError;
    return QStringLiteral("HTTP %1").arg(reply.status);
}
} // namespace

CloudSyncService::CloudSyncService(const QString& dataDirectory, QObject* parent)
    : QObject(parent), store_(dataDirectory), api_(new CloudApi(this))
{
    state_.ensureChannels();
    timer_ = new QTimer(this);
    timer_->setObjectName(QStringLiteral("cloudSyncTimer"));
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &CloudSyncService::tick);
}

CloudSyncService::~CloudSyncService()
{
    stop();
}

bool CloudSyncService::start(QString* error)
{
    if (error) error->clear();
    if (started_) return true;
    started_ = true;
    bool corrupt = false;
    QString loadError;
    if (!store_.load(state_, &corrupt, &loadError)) {
        statusError_ = loadError;
        setStatus(corrupt ? Cloud::Status::Corrupt : Cloud::Status::Error);
        report(loadError);
        if (error) *error = loadError;
        emit stateChanged();
        return false;
    }
    refreshCloudProfileInfo();
    QString lockError;
    if (!store_.acquireLock(&lockError)) {
        statusError_ = lockError;
        setStatus(Cloud::Status::Locked);
        report(lockError);
        if (error) *error = lockError;
        emit stateChanged();
        return true;
    }
    lockHeld_ = true;
    if (state_.bound()) {
        if (!state_.enabled) {
            setStatus(Cloud::Status::Paused);
        } else {
            setStatus(Cloud::Status::Connected);
            scheduleNext(kInitialDelayMs);
        }
    } else {
        setStatus(Cloud::Status::Unbound);
    }
    emit stateChanged();
    return true;
}

void CloudSyncService::stop()
{
    if (timer_) timer_->stop();
    if (api_) api_->abortAll();
    ++cycleToken_;
    inFlight_ = false;
    pairing_ = false;
    if (lockHeld_) {
        store_.releaseLock();
        lockHeld_ = false;
    }
    started_ = false;
}

Cloud::Snapshot CloudSyncService::snapshot() const
{
    Cloud::Snapshot snap;
    snap.status = status_;
    snap.origin = state_.origin;
    snap.enabled = state_.enabled;
    snap.busy = busy();
    snap.deviceName = state_.deviceName;
    snap.deviceId = state_.deviceId;
    snap.hasCredential = !state_.deviceSecret.isEmpty();
    snap.cloudProfileId = state_.cloudProfileId;
    snap.cloudProfileName = cloudProfileName_;
    snap.cloudProfileExists = cloudProfileExists_;
    snap.cloudProfileCurrent = isCurrentCloudProfile();
    snap.lastSyncAt = state_.lastSyncAt;
    snap.lastError = statusError_.isEmpty() ? state_.lastError : statusError_;
    for (auto it = state_.channels.cbegin(); it != state_.channels.cend(); ++it) {
        Cloud::ChannelSnapshot channel;
        channel.appliedGeneration = it->appliedGeneration;
        channel.appliedVersionId = it->appliedVersionId;
        channel.lastGeneration = it->lastGeneration;
        channel.result = it->lastResult;
        channel.reason = it->lastReason;
        channel.at = it->lastAt;
        snap.channels.insert(it.key(), channel);
    }
    return snap;
}

void CloudSyncService::setStatus(Cloud::Status status)
{
    if (status_ == status) return;
    status_ = status;
    if (status != Cloud::Status::Error && status != Cloud::Status::Revoked &&
        status != Cloud::Status::Corrupt && status != Cloud::Status::Locked)
        statusError_.clear();
    emit stateChanged();
}

void CloudSyncService::report(const QString& message, bool notify)
{
    Logger::instance().log(Logger::Level::Warning, QStringLiteral("集控：") + message);
    Logger::instance().flush();
    if (notify) emit notification(tr("集控"), message);
}

void CloudSyncService::notifyOnce(const QString& key, const QString& title, const QString& message)
{
    if (notified_.contains(key)) return;
    notified_.insert(key);
    if (notified_.size() > kMaxNotifiedKeys) notified_.clear();
    emit notification(title, message);
}

bool CloudSyncService::persist(const Cloud::State& state, QString* error)
{
    if (!store_.save(state, error)) return false;
    state_ = state;
    return true;
}

QByteArray CloudSyncService::bearer() const
{
    return state_.deviceSecret.toLatin1();
}

QJsonObject CloudSyncService::appliedBody() const
{
    QJsonObject applied;
    for (const auto& key : {Cloud::profileKey(), Cloud::displayKey()}) {
        const Cloud::ChannelState channel = state_.channels.value(key);
        if (channel.appliedGeneration <= 0) continue;
        QJsonObject item{{QStringLiteral("generation"), channel.appliedGeneration}};
        if (!channel.appliedVersionId.isEmpty())
            item.insert(QStringLiteral("version_id"), channel.appliedVersionId);
        applied.insert(key, item);
    }
    return applied;
}

QJsonObject CloudSyncService::syncBody() const
{
    QJsonArray profileFormats;
    profileFormats.append(Cloud::kProfileFormatVersion);
    QJsonArray displayFormats;
    displayFormats.append(Cloud::kDisplayFormatVersion);
    return {{QStringLiteral("client_version"), clientVersion()},
            {QStringLiteral("supported_formats"),
             QJsonObject{{Cloud::profileKey(), profileFormats},
                         {Cloud::displayKey(), displayFormats}}},
            {QStringLiteral("applied"), appliedBody()}};
}

QJsonObject CloudSyncService::receiptBody(const Cloud::Target& target, int attempt,
                                          const QString& result, const QString& reason) const
{
    return {{QStringLiteral("assignment_id"), target.assignmentId},
            {QStringLiteral("kind"), target.kind},
            {QStringLiteral("generation"), target.generation},
            {QStringLiteral("version_id"), target.versionId},
            {QStringLiteral("attempt"), attempt},
            {QStringLiteral("result"), result},
            {QStringLiteral("reason"), reason.left(1000)}};
}

bool CloudSyncService::isCurrentCloudProfile() const
{
    if (state_.cloudProfileId.isEmpty()) return false;
    const QString current = QFileInfo(ProfileManager::instance().filePath()).completeBaseName();
    return !current.isEmpty() && current.compare(state_.cloudProfileId, Qt::CaseInsensitive) == 0;
}

void CloudSyncService::refreshCloudProfileInfo()
{
    cloudProfileName_.clear();
    cloudProfileExists_ = false;
    if (state_.cloudProfileId.isEmpty()) return;
    Profile profile;
    QString error;
    if (ProfileManager::instance().readProfile(state_.cloudProfileId, profile, &error)) {
        cloudProfileName_ = profile.name;
        cloudProfileExists_ = true;
    }
}

bool CloudSyncService::ensureCloudProfile(QString* error)
{
    if (!state_.cloudProfileId.isEmpty()) {
        Profile existing;
        QString readError;
        if (ProfileManager::instance().readProfile(state_.cloudProfileId, existing, &readError)) {
            cloudProfileName_ = existing.name;
            cloudProfileExists_ = true;
            return true;
        }
    }
    QString createdId;
    QString createError;
    const QString name = tr("云端档案");
    // Keep the current schedule visible after the one-time switch; an empty
    // profile is the fallback when the current profile cannot be copied.
    bool created = ProfileManager::instance().createProfile(name, true, createdId, &createError);
    if (!created) created = ProfileManager::instance().createProfile(name, false, createdId, &createError);
    if (!created) {
        cloudProfileExists_ = false;
        return fail(error, createError);
    }
    Cloud::State next = state_;
    next.cloudProfileId = createdId;
    QString saveError;
    if (!persist(next, &saveError)) return fail(error, saveError);
    refreshCloudProfileInfo();
    return true;
}

Cloud::Target CloudSyncService::parseTarget(const QJsonObject& channels, const QString& kind) const
{
    Cloud::Target target;
    target.kind = kind;
    const QJsonValue value = channels.value(kind);
    if (!value.isObject()) return target;
    const QJsonObject object = value.toObject();
    target.present = true;
    target.assignmentId = object.value(QStringLiteral("assignment_id")).toString();
    target.generation = object.value(QStringLiteral("generation")).toInt();
    target.appliedGeneration = object.value(QStringLiteral("applied_generation")).toInt();
    target.appliedVersionId = object.value(QStringLiteral("applied_version_id")).toString();
    target.attempt = object.value(QStringLiteral("attempt")).toInt();
    target.status = object.value(QStringLiteral("status")).toString();
    target.reason = object.value(QStringLiteral("reason")).toString();
    target.versionId = object.value(QStringLiteral("version_id")).toString();
    target.sha256 = object.value(QStringLiteral("sha256")).toString();
    target.formatVersion = object.value(QStringLiteral("format_version")).toInt(1);
    target.compatible = object.value(QStringLiteral("compatible")).toBool();
    target.downloadPath = object.value(QStringLiteral("download_url")).toString();
    if (target.assignmentId.isEmpty() || target.versionId.isEmpty() || target.generation <= 0)
        target.present = false;
    return target;
}

int CloudSyncService::retryDelay(const CloudApi::Reply& reply, int fallback) const
{
    bool ok = false;
    const int seconds = reply.header(QStringLiteral("retry-after")).toInt(&ok);
    if (ok && seconds > 0) return qBound(1000, seconds * 1000, 3600000);
    if (reply.status == 429 || reply.status == 503) return qMax(fallback, 30000);
    return fallback;
}

void CloudSyncService::scheduleNext(int milliseconds)
{
    if (!started_ || !timer_) return;
    if (status_ == Cloud::Status::Revoked || status_ == Cloud::Status::Corrupt ||
        status_ == Cloud::Status::Locked)
        return;
    if (!state_.bound() || state_.deviceSecret.isEmpty()) return;
    if (!state_.enabled) return;
    timer_->start(qMax(0, milliseconds));
}

void CloudSyncService::tick()
{
    if (!started_ || !state_.enabled || !state_.bound()) return;
    if (status_ == Cloud::Status::Revoked || status_ == Cloud::Status::Corrupt ||
        status_ == Cloud::Status::Locked)
        return;
    beginCycle(false);
}

void CloudSyncService::syncNow()
{
    if (!started_) {
        emit syncFinished(false, tr("集控服务未启动"));
        return;
    }
    if (status_ == Cloud::Status::Corrupt || status_ == Cloud::Status::Locked) {
        emit syncFinished(false, statusError_.isEmpty() ? tr("集控状态不可用") : statusError_);
        return;
    }
    if (!state_.bound()) {
        emit syncFinished(false, tr("请先完成设备配对"));
        return;
    }
    if (inFlight_ || pairing_) {
        emit syncFinished(false, tr("正在同步，请稍后重试"));
        return;
    }
    timer_->stop();
    beginCycle(true);
}

void CloudSyncService::beginCycle(bool manual)
{
    if (!started_ || inFlight_ || pairing_) return;
    if (status_ == Cloud::Status::Corrupt || status_ == Cloud::Status::Locked) return;
    if (!state_.bound() || state_.deviceSecret.isEmpty()) return;
    if (!state_.enabled && !manual) return;
    inFlight_ = true;
    manual_ = manual;
    ++cycleToken_;
    const quint64 token = cycleToken_;
    setStatus(Cloud::Status::Syncing);
    if (state_.cloudProfileId.isEmpty()) {
        QString profileError;
        if (!ensureCloudProfile(&profileError))
            report(tr("云端档案创建失败：%1").arg(profileError));
    }
    resumeJournals(token, [this, token] {
        flushReceipts(token, [this, token] { doSync(token); });
    });
}

void CloudSyncService::finishCycle(quint64 token, const QString& error, int nextDelayMs)
{
    if (token != cycleToken_) return;
    inFlight_ = false;
    if (!error.isEmpty()) {
        state_.lastError = error;
        report(error);
        if (status_ != Cloud::Status::Revoked) setStatus(Cloud::Status::Error);
        backoffMs_ = qBound(kMinBackoffMs, backoffMs_ * 2, kMaxBackoffMs);
        scheduleNext(nextDelayMs > 0 ? nextDelayMs : backoffMs_);
        emit syncFinished(false, error);
        emit stateChanged();
        return;
    }
    backoffMs_ = kMinBackoffMs;
    state_.lastError.clear();
    if (state_.bound())
        setStatus(state_.enabled ? Cloud::Status::Connected : Cloud::Status::Paused);
    scheduleNext(nextDelayMs > 0 ? nextDelayMs : pendingSyncAfterMs_);
    emit stateChanged();
    emit syncFinished(true, tr("同步完成"));
}

void CloudSyncService::handleUnauthorized()
{
    if (api_) api_->abortAll();
    ++cycleToken_;
    inFlight_ = false;
    pairing_ = false;
    if (timer_) timer_->stop();
    const QString message = tr("设备凭证已撤销或无效，请重新配对");
    statusError_ = message;
    state_.lastError = message;
    QString error;
    store_.save(state_, &error);
    setStatus(Cloud::Status::Revoked);
    emit stateChanged();
    emit syncFinished(false, message);
}

void CloudSyncService::pair(const QString& code)
{
    if (!started_) {
        emit pairingFinished(false, tr("集控服务未启动"));
        return;
    }
    if (status_ == Cloud::Status::Corrupt) {
        emit pairingFinished(false, statusError_.isEmpty() ? tr("集控状态文件已损坏") : statusError_);
        return;
    }
    if (inFlight_ || pairing_) {
        emit pairingFinished(false, tr("正在同步，请稍后重试"));
        return;
    }
    QString normalized = code.trimmed();
    normalized.remove(QLatin1Char(' '));
    normalized = normalized.toUpper();
    if (normalized.size() != 12) {
        emit pairingFinished(false, tr("配对码应为 12 位字符"));
        return;
    }
    QString originError;
    if (Cloud::normalizeOrigin(state_.origin, &originError).isEmpty()) {
        emit pairingFinished(false, originError);
        return;
    }
    if (timer_) timer_->stop();
    if (api_) api_->abortAll();
    ++cycleToken_;
    pairing_ = true;
    setStatus(Cloud::Status::Pairing);
    const QString hash = sha256Hex(normalized.toUtf8());
    Cloud::State next = state_;
    if (next.pairCodeHash != hash) {
        if (!next.pairCodeHash.isEmpty() || next.bound() || !next.deviceSecret.isEmpty()) {
            // A new binding needs a new credential and an independent generation
            // space; the last applied content stays on disk.
            next.deviceSecret = newDeviceSecret();
            next.claimId = newClaimId();
            next.deviceId.clear();
            next.deviceName.clear();
            next.channels.clear();
            next.ensureChannels();
            next.attempts.clear();
            next.pendingReceipts.clear();
            next.journal.clear();
            store_.removePending(Cloud::profileKey());
            store_.removePending(Cloud::displayKey());
        }
        next.pairCodeHash = hash;
    }
    if (next.deviceSecret.isEmpty()) next.deviceSecret = newDeviceSecret();
    if (next.claimId.isEmpty()) next.claimId = newClaimId();
    QString saveError;
    if (!persist(next, &saveError)) {
        pairing_ = false;
        setStatus(Cloud::Status::Error);
        report(saveError);
        emit pairingFinished(false, saveError);
        return;
    }
    const QJsonObject body{
        {QStringLiteral("code"), normalized},
        {QStringLiteral("claim_id"), state_.claimId},
        {QStringLiteral("device_secret"), state_.deviceSecret},
        {QStringLiteral("client_version"), clientVersion()}};
    api_->postJson(Cloud::apiUrl(state_.origin, QStringLiteral("/device/pair")), {}, body,
                   [this](const CloudApi::Reply& reply) {
                       pairing_ = false;
                       if (!started_) return;
                       if (reply.status == 201) {
                           const QJsonObject data = dataObject(reply.body);
                           const QString deviceId = data.value(QStringLiteral("device_id")).toString();
                           const QString name = data.value(QStringLiteral("name")).toString();
                           if (deviceId.isEmpty()) {
                               const QString message = tr("服务器响应缺少设备 ID");
                               state_.lastError = message;
                               setStatus(Cloud::Status::Error);
                               emit pairingFinished(false, message);
                               return;
                           }
                           Cloud::State next = state_;
                           next.deviceId = deviceId;
                           next.deviceName = name;
                           next.lastError.clear();
                           QString error;
                           if (!persist(next, &error)) {
                               setStatus(Cloud::Status::Error);
                               emit pairingFinished(false, error);
                               return;
                           }
                           statusError_.clear();
                           QString profileError;
                           const bool profileReady = ensureCloudProfile(&profileError);
                           refreshCloudProfileInfo();
                           setStatus(state_.enabled ? Cloud::Status::Connected
                                                    : Cloud::Status::Paused);
                           emit pairingFinished(
                               true, tr("配对成功：%1").arg(name.isEmpty() ? deviceId : name));
                           if (!profileReady) {
                               report(tr("云端档案创建失败：%1").arg(profileError));
                           } else {
                               emit cloudProfileUpdated();
                           }
                           if (profileReady && !isCurrentCloudProfile())
                               emit cloudProfileSwitchRequested(state_.cloudProfileId);
                           scheduleNext(600);
                           emit stateChanged();
                           return;
                       }
                       QString message;
                       if (reply.status == 409)
                           message = tr("配对码已被使用，请重新生成配对码");
                       else if (reply.status == 422)
                           message = reply.message.isEmpty() ? tr("配对码无效或已过期") : reply.message;
                       else if (reply.status == 429)
                           message = tr("请求过于频繁，请稍后重试");
                       else if (!reply.transportError.isEmpty())
                           message = reply.transportError;
                       else if (!reply.message.isEmpty())
                           message = reply.message;
                       else
                           message = tr("配对失败：HTTP %1").arg(reply.status);
                       state_.lastError = message;
                       statusError_ = message;
                       setStatus(state_.bound() ? Cloud::Status::Error : Cloud::Status::Unbound);
                       report(message);
                       emit pairingFinished(false, message);
                       emit stateChanged();
                       if (state_.bound()) scheduleNext(kMinBackoffMs);
                   });
}

bool CloudSyncService::setOrigin(const QString& origin, QString* error)
{
    if (error) error->clear();
    if (inFlight_ || pairing_) return fail(error, tr("正在同步，请稍后重试"));
    if (state_.bound()) return fail(error, tr("已绑定设备，请先解除绑定再修改服务器地址"));
    const QString normalized = Cloud::normalizeOrigin(origin, error);
    if (normalized.isEmpty()) return false;
    Cloud::State next = state_;
    next.origin = normalized;
    if (!persist(next, error)) return false;
    emit stateChanged();
    return true;
}

void CloudSyncService::setEnabled(bool enabled)
{
    if (!started_ || state_.enabled == enabled) return;
    Cloud::State next = state_;
    next.enabled = enabled;
    QString error;
    if (!persist(next, &error)) {
        report(error);
        return;
    }
    if (!enabled) {
        if (timer_) timer_->stop();
        if (!inFlight_) setStatus(state_.bound() ? Cloud::Status::Paused : Cloud::Status::Unbound);
    } else if (state_.bound()) {
        setStatus(Cloud::Status::Connected);
        scheduleNext(0);
    }
    emit stateChanged();
}

void CloudSyncService::unbind()
{
    if (!started_) return;
    if (timer_) timer_->stop();
    if (api_) api_->abortAll();
    ++cycleToken_;
    inFlight_ = false;
    pairing_ = false;
    Cloud::State next = state_;
    next.deviceSecret.clear();
    next.claimId.clear();
    next.pairCodeHash.clear();
    next.deviceId.clear();
    next.deviceName.clear();
    next.channels.clear();
    next.ensureChannels();
    next.attempts.clear();
    next.pendingReceipts.clear();
    next.journal.clear();
    next.lastError.clear();
    store_.removePending(Cloud::profileKey());
    store_.removePending(Cloud::displayKey());
    QString error;
    if (!persist(next, &error)) {
        report(error);
    }
    statusError_.clear();
    blockSignature_.clear();
    retryAfter_.clear();
    setStatus(Cloud::Status::Unbound);
    emit stateChanged();
}

bool CloudSyncService::resetState(QString* error)
{
    if (error) error->clear();
    if (timer_) timer_->stop();
    if (api_) api_->abortAll();
    ++cycleToken_;
    inFlight_ = false;
    pairing_ = false;
    if (lockHeld_) {
        store_.releaseLock();
        lockHeld_ = false;
    }
    const QString path = store_.statePath();
    if (QFileInfo::exists(path)) {
        const QString backup = path + QStringLiteral(".corrupt-") +
                               QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        if (!QFile::rename(path, backup))
            return fail(error, tr("无法备份损坏的集控状态文件"));
    }
    store_.removePending(Cloud::profileKey());
    store_.removePending(Cloud::displayKey());
    started_ = false;
    QString startError;
    const bool started = start(&startError);
    if (!started && !startError.isEmpty()) {
        if (error) *error = startError;
        return false;
    }
    emit stateChanged();
    return true;
}

void CloudSyncService::resumeJournals(quint64 token, Step done)
{
    if (token != cycleToken_) return;
    QString kind;
    Cloud::Journal journal;
    for (const auto& key : {Cloud::profileKey(), Cloud::displayKey()}) {
        const Cloud::Journal candidate = state_.journal.value(key);
        if (candidate.active) {
            kind = key;
            journal = candidate;
            break;
        }
    }
    if (kind.isEmpty()) {
        done();
        return;
    }
    Cloud::Target target;
    target.present = true;
    target.kind = kind;
    target.assignmentId = journal.assignmentId;
    target.generation = journal.generation;
    target.versionId = journal.versionId;
    target.sha256 = journal.sha256;
    target.formatVersion = journal.formatVersion;
    target.compatible = true;
    target.downloadPath =
        QStringLiteral("/api/v1/versions/%1/content").arg(journal.versionId);
    startApply(token, target, journal.attempt, done);
}

void CloudSyncService::flushReceipts(quint64 token, Step done)
{
    if (token != cycleToken_) return;
    if (state_.pendingReceipts.isEmpty()) {
        done();
        return;
    }
    const QJsonObject receipt = state_.pendingReceipts.first();
    api_->postJson(Cloud::apiUrl(state_.origin, QStringLiteral("/device/receipts")), bearer(),
                   receipt,
                   [this, token, receipt, done](const CloudApi::Reply& reply) {
                       if (token != cycleToken_) return;
                       if (reply.status == 401) {
                           handleUnauthorized();
                           return;
                       }
                       const bool accepted = reply.transportError.isEmpty() &&
                           (reply.ok() || reply.status == 404 || reply.status == 409 ||
                            reply.status == 422);
                       if (!accepted) {
                           finishCycle(token, replyError(reply),
                                       retryDelay(reply, backoffMs_));
                           return;
                       }
                       if (reply.status == 404 || reply.status == 409 || reply.status == 422) {
                           report(tr("回执被服务器拒绝（HTTP %1）：%2")
                                      .arg(reply.status)
                                      .arg(replyError(reply)));
                       }
                       Cloud::State next = state_;
                       const QString assignment =
                           receipt.value(QStringLiteral("assignment_id")).toString();
                       const int attempt = receipt.value(QStringLiteral("attempt")).toInt();
                       for (qsizetype i = 0; i < next.pendingReceipts.size(); ++i) {
                           if (next.pendingReceipts.at(i).value(QStringLiteral("assignment_id")).toString() ==
                                   assignment &&
                               next.pendingReceipts.at(i).value(QStringLiteral("attempt")).toInt() ==
                                   attempt) {
                               next.pendingReceipts.removeAt(i);
                               break;
                           }
                       }
                       QString error;
                       if (!store_.save(next, &error)) {
                           finishCycle(token, error, backoffMs_);
                           return;
                       }
                       state_ = next;
                       emit stateChanged();
                       flushReceipts(token, done);
                   });
}

void CloudSyncService::doSync(quint64 token)
{
    if (token != cycleToken_) return;
    api_->postJson(Cloud::apiUrl(state_.origin, QStringLiteral("/device/sync")), bearer(),
                   syncBody(), [this, token](const CloudApi::Reply& reply) {
                       if (token != cycleToken_) return;
                       if (reply.status == 401) {
                           handleUnauthorized();
                           return;
                       }
                       if (!reply.transportError.isEmpty()) {
                           finishCycle(token, reply.transportError,
                                       retryDelay(reply, backoffMs_));
                           return;
                       }
                       if (!reply.ok()) {
                           finishCycle(token, replyError(reply), retryDelay(reply, backoffMs_));
                           return;
                       }
                       const QJsonObject data = dataObject(reply.body);
                       if (!data.contains(QStringLiteral("channels"))) {
                           finishCycle(token, tr("服务器同步响应格式无效"), 60000);
                           return;
                       }
                       pendingSyncAfterMs_ =
                           qBound(kMinSyncDelayMs,
                                  data.value(QStringLiteral("sync_after_seconds")).toInt(25),
                                  kMaxSyncDelayMs);
                       const QJsonObject channels =
                           data.value(QStringLiteral("channels")).toObject();
                       const std::array<Cloud::Target, 2> targets{
                           parseTarget(channels, Cloud::profileKey()),
                           parseTarget(channels, Cloud::displayKey())};
                       // Attempt counters are only needed for the assignments the
                       // server can still refer to; drop everything else so the
                       // private state file does not grow with release history.
                       QSet<QString> keep;
                       for (const auto& target : targets)
                           if (!target.assignmentId.isEmpty()) keep.insert(target.assignmentId);
                       for (const auto& receipt : state_.pendingReceipts)
                           keep.insert(receipt.value(QStringLiteral("assignment_id")).toString());
                       Cloud::State next = state_;
                       next.lastSyncAt = QDateTime::currentDateTime();
                       const auto attemptKeys = next.attempts.keys();
                       for (const auto& key : attemptKeys)
                           if (!keep.contains(key)) next.attempts.remove(key);
                       QString error;
                       if (!persist(next, &error)) {
                           finishCycle(token, error, backoffMs_);
                           return;
                       }
                       processNextChannel(token, 0, targets, [this, token] {
                           flushReceipts(token, [this, token] {
                               finishCycle(token, {}, pendingSyncAfterMs_);
                           });
                       });
                   });
}

void CloudSyncService::processNextChannel(quint64 token, int index,
                                          const std::array<Cloud::Target, 2>& targets, Step done)
{
    if (token != cycleToken_) return;
    if (index >= 2) {
        done();
        return;
    }
    processChannel(token, targets[index], [this, token, index, targets, done] {
        processNextChannel(token, index + 1, targets, done);
    });
}

void CloudSyncService::processChannel(quint64 token, const Cloud::Target& target, Step done)
{
    if (token != cycleToken_) return;
    const QString kind = target.kind;
    const int expectedFormat =
        kind == Cloud::profileKey() ? Cloud::kProfileFormatVersion : Cloud::kDisplayFormatVersion;
    if (!target.present || !target.compatible || target.formatVersion != expectedFormat) {
        blockSignature_.remove(kind);
        done();
        return;
    }
    for (const auto& receipt : state_.pendingReceipts) {
        if (receipt.value(QStringLiteral("assignment_id")).toString() == target.assignmentId) {
            done();
            return;
        }
    }
    if (target.generation <= state_.channels.value(kind).appliedGeneration) {
        blockSignature_.remove(kind);
        done();
        return;
    }
    QString blockedReason;
    if (kind == Cloud::profileKey()) {
        if (isCurrentCloudProfile() && profileEditorDirty_ && profileEditorDirty_())
            blockedReason = QStringLiteral("local_edit");
    } else if (CloudDisplay::hasPluginComponent(
                   ConfigManager::instance().config().courseBarConfig)) {
        blockedReason = QStringLiteral("plugin_layout_conflict");
    }
    if (!blockedReason.isEmpty()) {
        const QString signature = target.assignmentId + QLatin1Char('|') + blockedReason;
        if (blockSignature_.value(kind) == signature) {
            done();
            return;
        }
        const int attempt = state_.attempts.value(target.assignmentId) + 1;
        Cloud::State next = state_;
        next.attempts.insert(target.assignmentId, attempt);
        QString error;
        if (!persist(next, &error)) {
            finishCycle(token, error, backoffMs_);
            return;
        }
        blockSignature_.insert(kind, signature);
        notifyOnce(kind + QLatin1Char('|') + target.assignmentId + QLatin1Char('|') + blockedReason,
                   tr("集控同步被阻塞"),
                   kind == Cloud::profileKey()
                       ? tr("档案有未保存的更改，云端档案将在保存或放弃后更新")
                       : tr("课程条包含插件组件，云端展示配置将在移除后更新"));
        completeChannel(token, target, attempt, QStringLiteral("blocked"), blockedReason, false,
                        done);
        return;
    }
    blockSignature_.remove(kind);
    if (!manual_) {
        const QDateTime retryAt = retryAfter_.value(kind);
        if (retryAt.isValid() && QDateTime::currentDateTime() < retryAt) {
            done();
            return;
        }
    }
    const int attempt = state_.attempts.value(target.assignmentId) + 1;
    Cloud::State next = state_;
    next.attempts.insert(target.assignmentId, attempt);
    Cloud::Journal journal;
    journal.active = true;
    journal.kind = kind;
    journal.assignmentId = target.assignmentId;
    journal.generation = target.generation;
    journal.versionId = target.versionId;
    journal.sha256 = target.sha256;
    journal.formatVersion = target.formatVersion;
    journal.attempt = attempt;
    journal.stage = QStringLiteral("attempting");
    journal.contentFile = kind + QStringLiteral(".json");
    next.journal.insert(kind, journal);
    QString error;
    if (!persist(next, &error)) {
        finishCycle(token, error, backoffMs_);
        return;
    }
    startApply(token, target, attempt, done);
}

void CloudSyncService::startApply(quint64 token, const Cloud::Target& target, int attempt,
                                  Step done)
{
    if (token != cycleToken_) return;
    QByteArray pending;
    if (store_.readPending(target.kind, pending) && !pending.isEmpty() &&
        sha256Hex(pending) == target.sha256) {
        applyContent(token, target, attempt, pending, done);
        return;
    }
    downloadTarget(token, target, attempt, done);
}

void CloudSyncService::downloadTarget(quint64 token, const Cloud::Target& target, int attempt,
                                      Step done)
{
    if (token != cycleToken_) return;
    QUrl url;
    QString urlError;
    if (!Cloud::resolveApiPath(state_.origin, target.downloadPath, url, &urlError)) {
        failChannel(token, target, attempt, QStringLiteral("download_failed"), urlError, done);
        return;
    }
    api_->get(url, bearer(), [this, token, target, attempt, done](const CloudApi::Reply& reply) {
        if (token != cycleToken_) return;
        if (reply.status == 401) {
            handleUnauthorized();
            return;
        }
        if (!reply.transportError.isEmpty()) {
            failChannel(token, target, attempt, QStringLiteral("download_failed"),
                        reply.transportError, done);
            return;
        }
        if (!reply.ok()) {
            failChannel(token, target, attempt, QStringLiteral("download_failed"),
                        replyError(reply), done);
            return;
        }
        if (sha256Hex(reply.body) != target.sha256) {
            failChannel(token, target, attempt, QStringLiteral("checksum_mismatch"),
                        tr("内容摘要与服务器记录不一致"), done);
            return;
        }
        const QString headerDigest = reply.header(QStringLiteral("x-content-sha256"));
        if (!headerDigest.isEmpty() &&
            headerDigest.compare(target.sha256, Qt::CaseInsensitive) != 0) {
            failChannel(token, target, attempt, QStringLiteral("checksum_mismatch"),
                        tr("内容摘要响应头与服务器记录不一致"), done);
            return;
        }
        QString writeError;
        if (!store_.writePending(target.kind, reply.body, &writeError)) {
            failChannel(token, target, attempt, QStringLiteral("save_failed"), writeError, done);
            return;
        }
        Cloud::State next = state_;
        auto journal = next.journal.find(target.kind);
        if (journal != next.journal.end()) {
            journal->stage = QStringLiteral("downloaded");
            journal->contentFile = target.kind + QStringLiteral(".json");
        }
        QString error;
        if (!persist(next, &error)) {
            finishCycle(token, error, backoffMs_);
            return;
        }
        applyContent(token, target, attempt, reply.body, done);
    });
}

void CloudSyncService::applyContent(quint64 token, const Cloud::Target& target, int attempt,
                                    const QByteArray& contents, Step done)
{
    if (token != cycleToken_) return;
    if (target.kind == Cloud::profileKey()) {
        Profile profile;
        QStringList warnings;
        QString error;
        if (!ProfileExchange::decode(contents, ProfileExchange::Format::NativeJson, profile,
                                     &warnings, &error)) {
            failChannel(token, target, attempt, QStringLiteral("validation_failed"), error, done);
            return;
        }
        if (!ProfileExchange::validateTransfer(profile, &error)) {
            failChannel(token, target, attempt, QStringLiteral("validation_failed"), error, done);
            return;
        }
        if (state_.cloudProfileId.isEmpty() && !ensureCloudProfile(&error)) {
            failChannel(token, target, attempt, QStringLiteral("save_failed"), error, done);
            return;
        }
        QList<QDate> canceled;
        if (!ProfileManager::instance().commitManagedProfile(state_.cloudProfileId, profile, &error,
                                                             &canceled)) {
            failChannel(token, target, attempt, QStringLiteral("save_failed"), error, done);
            return;
        }
        if (!isCurrentCloudProfile()) {
            refreshCloudProfileInfo();
            emit cloudProfileUpdated();
            notifyOnce(QStringLiteral("profile-updated|") + target.assignmentId,
                       tr("云端档案已更新"),
                       tr("云端档案“%1”已更新，切换为该档案后生效")
                           .arg(cloudProfileName_.isEmpty() ? tr("云端档案") : cloudProfileName_));
        }
        completeChannel(token, target, attempt, QStringLiteral("applied"), {}, true, done);
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        failChannel(token, target, attempt, QStringLiteral("validation_failed"),
                    tr("展示配置不是有效的 JSON 对象"), done);
        return;
    }
    Config candidate;
    QString error;
    if (!CloudDisplay::buildConfig(document.object(), ConfigManager::instance().config(),
                                   candidate, &error)) {
        failChannel(token, target, attempt, QStringLiteral("validation_failed"), error, done);
        return;
    }
    if (!ConfigManager::validateCandidate(candidate, &error)) {
        failChannel(token, target, attempt, QStringLiteral("validation_failed"), error, done);
        return;
    }
    if (!ConfigManager::instance().commit(candidate, &error)) {
        failChannel(token, target, attempt, QStringLiteral("save_failed"), error, done);
        return;
    }
    emit displayConfigApplied();
    completeChannel(token, target, attempt, QStringLiteral("applied"), {}, true, done);
}

void CloudSyncService::failChannel(quint64 token, const Cloud::Target& target, int attempt,
                                   const QString& result, const QString& reason, Step done)
{
    if (token != cycleToken_) return;
    retryAfter_.insert(target.kind, QDateTime::currentDateTime().addSecs(kFailureThrottleSeconds));
    const QString label = target.kind == Cloud::profileKey() ? tr("课表档案") : tr("展示配置");
    notifyOnce(target.kind + QLatin1Char('|') + target.assignmentId + QLatin1Char('|') + result,
               tr("集控同步失败"), tr("%1：%2").arg(label, reason.left(200)));
    completeChannel(token, target, attempt, result, reason, false, done);
}

void CloudSyncService::completeChannel(quint64 token, const Cloud::Target& target, int attempt,
                                       const QString& result, const QString& reason, bool applied,
                                       Step done)
{
    if (token != cycleToken_) return;
    Cloud::State next = state_;
    next.journal.remove(target.kind);
    Cloud::ChannelState& channel = next.channel(target.kind);
    channel.lastGeneration = target.generation;
    channel.lastResult = result;
    channel.lastReason = reason;
    channel.lastAt = QDateTime::currentDateTime();
    if (applied) {
        channel.appliedGeneration = target.generation;
        channel.appliedVersionId = target.versionId;
    }
    next.pendingReceipts.append(receiptBody(target, attempt, result, reason));
    QString error;
    if (!store_.save(next, &error)) {
        // The content may already be on disk, but without the durable applied
        // marker nothing is published to readers; the journal is replayed later.
        finishCycle(token, error, backoffMs_);
        return;
    }
    state_ = next;
    if (applied) {
        store_.removePending(target.kind);
        retryAfter_.remove(target.kind);
        blockSignature_.remove(target.kind);
    }
    emit stateChanged();
    done();
}
