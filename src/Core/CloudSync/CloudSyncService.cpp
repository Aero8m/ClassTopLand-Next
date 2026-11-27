#include "CloudSyncService.h"

#include "CloudDisplayConfig.h"
#include "CloudEndpoint.h"
#include "CloudOperations.h"
#include "CloudPower.h"
#include "CloudScreen.h"

#include "../ConfigManager/ConfigManager.h"
#include "../Logger/Logger.h"
#include "../ProfileManager/ProfileExchange.h"
#include "../ProfileManager/ProfileManager.h"
#include "../Update/UpdateService.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <QRegularExpression>
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
    return QStringLiteral("ClassTopLand-Next/") + CloudOperations::buildId().left(60);
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

QString replyDiagnostic(const CloudApi::Reply& reply)
{
    if (!reply.transportDiagnostic.isEmpty()) return reply.transportDiagnostic;
    const QString status = QStringLiteral("HTTP %1").arg(reply.status);
    return reply.message.isEmpty() ? status : status + QStringLiteral(": ") + reply.message;
}

bool validTaskId(const QString& id)
{
    static const QRegularExpression pattern(QStringLiteral("\\A[a-f0-9]{32}\\z"));
    return pattern.match(id).hasMatch();
}
} // namespace

CloudSyncService::CloudSyncService(const QString& dataDirectory, QObject* parent)
    : QObject(parent), store_(dataDirectory), api_(new CloudApi(this)), screen_(std::make_unique<CloudScreen>()),
      updateInstanceId_(newClaimId())
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
        report(QStringLiteral("Could not load sync state"), loadError);
        if (error) *error = loadError;
        emit stateChanged();
        return false;
    }
    refreshCloudProfileInfo();
    QString lockError;
    if (!store_.acquireLock(&lockError)) {
        statusError_ = lockError;
        setStatus(Cloud::Status::Locked);
        report(QStringLiteral("Could not lock sync state"), lockError);
        if (error) *error = lockError;
        emit stateChanged();
        return true;
    }
    lockHeld_ = true;
    if (state_.bound()) {
        // Read the previous installer's outcome before staging directories are cleaned.
        recoverUpdate();
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
    screen_->stop();
    if (timer_) timer_->stop();
    if (api_) api_->abortAll();
    ++cycleToken_;
    inFlight_ = false;
    pairing_ = false;
    if (updater_ && state_.updateJournal.value(QStringLiteral("status")).toString() != QStringLiteral("installing"))
        updater_->finishManaged();
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
    snap.hasCredential = !state_.deviceSecret.isEmpty() || !state_.pendingPairSecret.isEmpty();
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

void CloudSyncService::report(const QString& message, const QString& detail)
{
    QString line = QStringLiteral("Cloud sync: ") + message;
    if (!detail.isEmpty() && detail != message)
        line += QStringLiteral(": ") + detail;
    Logger::instance().log(Logger::Level::Warning, line);
    Logger::instance().flush();
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
    profileFormats.append(1);
    profileFormats.append(Cloud::kProfileFormatVersion);
    QJsonArray displayFormats;
    displayFormats.append(1);
    displayFormats.append(Cloud::kDisplayFormatVersion);
    if (updateSupported()) displayFormats.append(Cloud::kDisplayUpdateFormatVersion);
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
            report(QStringLiteral("Could not create the cloud profile"), profileError);
    }
    if (!recoverUpdate()) {
        finishCycle(token, tr("无法保存远程更新恢复状态"), backoffMs_);
        return;
    }
    QString recoveryError;
    if (!recoverOperation(&recoveryError)) {
        finishCycle(token, recoveryError, backoffMs_,
                    QStringLiteral("Could not recover an interrupted device operation: %1").arg(recoveryError));
        return;
    }
    resumeJournals(token, [this, token] {
        flushReceipts(token, [this, token] { doSync(token); });
    });
}

