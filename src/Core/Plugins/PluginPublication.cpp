#include "PluginManager.h"
#include "PluginRuntime.h"
#include "PluginContracts.h"

#include "../ConfigManager/ConfigManager.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>
#include <utility>

namespace
{
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
}

PluginManager::Package PluginManager::stagePackage(const QString &source, QString *error) const
{
    const auto root = QDir(directory_).filePath(QStringLiteral("plugin-versions"));
    const auto sourcePath = QFileInfo(source).canonicalFilePath();
    if (sourcePath.isEmpty() || QFileInfo(source).isSymLink() || QFileInfo(source).isJunction() ||
        !QDir().mkpath(root) || !Plugins::within(directory_, root)) {
        fail(error, QStringLiteral("插件来源或版本目录不可用"));
        return {};
    }
    auto snapshot = std::make_shared<QTemporaryDir>(QDir(root).filePath(QStringLiteral("revision-XXXXXX")));
    if (!snapshot->isValid()) {
        fail(error, snapshot->errorString());
        return {};
    }
    QDirIterator entries(sourcePath, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                         QDirIterator::Subdirectories);
    const QDir sourceDir(sourcePath), targetDir(snapshot->path());
    qint64 size = 0;
    int count = 0;
    while (entries.hasNext()) {
        const auto path = entries.next();
        const auto info = entries.fileInfo();
        const auto relative = sourceDir.relativeFilePath(path);
        const auto target = targetDir.filePath(relative);
        if (info.isSymLink() || info.isJunction() || !Plugins::within(sourcePath, path) ||
            ++count > 512 || (info.isFile() && (size += info.size()) > 16 * 1024 * 1024)) {
            fail(error, QStringLiteral("插件包包含链接、越界路径或超出大小限制"));
            return {};
        }
        const bool copied = info.isDir() ? QDir().mkpath(target)
            : info.isFile() && QDir().mkpath(QFileInfo(target).absolutePath()) && QFile::copy(path, target);
        if (!copied) {
            fail(error, QStringLiteral("无法复制插件文件：%1").arg(relative));
            return {};
        }
    }
    auto manifest = Plugins::Manifest::read(snapshot->path());
    if (!manifest.valid()) {
        fail(error, manifest.failure);
        return {};
    }
    return {manifest, std::move(snapshot), sourcePath};
}

Plugins::Manifest PluginManager::installedRevision(const QString &revision) const
{
    static const QRegularExpression name(QStringLiteral("\\Arevision-[A-Za-z0-9]{6}\\z"));
    const auto root = QDir(directory_).filePath(QStringLiteral("plugin-versions"));
    const auto path = QDir(root).filePath(revision);
    if (!name.match(revision).hasMatch() || !Plugins::within(directory_, root) ||
        !Plugins::within(root, path)) {
        Plugins::Manifest missing;
        missing.failure = QStringLiteral("已发布版本目录不可用");
        return missing;
    }
    return Plugins::Manifest::read(path);
}

bool PluginManager::canPublish() const
{
    return !writesBlocked_ && !quiescing_ && !stopped_ && !publication_;
}

bool PluginManager::pin(const std::shared_ptr<Record> &record, bool enable, QString *error)
{
    auto candidate = ConfigManager::instance().config();
    auto &preference = candidate.pluginConfig.plugins[record->manifest.id];
    Package package;
    if (preference.activeRevision.isEmpty()) {
        package = stagePackage(record->manifest.directory, error);
        if (!package.directory) return false;
        if (package.manifest.id != record->manifest.id ||
            package.manifest.fingerprint != record->manifest.fingerprint)
            return fail(error, QStringLiteral("插件文件已变化，请重新扫描并审阅"));
        preference.activeRevision = QFileInfo(package.directory->path()).fileName();
        preference.sourceName = QFileInfo(record->source).fileName();
    }
    if (enable) {
        preference.enabled = true;
        preference.grantedPermissions = record->manifest.permissions;
        preference.approvedVersion = record->manifest.version;
        preference.fault.clear();
    }
    if (!package.directory && !enable) return true;
    if (!ConfigManager::instance().commit(candidate, error)) return false;
    if (package.directory) {
        package.directory->setAutoRemove(false);
        record->manifest = package.manifest;
    }
    return true;
}