void CloudSyncService::finishCycle(quint64 token, const QString& error, int nextDelayMs, const QString& diagnostic)
{
    if (token != cycleToken_) return;
    inFlight_ = false;
    if (!error.isEmpty()) {
        state_.lastError = error;
        if (diagnostic.isEmpty())
            report(QStringLiteral("Sync cycle failed"), error);
        else
            report(diagnostic);
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
    if (updater_) updater_->finishManaged();
    screen_->stop();
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
    const bool wasRevoked = status_ == Cloud::Status::Revoked;
    if (updater_ && updater_->managedUpdate())
        finishUpdate(QStringLiteral("failed"), tr("设备开始重新配对，远程更新已停止。"));
    pairing_ = true;
    setStatus(Cloud::Status::Pairing);
    const QString hash = sha256Hex(normalized.toUtf8());
    Cloud::State next = state_;
    if (next.pairCodeHash != hash || next.claimId.isEmpty()) {
        next.pairCodeHash = hash;
        next.pendingPairSecret = newDeviceSecret();
        next.claimId = newClaimId();
    } else if (next.pendingPairSecret.isEmpty()) {
        // Resume a claim persisted by an older version before switching credentials.
        next.pendingPairSecret = next.deviceSecret.isEmpty() ? newDeviceSecret() : next.deviceSecret;
    }
    QString saveError;
    if (!persist(next, &saveError)) {
        pairing_ = false;
        setStatus(wasRevoked ? Cloud::Status::Revoked : Cloud::Status::Error);
        report(QStringLiteral("Could not save the pending pairing"), saveError);
        emit pairingFinished(false, saveError);
        scheduleNext(kMinBackoffMs);
        return;
    }
    const QJsonObject body{
        {QStringLiteral("code"), normalized},
        {QStringLiteral("claim_id"), state_.claimId},
        {QStringLiteral("device_secret"), state_.pendingPairSecret},
        {QStringLiteral("client_version"), clientVersion()}};
    api_->postJson(Cloud::apiUrl(state_.origin, QStringLiteral("/device/pair")), {}, body,
                   [this, wasRevoked](const CloudApi::Reply& reply) {
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
                           const bool sameDevice = next.deviceId == deviceId;
                           if (!sameDevice) {
                               next.channels.clear();
                               next.ensureChannels();
                               next.attempts.clear();
                               next.pendingReceipts.clear();
                               next.journal.clear();
                               next.operationJournal = {};
                               next.lastSyncAt = {};
                           }
                           if (!sameDevice || next.deviceSecret != next.pendingPairSecret)
                               next.updateJournal = {};
                           next.deviceSecret = next.pendingPairSecret;
                           next.pendingPairSecret.clear();
                           next.claimId.clear();
                           next.pairCodeHash.clear();
                           next.deviceId = deviceId;
                           next.deviceName = name;
                           next.lastError.clear();
                           QString error;
                           if (!persist(next, &error)) {
                               setStatus(Cloud::Status::Error);
                               emit pairingFinished(false, error);
                               return;
                           }
                           screen_->stop();
                           if (!sameDevice) {
                               store_.removePending(Cloud::profileKey());
                               store_.removePending(Cloud::displayKey());
                               retryAfter_.clear();
                               blockSignature_.clear();
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
                               report(QStringLiteral("Could not create the cloud profile"), profileError);
                           } else {
                               emit cloudProfileUpdated();
                           }
                           if (!sameDevice && profileReady && !isCurrentCloudProfile())
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
                       setStatus(wasRevoked ? Cloud::Status::Revoked :
                                 state_.bound() ? Cloud::Status::Error : Cloud::Status::Unbound);
                       report(QStringLiteral("Pairing failed"), replyDiagnostic(reply));
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
    if (next.origin != normalized) {
        next.deviceSecret.clear();
        next.pendingPairSecret.clear();
        next.claimId.clear();
        next.pairCodeHash.clear();
    }
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
        report(QStringLiteral("Could not save the sync preference"), error);
        return;
    }
    if (!enabled) {
        if (updater_ && updater_->managedUpdate())
            finishUpdate(QStringLiteral("failed"), tr("集控同步已暂停，远程更新已停止。"));
        screen_->stop();
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
    screen_->stop();
    if (!started_) return;
    if (updater_) updater_->finishManaged();
    if (timer_) timer_->stop();
    if (api_) api_->abortAll();
    ++cycleToken_;
    inFlight_ = false;
    pairing_ = false;
    Cloud::State next = state_;
    next.deviceSecret.clear();
    next.pendingPairSecret.clear();
    next.claimId.clear();
    next.pairCodeHash.clear();
    next.deviceId.clear();
    next.deviceName.clear();
    next.channels.clear();
    next.ensureChannels();
    next.attempts.clear();
    next.pendingReceipts.clear();
    next.journal.clear();
    next.operationJournal = {};
    next.updateJournal = {};
    next.lastError.clear();
    store_.removePending(Cloud::profileKey());
    store_.removePending(Cloud::displayKey());
    QString error;
    if (!persist(next, &error)) {
        report(QStringLiteral("Could not save the unbound state"), error);
    }
    statusError_.clear();
    blockSignature_.clear();
    retryAfter_.clear();
    setStatus(Cloud::Status::Unbound);
    emit stateChanged();
}

bool CloudSyncService::resetState(QString* error)
{
    screen_->stop();
    if (error) error->clear();
    if (updater_) updater_->finishManaged();
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
                           finishCycle(token, replyError(reply), retryDelay(reply, backoffMs_),
                                       QStringLiteral("Could not send sync receipt: %1").arg(replyDiagnostic(reply)));
                           return;
                       }
                       if (reply.status == 404 || reply.status == 409 || reply.status == 422) {
                           report(QStringLiteral("Server rejected the sync receipt"), replyDiagnostic(reply));
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
                           finishCycle(token, reply.transportError, retryDelay(reply, backoffMs_),
                                       replyDiagnostic(reply));
                           return;
                       }
                       if (!reply.ok()) {
                           finishCycle(token, replyError(reply), retryDelay(reply, backoffMs_), replyDiagnostic(reply));
                           return;
                       }
                       const QJsonObject data = dataObject(reply.body);
                       if (!data.contains(QStringLiteral("channels"))) {
                           finishCycle(token, tr("服务器同步响应格式无效"), 60000,
                                       QStringLiteral("Invalid sync response from the server"));
                           return;
                       }
                       qtPowerEnabled_ = data.value(QStringLiteral("qt_power")).toBool();
                       qtScreenSnapshotsEnabled_ = data.value(QStringLiteral("qt_screen_snapshots")).toBool();
                       qtUpdateEnabled_ = data.value(QStringLiteral("qt_update")).toBool();
                       if (!qtUpdateEnabled_ && updater_ && updater_->managedUpdate())
                           finishUpdate(QStringLiteral("failed"), tr("服务器未声明支持远程更新。"));
                       if (qtPowerEnabled_ && CloudPower::supported() && !bootProbeStarted_) {
                           bootProbeStarted_ = true;
                           CloudPower::readBootId(this, [this](const QString& id) { powerBootId_ = id; });
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
                               pollOperation(token);
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

void CloudSyncService::pollOperation(quint64 token)
{
    if (token != cycleToken_) return;
    const auto journal = state_.operationJournal;
    const QString id = journal.value(QStringLiteral("id")).toString();
    const QString status = journal.value(QStringLiteral("status")).toString();
    if (!id.isEmpty() && !status.isEmpty()) {
        if (status == QStringLiteral("accepted") && !qtPowerEnabled_) {
            QString error;
            auto next = state_;
            next.operationJournal = {};
            if (!persist(next, &error)) {
                finishCycle(token, error, backoffMs_);
                return;
            }
            report(QStringLiteral("The server does not support power operation receipts"));
        } else if (status == QStringLiteral("restart_accepted")) {
            const auto expected = journal.value(QStringLiteral("activate_cloud")).toBool();
            if (managedRestartAccepted_) {
                finishCycle(token, {}, pendingSyncAfterMs_);
                return;
            }
            const bool applied = !expected || isCurrentCloudProfile();
            sendOperationResult(token, id, applied ? QStringLiteral("applied") : QStringLiteral("failed"),
                                applied ? QString() : tr("重启后未启用云端档案"));
            return;
        } else {
            sendOperationResult(token, id, status, journal.value(QStringLiteral("reason")).toString(),
                                journal.value(QStringLiteral("boot_id")).toString());
            return;
        }
    }
    auto telemetry = CloudOperations::telemetry(isCurrentCloudProfile());
    if (qtScreenSnapshotsEnabled_ && CloudScreen::supported())
        telemetry.insert(QStringLiteral("screen_snapshots"), true);
    if (qtPowerEnabled_ && CloudPower::supported()) {
        telemetry.insert(QStringLiteral("power_control"), true);
        if (!powerBootId_.isEmpty()) telemetry.insert(QStringLiteral("boot_id"), powerBootId_);
    }
    if (pluginTelemetry_) telemetry.insert(QStringLiteral("plugins"), pluginTelemetry_());
    if (qtUpdateEnabled_ && updateSupported()) {
        telemetry.insert(QStringLiteral("software_update"), QJsonObject{
            {QStringLiteral("build_number"), UpdateService::currentBuild()},
            {QStringLiteral("automatic_updates"), updater_->automaticUpdates()},
            {QStringLiteral("instance_id"), updateInstanceId_}});
    }
    api_->postJson(Cloud::apiUrl(state_.origin, QStringLiteral("/device/operations/poll")), bearer(), telemetry,
                   [this, token](const CloudApi::Reply& reply) {
                       if (token != cycleToken_) return;
                       if (reply.status == 401) {
                           handleUnauthorized();
                           return;
                       }
                       if (!reply.ok()) {
                           if (reply.status != 404)
                               report(QStringLiteral("Could not send device status"), replyDiagnostic(reply));
                           finishCycle(token, {}, pendingSyncAfterMs_);
                           return;
                       }
                       const auto data = dataObject(reply.body);
                       if (state_.enabled && data.value(QStringLiteral("screen_enabled")).toBool())
                           screen_->configure(state_.origin, bearer());
                       else
                           screen_->stop();
                       if (qtUpdateEnabled_ && flushUpdateReceipt(token)) return;
                       const auto task = data.value(QStringLiteral("task")).toObject();
                       if (task.isEmpty()) {
                           finishCycle(token, {}, pendingSyncAfterMs_);
                           return;
                       }
                       executeOperation(token, task);
                   });
}

void CloudSyncService::setUpdateService(UpdateService* updater, std::function<QString()> guard)
{
    updater_ = updater;
    updateGuard_ = std::move(guard);
    connect(updater, &UpdateService::changed, this, &CloudSyncService::updateChanged);
}

bool CloudSyncService::updateSupported() const
{
    return updater_ && updater_->supported();
}

bool CloudSyncService::saveUpdateJournal(const QJsonObject& journal, QString* error)
{
    auto next = state_;
    next.updateJournal = journal;
    QString saveError;
    if (persist(next, &saveError)) return true;
    report(QStringLiteral("Could not save remote update state"), saveError);
    return fail(error, saveError);
}

bool CloudSyncService::finishUpdate(const QString& status, const QString& reason, int targetBuild)
{
    QJsonObject journal{{QStringLiteral("id"), state_.updateJournal.value(QStringLiteral("id"))},
                        {QStringLiteral("status"), status},
                        {QStringLiteral("reason"), reason.left(1000)}};
    if (targetBuild > 0) journal.insert(QStringLiteral("target_build"), targetBuild);
    const bool saved = saveUpdateJournal(journal);
    if (updater_) updater_->finishManaged();
    if (!reason.isEmpty()) report(QStringLiteral("Remote update: %1").arg(status), reason);
    if (!inFlight_) scheduleNext(0);
    return saved;
}

bool CloudSyncService::recoverUpdate()
{
    const auto& journal = state_.updateJournal;
    if (journal.isEmpty()) return true;
    const QString phase = journal.value(QStringLiteral("status")).toString();
    if (!validTaskId(journal.value(QStringLiteral("id")).toString())) return saveUpdateJournal({});
    if (phase == QStringLiteral("failed") || phase == QStringLiteral("up_to_date") ||
        phase == QStringLiteral("applied")) return true;
    if (journal.value(QStringLiteral("instance_id")).toString() == updateInstanceId_
        && updater_ && updater_->managedUpdate()) return true;
    if (phase == QStringLiteral("installing")) {
        const int target = journal.value(QStringLiteral("target_build")).toInt();
        if (updateSupported() && target > 0 && UpdateService::currentBuild() >= target)
            return finishUpdate(QStringLiteral("applied"));
        return finishUpdate(QStringLiteral("failed"),
                            UpdateService::installationError(journal.value(QStringLiteral("stage")).toString()));
    }
    return finishUpdate(QStringLiteral("failed"), tr("客户端在更新完成前退出，远程更新已停止。"));
}

void CloudSyncService::executeUpdate(quint64 token, const QString& id)
{
    if (!qtUpdateEnabled_ || !state_.updateJournal.isEmpty()) {
        finishCycle(token, {}, pendingSyncAfterMs_);
        return;
    }
    QJsonObject journal{{QStringLiteral("id"), id},
                        {QStringLiteral("status"), QStringLiteral("checking")},
                        {QStringLiteral("instance_id"), updateInstanceId_}};
    QString error;
    if (!saveUpdateJournal(journal, &error)) {
        finishCycle(token, error, backoffMs_);
        return;
    }
    if (!state_.enabled)
        finishUpdate(QStringLiteral("failed"), tr("集控同步已暂停。"));
    else if (!updateSupported())
        finishUpdate(QStringLiteral("failed"), tr("当前客户端不支持远程更新。"));
    else if (const QString blocked = updateGuard_ ? updateGuard_() : tr("更新退出检查不可用。"); !blocked.isEmpty())
        finishUpdate(QStringLiteral("failed"), blocked);
    else if (!updater_->checkManaged())
        finishUpdate(QStringLiteral("failed"), tr("客户端已有更新操作正在进行或等待安装。"));
    finishCycle(token, {}, kMinSyncDelayMs);
}

void CloudSyncService::updateChanged()
{
    if (!updater_ || !updater_->managedUpdate()) return;
    using State = UpdateService::State;
    const auto state = updater_->state();
    auto journal = state_.updateJournal;
    const QString phase = journal.value(QStringLiteral("status")).toString();
    if (phase == QStringLiteral("checking") && state == State::Current) {
        finishUpdate(QStringLiteral("up_to_date"), {}, updater_->availableBuild());
    } else if (phase == QStringLiteral("checking") && state == State::Available) {
        journal.insert(QStringLiteral("status"), QStringLiteral("updating"));
        journal.insert(QStringLiteral("target_build"), updater_->availableBuild());
        if (!saveUpdateJournal(journal)) updater_->finishManaged();
        if (!inFlight_) scheduleNext(0);
    } else if (state == State::Failed || (phase == QStringLiteral("downloading") && state == State::Available)) {
        finishUpdate(QStringLiteral("failed"), updater_->message());
    } else if (phase == QStringLiteral("downloading") && state == State::Ready && !inFlight_) {
        scheduleNext(0);
    }
}

bool CloudSyncService::flushUpdateReceipt(quint64 token)
{
    const auto journal = state_.updateJournal;
    const auto phase = journal.value(QStringLiteral("status")).toString();
    const bool ready = phase == QStringLiteral("downloading") && updater_ && updater_->state() == UpdateService::State::Ready;
    const bool starting = phase == QStringLiteral("updating");
    if (!starting && !ready && phase != QStringLiteral("failed") && phase != QStringLiteral("up_to_date")
        && phase != QStringLiteral("applied")) return false;
    const auto id = journal.value(QStringLiteral("id")).toString();
    const auto status = ready ? QStringLiteral("updating") : phase;
    QJsonObject receipt{{QStringLiteral("status"), status},
                        {QStringLiteral("reason"), journal.value(QStringLiteral("reason")).toString()}};
    if (status == QStringLiteral("updating") || status == QStringLiteral("up_to_date"))
        receipt.insert(QStringLiteral("target_build"), journal.value(QStringLiteral("target_build")));
    const auto path = QStringLiteral("/device/operations/%1/result").arg(id);
    api_->postJson(Cloud::apiUrl(state_.origin, path), bearer(), receipt,
                   [this, token, journal, starting, ready](const CloudApi::Reply& reply) {
        if (token != cycleToken_) return;
        if (reply.status == 401) {
            handleUnauthorized();
            return;
        }
        if (journal != state_.updateJournal) {
            finishCycle(token, {}, kMinSyncDelayMs);
            return;
        }
        if (!reply.ok() && reply.status != 404 && reply.status != 409 && reply.status != 422) {
            finishCycle(token, replyError(reply), pendingSyncAfterMs_, replyDiagnostic(reply));
            return;
        }
        const bool approved = reply.ok() &&
            dataObject(reply.body).value(QStringLiteral("status")).toString() == QStringLiteral("updating");
        QString error;
        if ((starting || ready) && approved && state_.enabled && updater_ && updater_->managedUpdate()) {
            if (starting) {
                auto next = journal;
                next.insert(QStringLiteral("status"), QStringLiteral("downloading"));
                if (saveUpdateJournal(next, &error)) updater_->downloadManaged();
            } else {
                updater_->installManaged();
            }
        } else {
            if (!reply.ok()) report(QStringLiteral("Remote update receipt was rejected"), replyDiagnostic(reply));
            saveUpdateJournal({}, &error);
            if (updater_) updater_->finishManaged();
        }
        finishCycle(token, error, kMinSyncDelayMs);
    });
    return true;
}

bool CloudSyncService::recordUpdateHandoff(const QString& installerPath, QString* error)
{
    if (!state_.enabled || !updater_ || !updater_->managedUpdate()
        || state_.updateJournal.value(QStringLiteral("status")).toString() != QStringLiteral("downloading"))
        return fail(error, tr("远程更新任务已失效。"));
    auto journal = state_.updateJournal;
    journal.insert(QStringLiteral("status"), QStringLiteral("installing"));
    journal.insert(QStringLiteral("stage"), QFileInfo(installerPath).dir().dirName());
    return saveUpdateJournal(journal, error);
}

bool CloudSyncService::recoverOperation(QString* error)
{
    auto journal = state_.operationJournal;
    const auto task = journal.value(QStringLiteral("schedule_task")).toObject();
    if (task.isEmpty() || journal.contains(QStringLiteral("status"))) return true;
    const auto changes = task.value(QStringLiteral("changes")).toArray();
    const bool sameProfile = task.value(QStringLiteral("profile_id")).toString() == CloudOperations::activeProfileId();
    const auto& current = ProfileManager::instance().profile();
    if (sameProfile && CloudOperations::matches(current, changes, true)) {
        journal.insert(QStringLiteral("status"), QStringLiteral("applied"));
    } else if (!sameProfile || !CloudOperations::matches(current, changes, false)) {
        journal.insert(QStringLiteral("status"), QStringLiteral("conflict"));
        journal.insert(QStringLiteral("reason"), tr("恢复任务时发现生效档案或相关课表已变化"));
    } else {
        const auto expiry = QDateTime::fromString(task.value(QStringLiteral("expires_at")).toString(), Qt::ISODate);
        if (expiry.isValid() && expiry > QDateTime::currentDateTimeUtc()) return true;
        journal.insert(QStringLiteral("status"), QStringLiteral("expired"));
        journal.insert(QStringLiteral("reason"), tr("任务有效期已过"));
    }
    journal.remove(QStringLiteral("schedule_task"));
    Cloud::State next = state_;
    next.operationJournal = journal;
    return persist(next, error);
}

void CloudSyncService::executeOperation(quint64 token, const QJsonObject& task)
{
    const QString id = task.value(QStringLiteral("id")).toString();
    if (!validTaskId(id)) {
        finishCycle(token, tr("设备任务 ID 无效"), pendingSyncAfterMs_, QStringLiteral("Invalid device operation ID"));
        return;
    }
    const auto complete = [this, token, &id](const QString& status, const QString& reason = {}) {
        sendOperationResult(token, id, status, reason);
    };
    const auto operation = task.value(QStringLiteral("operation")).toObject();
    const QString action = operation.value(QStringLiteral("action")).toString();
    if (action == QStringLiteral("update_app")) {
        executeUpdate(token, id);
        return;
    }
    const auto changes = task.value(QStringLiteral("changes")).toArray();
    const bool recovering = state_.operationJournal.value(QStringLiteral("id")).toString() == id;
    auto& profiles = ProfileManager::instance();
    auto candidate = profiles.profile();
    QString error;
    if (!changes.isEmpty()) {
        if (task.value(QStringLiteral("profile_id")).toString() != CloudOperations::activeProfileId())
            return complete(QStringLiteral("conflict"), tr("当前生效档案已切换"));
        if (recovering && CloudOperations::matches(candidate, changes, true))
            return complete(QStringLiteral("applied"));
        for (const auto& value : changes) {
            const auto date = QDate::fromString(value.toObject().value(QStringLiteral("date")).toString(), Qt::ISODate);
            if (!date.isValid() || date < QDate::currentDate())
                return complete(QStringLiteral("expired"), tr("涉及的日期已过"));
        }
        if (profileEditorDirty_ && profileEditorDirty_())
            return complete(QStringLiteral("blocked"), tr("档案有未保存的编辑"));
        if (!CloudOperations::matches(candidate, changes, false) ||
            !CloudOperations::prepare(candidate, task, &error) ||
            !CloudOperations::matches(candidate, changes, true))
            return complete(QStringLiteral("conflict"), error.isEmpty() ? tr("相关课表已变化") : error);
    } else if (recovering) {
        return complete(QStringLiteral("failed"), tr("上次执行意外中断，无法确认结果"));
    }
    const auto expiry = QDateTime::fromString(task.value(QStringLiteral("expires_at")).toString(), Qt::ISODate);
    if (!expiry.isValid() || expiry <= QDateTime::currentDateTimeUtc())
        return complete(QStringLiteral("expired"), tr("任务有效期已过"));
    Cloud::State next = state_;
    next.operationJournal = {{QStringLiteral("id"), id}};
    if (!changes.isEmpty()) next.operationJournal.insert(QStringLiteral("schedule_task"), task);
    if (!persist(next, &error)) {
        finishCycle(token, error, backoffMs_);
        return;
    }
    if (!changes.isEmpty()) {
        if (!profiles.commit(candidate, &error))
            return complete(QStringLiteral("failed"), error);
        return complete(QStringLiteral("applied"));
    }
    if (action == QStringLiteral("notify")) {
        emit managedNotification(operation.value(QStringLiteral("message")).toString());
        return complete(QStringLiteral("applied"));
    }
    const bool restartSystem = action == QStringLiteral("restart_system");
    if (restartSystem || action == QStringLiteral("shutdown_system")) {
        if (!qtPowerEnabled_ || !CloudPower::supported())
            return complete(QStringLiteral("failed"), tr("客户端不支持 Windows 电源操作"));
        if (powerOperationGuard_) {
            const auto reason = powerOperationGuard_();
            if (!reason.isEmpty()) return complete(QStringLiteral("blocked"), reason);
        }
        if (!CloudPower::request(restartSystem, &error))
            return complete(QStringLiteral("failed"), error);
        const auto bootId = restartSystem ? powerBootId_ : QString();
        const auto reason = restartSystem && bootId.isEmpty()
            ? tr("Windows 已接受重启请求，无法读取系统启动标识") : QString();
        return sendOperationResult(token, id, QStringLiteral("accepted"), reason, bootId);
    }
    const bool activate = action == QStringLiteral("activate_cloud");
    if (activate && isCurrentCloudProfile()) return complete(QStringLiteral("applied"));
    if (activate && !cloudProfileExists_) return complete(QStringLiteral("blocked"), tr("云端档案尚未准备好"));
    if (activate || action == QStringLiteral("restart_app")) {
        if (!managedRestart_) return complete(QStringLiteral("failed"), tr("应用重启不可用"));
        error = managedRestart_(activate ? state_.cloudProfileId : QString());
        if (!error.isEmpty()) return complete(QStringLiteral("blocked"), error);
        managedRestartAccepted_ = true;
        next = state_;
        next.operationJournal.insert(QStringLiteral("status"), QStringLiteral("restart_accepted"));
        next.operationJournal.insert(QStringLiteral("activate_cloud"), activate);
        if (!persist(next, &error))
            report(QStringLiteral("Could not save the device operation journal"), error);
        return;
    }
    complete(QStringLiteral("failed"), tr("客户端不支持该操作"));
}

void CloudSyncService::sendOperationResult(quint64 token, const QString& id, const QString& status,
                                          const QString& reason, const QString& bootId)
{
    QJsonObject receipt{{QStringLiteral("status"), status}, {QStringLiteral("reason"), reason.left(1000)}};
    if (!bootId.isEmpty()) receipt.insert(QStringLiteral("boot_id"), bootId);
    Cloud::State next = state_;
    next.operationJournal = receipt;
    next.operationJournal.insert(QStringLiteral("id"), id);
    QString error;
    if (!persist(next, &error)) {
        finishCycle(token, error, backoffMs_);
        return;
    }
    if (status != QStringLiteral("applied") && status != QStringLiteral("accepted"))
        report(QStringLiteral("Device operation status: %1").arg(status), reason);
    const auto path = QStringLiteral("/device/operations/%1/result").arg(id);
    api_->postJson(Cloud::apiUrl(state_.origin, path), bearer(), receipt,
                   [this, token, status](const CloudApi::Reply& reply) {
                       if (token != cycleToken_) return;
                       if (reply.status == 401) {
                           handleUnauthorized();
                           return;
                       }
                       if (!reply.ok() && reply.status != 404 && reply.status != 409) {
                           finishCycle(token, replyError(reply), pendingSyncAfterMs_,
                                       QStringLiteral("Could not send device operation result: %1").arg(replyDiagnostic(reply)));
                           return;
                       }
                       Cloud::State next = state_;
                       next.operationJournal = {};
                       QString error;
                       if (!persist(next, &error)) {
                           finishCycle(token, error, backoffMs_);
                           return;
                       }
                       finishCycle(token, {}, status == QStringLiteral("blocked") ? pendingSyncAfterMs_ : 5000);
                   });
}

void CloudSyncService::processChannel(quint64 token, const Cloud::Target& target, Step done)
{
    if (token != cycleToken_) return;
    const QString kind = target.kind;
    const bool supportedFormat = kind == Cloud::profileKey()
        ? (target.formatVersion >= 1 && target.formatVersion <= Cloud::kProfileFormatVersion)
        : (target.formatVersion == 1 || target.formatVersion == Cloud::kDisplayFormatVersion
           || (target.formatVersion == Cloud::kDisplayUpdateFormatVersion && updateSupported()));
    if (!target.present || !target.compatible || !supportedFormat) {
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
    const int attempt = qMax(state_.attempts.value(target.assignmentId), target.attempt) + 1;
    if (kind == Cloud::profileKey() && isCurrentCloudProfile() &&
        profileEditorDirty_ && profileEditorDirty_()) {
        const QString blockedReason = QStringLiteral("local_edit");
        const QString signature = target.assignmentId + QLatin1Char('|') + blockedReason;
        if (blockSignature_.value(kind) == signature) {
            done();
            return;
        }
        Cloud::State next = state_;
        next.attempts.insert(target.assignmentId, attempt);
        QString error;
        if (!persist(next, &error)) {
            finishCycle(token, error, backoffMs_);
            return;
        }
        blockSignature_.insert(kind, signature);
        report(QStringLiteral("Sync blocked by unsaved local edits"));
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
        QByteArray nativeContent = contents;
        if (target.formatVersion == 2) {
            auto root = QJsonDocument::fromJson(contents).object();
            if (root.value(QStringLiteral("version")) != QJsonValue(2) ||
                root.value(QStringLiteral("profile")).toObject().contains(QStringLiteral("dateOverrides"))) {
                failChannel(token, target, attempt, QStringLiteral("validation_failed"),
                            tr("常规档案格式无效"), done);
                return;
            }
            root.insert(QStringLiteral("version"), 1);
            nativeContent = QJsonDocument(root).toJson(QJsonDocument::Compact);
        }
        if (!ProfileExchange::decode(nativeContent, ProfileExchange::Format::NativeJson, profile,
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
        if (target.formatVersion == 2) {
            Profile current;
            if (!ProfileManager::instance().readProfile(state_.cloudProfileId, current, &error)) {
                failChannel(token, target, attempt, QStringLiteral("save_failed"), error, done);
                return;
            }
            profile.dateOverrides = current.dateOverrides;
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
        }
        QStringList dates;
        for (const auto& date : canceled) dates.append(date.toString(Qt::ISODate));
        const QString reason = dates.isEmpty() ? QString() :
            tr("临时安排来源已变更，已取消：%1").arg(dates.join(QStringLiteral("、"))).left(1000);
        completeChannel(token, target, attempt, QStringLiteral("applied"), reason, true, done);
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
    if (document.object().value(QStringLiteral("schema_version")).toInt(1) != target.formatVersion) {
        failChannel(token, target, attempt, QStringLiteral("validation_failed"),
                    tr("配置内容与发布格式版本不一致"), done);
        return;
    }
    if (target.formatVersion == Cloud::kDisplayUpdateFormatVersion && !updateSupported()) {
        failChannel(token, target, attempt, QStringLiteral("unsupported_format"),
                    tr("当前客户端不支持展示配置 v4"), done);
        return;
    }
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
    if (document.object().contains(QStringLiteral("windowInterceptionConfig")) &&
        (!windowRulesReload_ || !windowRulesReload_(&error))) {
        failChannel(token, target, attempt, QStringLiteral("window_rules_apply_failed"),
                    error.isEmpty() ? tr("窗口规则重新加载失败") : error, done);
        return;
    }
    completeChannel(token, target, attempt, QStringLiteral("applied"), {}, true, done);
}

void CloudSyncService::failChannel(quint64 token, const Cloud::Target& target, int attempt,
                                   const QString& result, const QString& reason, Step done)
{
    if (token != cycleToken_) return;
    retryAfter_.insert(target.kind, QDateTime::currentDateTime().addSecs(kFailureThrottleSeconds));
    const QString label = target.kind == Cloud::profileKey() ? QStringLiteral("Schedule profile")
                                                            : QStringLiteral("Display configuration");
    report(QStringLiteral("%1 sync failed (%2)").arg(label, result), reason.left(200));
    completeChannel(token, target, attempt, QStringLiteral("failed"),
                    result + QStringLiteral(": ") + reason, false, done);
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