bool PluginManager::publish(const Package &package, PublishMode mode, PublishCompletion completion, QString *error)
{
    if (!canPublish())
        return fail(error, QStringLiteral("当前无法发布插件，或已有发布任务"));
    if (!package.directory || !package.manifest.valid())
        return fail(error, QStringLiteral("插件包尚未通过校验"));
    const auto id = package.manifest.id;
    const auto source = QFileInfo(package.source);
    if (!source.isDir() || source.isSymLink() || source.isJunction() ||
        source.absolutePath() != QFileInfo(pluginsDirectory()).canonicalFilePath() ||
        !Plugins::within(pluginsDirectory(), package.source))
        return fail(error, QStringLiteral("插件源包必须位于 plugins 的直接子目录"));
    for (const auto &record : records_)
        if (record->key != id && record->manifest.id == id)
            return fail(error, QStringLiteral("存在重复插件 ID，请重新扫描并处理冲突"));
    const auto previous = records_.value(id);
    if ((previous && (previous->key != id || previous->manifest.id != id ||
                      previous->state == QStringLiteral("停止中") || previous->state == QStringLiteral("启动中"))) ||
        (previous && !previous->source.isEmpty() && previous->source != package.source))
        return fail(error, QStringLiteral("插件 ID、来源或运行状态已变化"));
    auto transaction = std::make_unique<Publication>();
    transaction->package = package;
    transaction->previous = previous;
    transaction->completion = std::move(completion);
    transaction->mode = mode;
    transaction->candidate = std::make_shared<Record>();
    transaction->candidate->manifest = package.manifest;
    transaction->candidate->key = id;
    transaction->candidate->source = package.source;
    publication_ = std::move(transaction);
    auto proceed = [this, id](bool allowed) {
        if (!publication_ || publication_->candidate->manifest.id != id) return;
        if (!allowed) {
            const auto transaction=std::move(publication_);
            if(transaction->completion) transaction->completion(QStringLiteral("已取消重载"));
            emit changed(); return;
        }
        publication_->prepared=true;
        const auto previous=publication_->previous;
        publication_->wasRunning=previous && previous->state==QStringLiteral("运行中");
        if(previous && previous->runtime) stopRecord(previous,false);
        else QTimer::singleShot(0,this,&PluginManager::activatePublication);
    };
    if(previous && previous->runtime && preparePluginStop) preparePluginStop(id,std::move(proceed));
    else proceed(true);
    emit changed();
    return true;
}

void PluginManager::activatePublication()
{
    if (!publication_ || !publication_->prepared || stopped_) return;
    const auto record = publication_->candidate;
    if (writesBlocked_) {
        finishPublication(QStringLiteral("应用正在退出"));
        return;
    }
    const auto current = Plugins::Manifest::read(record->manifest.directory);
    if (!current.valid() || current.fingerprint != record->manifest.fingerprint) {
        finishPublication(QStringLiteral("已审阅的插件包发生变化，请重新审阅"));
        return;
    }
    records_[record->key] = record;
    startRecord(record);
    if (!record->runtime)
        finishPublication(record->failure.isEmpty() ? QStringLiteral("候选插件无法启动") : record->failure);
}

void PluginManager::finishPublication(const QString &error)
{
    if (!publication_ || stopped_) return;
    const auto record = publication_->candidate;
    if (error.isEmpty()) {
        const auto registrationError=Plugins::contributionRegistrationError(record->manifest.contributions,record->pages,record->components,record->actions);
        if(!registrationError.isEmpty()) { finishPublication(registrationError); return; }
        auto candidate = ConfigManager::instance().config();
        auto &preference = candidate.pluginConfig.plugins[record->key];
        const bool firstPublication = preference.activeRevision.isEmpty() && preference.approvedVersion.isEmpty();
        preference.activeRevision = QFileInfo(publication_->package.directory->path()).fileName();
        preference.sourceName = QFileInfo(record->source).fileName();
        if (firstPublication && publication_->mode == PublishMode::EnableOnFirstInstall)
            preference.enabled = true;
        preference.grantedPermissions = record->manifest.permissions;
        preference.approvedVersion = record->manifest.version;
        preference.fault.clear();
        QString saveError;
        if (!ConfigManager::instance().commit(candidate, &saveError)) {
            finishPublication(saveError);
            return;
        }
        publication_->package.directory->setAutoRemove(false);
        const auto transaction = std::move(publication_);
        if (preference.enabled) {
            record->state = QStringLiteral("运行中");
            emit contributionsChanged();
        } else {
            stopRecord(record, false);
        }
        emit changed();
        if (transaction->completion) transaction->completion({});
        return;
    }
    if (record->runtime) {
        publication_->failure = error;
        stopRecord(record, false);
        return;
    }
    const auto transaction = std::move(publication_);
    if (transaction->previous) {
        records_[record->key] = transaction->previous;
        if (transaction->wasRunning && !writesBlocked_)
            startRecord(transaction->previous);
    } else {
        records_.remove(record->key);
    }
    emit changed();
    if (transaction->completion) transaction->completion(error);
}

