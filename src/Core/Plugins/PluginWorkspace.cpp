#include "PluginWorkspace.h"

#include "../ConfigManager/ConfigManager.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>

namespace
{
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
bool writeJson(const QString &path, const QJsonObject &value, QString *error)
{
    QSaveFile file(path);
    const auto bytes = QJsonDocument(value).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, file.errorString());
    return true;
}
QJsonObject readJson(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
        fail(error, QStringLiteral("无法读取版本记录：%1").arg(path));
        return {};
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        fail(error, QStringLiteral("版本记录损坏，已保留原文件"));
        return {};
    }
    return document.object();
}
// Inventory includes directories so empty directories and forbidden links cannot disappear silently.
QStringList inventory(const QString &root, QString *error)
{
    const QFileInfo base(root);
    if (!base.isDir() || base.isSymLink() || base.isJunction()) {
        fail(error, QStringLiteral("插件目录不可用或包含链接"));
        return {};
    }
    QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    QStringList entries;
    qint64 bytes = 0;
    while (it.hasNext()) {
        const auto path = it.next();
        const auto info = it.fileInfo();
        if (info.isSymLink() || info.isJunction() || !Plugins::within(root, path) ||
            (!info.isFile() && !info.isDir()) || entries.size() >= 512 ||
            (info.isFile() && (bytes += info.size()) > 16 * 1024 * 1024)) {
            fail(error, QStringLiteral("插件包含链接、越界路径或超过 512 项／16 MiB"));
            return {};
        }
        entries.append(QDir(root).relativeFilePath(path) + (info.isDir() ? QStringLiteral("/") : QString()));
    }
    entries.sort();
    return entries;
}
bool copyTree(const QString &from, const QString &to, QString *error)
{
    QString failure;
    const auto entries = inventory(from, &failure);
    if (!failure.isEmpty()) return fail(error, failure);
    if (!QDir().mkpath(to)) return fail(error, QStringLiteral("无法创建插件备份目录"));
    for (const auto &relative : entries) {
        const auto target = QDir(to).filePath(relative);
        if (relative.endsWith(QLatin1Char('/'))) {
            if (!QDir().mkpath(target)) return fail(error, QStringLiteral("无法创建目录：%1").arg(relative));
        } else if (!QDir().mkpath(QFileInfo(target).absolutePath()) ||
                   !QFile::copy(QDir(from).filePath(relative), target)) {
            return fail(error, QStringLiteral("无法备份文件：%1").arg(relative));
        }
    }
    return true;
}
QByteArray bytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool validVersion(const QString &version)
{
    const QUuid value(version);
    return !value.isNull() && value.toString(QUuid::WithoutBraces) == version;
}
}

PluginWorkspace::PluginWorkspace(const QString &directory, PluginManager *manager)
    : QObject(manager), directory_(directory), manager_(manager) {}

QString PluginWorkspace::sourcePath(const QString &plugin) const
{
    if (!Plugins::validId(plugin)) return {};
    if (const auto record = manager_->record(plugin); record && !record->source.isEmpty())
        return record->source;
    const auto preference = ConfigManager::instance().config().pluginConfig.plugins.value(plugin);
    const auto name = preference.sourceName.isEmpty() ? plugin : preference.sourceName;
    if (QFileInfo(name).fileName() != name || name == QStringLiteral(".") || name == QStringLiteral("..")) return {};
    return QDir(manager_->pluginsDirectory()).filePath(name);
}

QString PluginWorkspace::source(const QString &plugin, QString *error)
{
    if (!Plugins::validId(plugin)) { fail(error, QStringLiteral("无效的插件 ID")); return {}; }
    const auto pluginsRoot = manager_->pluginsDirectory();
    if (!QDir().mkpath(pluginsRoot) || QFileInfo(pluginsRoot).isSymLink() || QFileInfo(pluginsRoot).isJunction() ||
        !Plugins::within(directory_, pluginsRoot)) { fail(error, QStringLiteral("插件目录越界或是链接")); return {}; }
    const auto path = sourcePath(plugin);
    if (QFileInfo(path).isDir()) {
        if (!Plugins::within(manager_->pluginsDirectory(), path) || QFileInfo(path).isSymLink() || QFileInfo(path).isJunction()) {
            fail(error, QStringLiteral("插件源目录越界或是链接")); return {};
        }
        return QFileInfo(path).canonicalFilePath();
    }
    fail(error, QStringLiteral("插件源目录不存在"));
    return {};
}

bool PluginWorkspace::create(const QString &plugin, QString *error)
{
    if (!manager_->canPublish() || !Plugins::validId(plugin) || manager_->record(plugin))
        return fail(error, QStringLiteral("无法创建插件：权限不足、ID 无效或已安装"));
    const auto path = sourcePath(plugin);
    if (QFileInfo::exists(path)) return fail(error, QStringLiteral("插件目录已存在，请使用 open_plugin"));
    const auto pluginsRoot = manager_->pluginsDirectory();
    if (!QDir().mkpath(pluginsRoot) || QFileInfo(pluginsRoot).isSymLink() || QFileInfo(pluginsRoot).isJunction() ||
        !Plugins::within(directory_, pluginsRoot)) return fail(error, QStringLiteral("插件目录越界或是链接"));
    QTemporaryDir candidate(QDir(pluginsRoot).filePath(QStringLiteral(".create-XXXXXX")));
    if (!candidate.isValid()) return fail(error, candidate.errorString());
    if (!writeJson(QDir(candidate.path()).filePath(QStringLiteral("manifest.json")),
        {{QStringLiteral("manifestVersion"), 2}, {QStringLiteral("apiVersion"), 2}, {QStringLiteral("id"), plugin},
         {QStringLiteral("name"), plugin}, {QStringLiteral("version"), QStringLiteral("0.1.0")},
         {QStringLiteral("entry"), QStringLiteral("main.mjs")}, {QStringLiteral("permissions"), QJsonObject()},
         {QStringLiteral("contributions"), QJsonObject()}}, error)) return false;
    QSaveFile script(QDir(candidate.path()).filePath(QStringLiteral("main.mjs")));
    const QByteArray content("export function activate(ctx) {}\n");
    if (!script.open(QIODevice::WriteOnly) || script.write(content) != content.size() || !script.commit())
        return fail(error, script.errorString());
    if (!QDir().rename(candidate.path(), path)) return fail(error, QStringLiteral("无法创建插件目录"));
    candidate.setAutoRemove(false);
    return true;
}

QJsonArray PluginWorkspace::list() const
{
    QJsonArray result;
    for (const auto &record : manager_->records()) {
        if (record->key != record->manifest.id) continue;
        result.append(QJsonObject{{QStringLiteral("id"), record->key}, {QStringLiteral("name"), record->manifest.name},
            {QStringLiteral("state"), record->state}, {QStringLiteral("source"), sourcePath(record->key)},
            {QStringLiteral("runningRevision"), QString::fromLatin1(record->manifest.fingerprint)},
            {QStringLiteral("pending"), record->updatePending}, {QStringLiteral("error"), record->failure}});
    }
    return result;
}

QString PluginWorkspace::historyDirectory(const QString &plugin) const
{
    return Plugins::validId(plugin) ? QDir(directory_).filePath(QStringLiteral("plugin-history/") + plugin) : QString();
}
QString PluginWorkspace::versionDirectory(const QString &plugin, const QString &version) const
{
    const auto root = historyDirectory(plugin);
    if (root.isEmpty() || !validVersion(version)) return {};
    const auto path = QDir(root).filePath(version);
    return Plugins::within(directory_, root) && Plugins::within(root, path) ? path : QString();
}
QByteArray PluginWorkspace::fingerprint(const QString &root, QString *error)
{
    QString failure;
    const auto entries = inventory(root, &failure);
    if (!failure.isEmpty()) { fail(error, failure); return {}; }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto &entry : entries) {
        hash.addData(entry.toUtf8()); hash.addData(QByteArrayView("\0", 1));
        if (!entry.endsWith(QLatin1Char('/'))) {
            QFile file(QDir(root).filePath(entry));
            QCryptographicHash content(QCryptographicHash::Sha256);
            if (!file.open(QIODevice::ReadOnly) || !content.addData(&file)) { fail(error, file.errorString()); return {}; }
            hash.addData(content.result());
        }
    }
    return hash.result().toHex();
}

QString PluginWorkspace::checkpoint(const QString &plugin, const QString &session, const QString &summary, QString *error)
{
    const auto path = source(plugin, error);
    if (path.isEmpty()) return {};
    const auto before = fingerprint(path, error);
    if (before.isEmpty()) return {};
    const auto root = historyDirectory(plugin);
    const auto container = QFileInfo(root).absolutePath();
    if (!QDir().mkpath(container) || QFileInfo(container).isSymLink() || QFileInfo(container).isJunction() || !Plugins::within(directory_, container)) {
        fail(error, QStringLiteral("版本目录越界或是链接")); return {};
    }
    if (!QDir().mkpath(root) || !Plugins::within(directory_, root)) { fail(error, QStringLiteral("无法创建版本目录")); return {}; }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QTemporaryDir staging(QDir(root).filePath(QStringLiteral(".checkpoint-XXXXXX")));
    if (!staging.isValid()) { fail(error, staging.errorString()); return {}; }
    const auto target = staging.path();
    if (!copyTree(path, QDir(target).filePath(QStringLiteral("package")), error)) return {};
    if (before != fingerprint(path, error) || before != fingerprint(QDir(target).filePath(QStringLiteral("package")), error)) {
        fail(error, QStringLiteral("备份时源文件发生变化，请重试")); return {};
    }
    const auto active = ConfigManager::instance().config().pluginConfig.plugins.value(plugin).activeRevision;
    if (!writeJson(QDir(target).filePath(QStringLiteral("record.json")),
        {{QStringLiteral("version"), 1}, {QStringLiteral("id"), id}, {QStringLiteral("plugin"), plugin},
         {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
         {QStringLiteral("session"), session}, {QStringLiteral("summary"), summary.left(1000)},
         {QStringLiteral("fingerprint"), QString::fromLatin1(before)}, {QStringLiteral("activeRevision"), active},
         {QStringLiteral("status"), QStringLiteral("editing")}}, error)) return {};
    if (!QDir().rename(target, QDir(root).filePath(id))) { fail(error, QStringLiteral("无法提交版本快照")); return {}; }
    staging.setAutoRemove(false);
    emit changed();
    return id;
}
QJsonArray PluginWorkspace::versions(const QString &plugin, QString *error) const
{
    QJsonArray result;
    const auto root = historyDirectory(plugin);
    if (root.isEmpty()) { fail(error, QStringLiteral("无效的插件 ID")); return {}; }
    if (!QFileInfo::exists(root)) return result;
    if (!Plugins::within(directory_, root)) { fail(error, QStringLiteral("版本目录越界")); return {}; }
    QList<QJsonObject> records;
    for (const auto &entry : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        if (versionDirectory(plugin, entry.fileName()).isEmpty() || entry.isJunction()) continue;
        auto value = readJson(QDir(entry.absoluteFilePath()).filePath(QStringLiteral("record.json")), error);
        if (value.isEmpty()) return {};
        if (value.value(QStringLiteral("version")).toInt() != 1 || value.value(QStringLiteral("id")).toString() != entry.fileName() ||
            value.value(QStringLiteral("plugin")).toString() != plugin) { fail(error, QStringLiteral("版本记录不匹配")); return {}; }
        records.append(value);
    }
    std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
        return a.value(QStringLiteral("time")).toString() > b.value(QStringLiteral("time")).toString();
    });
    for (const auto &record : records) result.append(record);
    return result;
}
bool PluginWorkspace::changed(const QString &plugin, const QString &version, QString *error) const
{
    const auto root = versionDirectory(plugin, version);
    if (root.isEmpty()) { fail(error, QStringLiteral("版本不存在")); return false; }
    const auto record = readJson(QDir(root).filePath(QStringLiteral("record.json")), error);
    const auto current = fingerprint(sourcePath(plugin), error);
    return !current.isEmpty() && current != record.value(QStringLiteral("fingerprint")).toString().toLatin1();
}
QString PluginWorkspace::difference(const QString &plugin, const QString &version, QString *error) const
{
    const auto root = versionDirectory(plugin, version);
    if (root.isEmpty()) { fail(error, QStringLiteral("版本不存在")); return {}; }
    const auto before = QDir(root).filePath(QStringLiteral("package")), after = sourcePath(plugin);
    QString failure;
    const auto oldFiles = inventory(before, &failure), newFiles = inventory(after, &failure);
    if (!failure.isEmpty()) { fail(error, failure); return {}; }
    auto all = oldFiles + newFiles; all.removeDuplicates(); all.sort();
    QString result;
    for (const auto &file : all) {
        if (file.endsWith(QLatin1Char('/'))) continue;
        const bool oldExists = oldFiles.contains(file), newExists = newFiles.contains(file);
        const auto oldBytes = bytes(QDir(before).filePath(file)), newBytes = bytes(QDir(after).filePath(file));
        if (oldExists == newExists && oldBytes == newBytes) continue;
        result += QStringLiteral("--- %1\n+++ %2\n").arg(oldExists ? file : QStringLiteral("/dev/null"), newExists ? file : QStringLiteral("/dev/null"));
        if (oldBytes.contains('\0') || newBytes.contains('\0')) result += QStringLiteral("（二进制文件变化）\n");
        else {
            const auto a = QString::fromUtf8(oldBytes).split(QLatin1Char('\n')), b = QString::fromUtf8(newBytes).split(QLatin1Char('\n'));
            int start = 0, end = 0;
            while (start < a.size() && start < b.size() && a[start] == b[start]) ++start;
            while (end < a.size() - start && end < b.size() - start && a[a.size()-1-end] == b[b.size()-1-end]) ++end;
            result += QStringLiteral("@@ -%1,%2 +%1,%3 @@\n").arg(start + 1).arg(a.size()-start-end).arg(b.size()-start-end);
            for (qsizetype i = start; i < a.size()-end && result.size() < 256*1024; ++i) result += QLatin1Char('-') + a[i] + QLatin1Char('\n');
            for (qsizetype i = start; i < b.size()-end && result.size() < 256*1024; ++i) result += QLatin1Char('+') + b[i] + QLatin1Char('\n');
        }
        if (result.size() >= 256*1024) { result.truncate(256*1024); result += QStringLiteral("\n…差异已截断\n"); break; }
    }
    return result;
}
bool PluginWorkspace::finish(const QString &plugin, const QString &version, const QString &status, QString *error)
{
    const auto root = versionDirectory(plugin, version);
    if (root.isEmpty()) return fail(error, QStringLiteral("版本不存在"));
    const auto path = QDir(root).filePath(QStringLiteral("record.json"));
    auto record = readJson(path, error);
    if (record.isEmpty()) return false;
    if (!changed(plugin, version, error) && (!error || error->isEmpty())) {
        if (!QDir(root).removeRecursively()) return fail(error, QStringLiteral("无法删除未变化的备份"));
    } else {
        record.insert(QStringLiteral("status"), status);
        if (!writeJson(path, record, error)) return false;
    }
    if (status == QStringLiteral("applied") || status == QStringLiteral("rolled-back")) {
        const auto history = versions(plugin, error);
        if (error && !error->isEmpty()) return false;
        for (const auto &entry : history) {
            auto previous = entry.toObject();
            const auto id = previous.value(QStringLiteral("id")).toString();
            const auto state = previous.value(QStringLiteral("status")).toString();
            if (id == version || (state != QStringLiteral("pending") && state != QStringLiteral("editing")) ||
                previous.value(QStringLiteral("time")).toString() > record.value(QStringLiteral("time")).toString()) continue;
            previous.insert(QStringLiteral("status"), QStringLiteral("superseded"));
            if (!writeJson(QDir(versionDirectory(plugin, id)).filePath(QStringLiteral("record.json")), previous, error)) return false;
        }
    }
    if (!prune(plugin, error)) return false;
    emit changed(); return true;
}
bool PluginWorkspace::prune(const QString &plugin, QString *error)
{
    const auto records = versions(plugin, error);
    if (error && !error->isEmpty()) return false;
    const auto active = ConfigManager::instance().config().pluginConfig.plugins.value(plugin).activeRevision;
    const auto running = manager_->record(plugin);
    for (qsizetype i = 20; i < records.size(); ++i) {
        const auto record = records[i].toObject();
        const auto status = record.value(QStringLiteral("status")).toString();
        if (status == QStringLiteral("editing") || status == QStringLiteral("pending") || status == QStringLiteral("restoring")) continue;
        const auto path = versionDirectory(plugin, record.value(QStringLiteral("id")).toString());
        if (running && !active.isEmpty() && record.value(QStringLiteral("activeRevision")).toString() == active &&
            Plugins::Manifest::read(QDir(path).filePath(QStringLiteral("package"))).fingerprint == running->manifest.fingerprint) continue;
        if (path.isEmpty() || !QDir(path).removeRecursively()) return fail(error, QStringLiteral("无法清理旧版本"));
    }
    return true;
}
PluginManager::Package PluginWorkspace::validate(const QString &plugin, QString *error)
{
    const auto path = source(plugin, error);
    if (path.isEmpty()) return {};
    auto package = manager_->stagePackage(path, error);
    if (package.manifest.id != plugin) { fail(error, QStringLiteral("不能更改现有插件 ID")); return {}; }
    return package;
}
bool PluginWorkspace::apply(const QString &plugin, PluginManager::PublishCompletion completion, QString *error)
{
    auto package = validate(plugin, error);
    if (!package.directory) return false;
    return manager_->publish(package, PluginManager::PublishMode::PreserveEnabled, std::move(completion), error);
}
bool PluginWorkspace::restore(const QString &plugin, const QString &snapshot, QString *error)
{
    const auto path = sourcePath(plugin), parent = QFileInfo(path).absolutePath();
    if (!Plugins::within(manager_->pluginsDirectory(), path) || QFileInfo(path).isSymLink() || QFileInfo(path).isJunction())
        return fail(error, QStringLiteral("插件源目录越界"));
    QTemporaryDir staging(QDir(parent).filePath(QStringLiteral(".restore-XXXXXX")));
    if (!staging.isValid() || !copyTree(snapshot, QDir(staging.path()).filePath(QStringLiteral("new")), error)) return false;
    QDir dir;
    const auto old = QDir(staging.path()).filePath(QStringLiteral("old"));
    if (!dir.rename(path, old)) return fail(error, QStringLiteral("无法替换源目录，文件可能被占用"));
    if (!dir.rename(QDir(staging.path()).filePath(QStringLiteral("new")), path)) {
        if (!dir.rename(old, path)) { staging.setAutoRemove(false); return fail(error, QStringLiteral("恢复失败，原包保留在 %1").arg(old)); }
        return fail(error, QStringLiteral("无法安装恢复包"));
    }
    return true;
}
bool PluginWorkspace::rollback(const QString &plugin, const QString &version, PluginManager::PublishCompletion completion, QString *error, bool fromAgent)
{
    if (restoring_ || manager_->publishing() || !manager_->canPublish() || (agentBusy_ && !fromAgent)) return fail(error, QStringLiteral("当前无法回滚，请等待插件操作完成或停止 Agent"));
    const auto root = versionDirectory(plugin, version);
    if (root.isEmpty()) return fail(error, QStringLiteral("版本不存在"));
    const auto record = readJson(QDir(root).filePath(QStringLiteral("record.json")), error);
    const auto target = QDir(root).filePath(QStringLiteral("package"));
    if (record.isEmpty() || fingerprint(target, error) != record.value(QStringLiteral("fingerprint")).toString().toLatin1())
        return fail(error, QStringLiteral("备份包校验失败，未修改源文件"));
    const auto backup = checkpoint(plugin, {}, QStringLiteral("回滚前备份"), error);
    if (backup.isEmpty()) return false;
    const auto saved = QDir(versionDirectory(plugin, backup)).filePath(QStringLiteral("package"));
    if (fingerprint(sourcePath(plugin), error) != fingerprint(saved, error))
        return fail(error, QStringLiteral("CONFLICT：回滚前源文件发生变化，请重新审阅"));
    const auto recoveryRoot = QDir(directory_).filePath(QStringLiteral("plugin-recovery"));
    if (!QDir().mkpath(recoveryRoot) || !Plugins::within(directory_, recoveryRoot))
        return fail(error, QStringLiteral("无法创建回滚恢复日志"));
    const auto journal = QDir(recoveryRoot).filePath(plugin + QStringLiteral(".json"));
    const auto previousRevision = ConfigManager::instance().config().pluginConfig.plugins.value(plugin).activeRevision;
    if (!writeJson(journal, {{QStringLiteral("version"), 1}, {QStringLiteral("plugin"), plugin},
        {QStringLiteral("backup"), backup}, {QStringLiteral("activeRevision"), previousRevision}}, error)) return false;
    if (!restore(plugin, target, error)) return false;
    restoring_ = true;
    const auto done = [this, plugin, backup, saved, journal, completion](const QString &failure) {
        QString result = failure, storage;
        if (!failure.isEmpty() && !restore(plugin, saved, &storage)) result += QStringLiteral("；恢复回滚前文件失败：") + storage;
        if (!finish(plugin, backup, result.isEmpty() ? QStringLiteral("rolled-back") : QStringLiteral("pending"), &storage))
            result += QStringLiteral("；版本记录保存失败：") + storage;
        if (storage.isEmpty() && !QFile::remove(journal)) result += QStringLiteral("；恢复日志清理失败");
        restoring_ = false;
        manager_->scan(); emit changed();
        if (completion) completion(result);
    };
    if (!apply(plugin, done, error)) { const auto failure = error ? *error : QStringLiteral("回滚加载失败"); done(failure); return true; }
    return true;
}

bool PluginWorkspace::recover(QString *error)
{
    const auto root = QDir(directory_).filePath(QStringLiteral("plugin-recovery"));
    if (!QFileInfo::exists(root)) return true;
    if (!Plugins::within(directory_, root)) return fail(error, QStringLiteral("恢复日志目录越界"));
    for (const auto &file : QDir(root).entryInfoList({QStringLiteral("*.json")}, QDir::Files | QDir::NoSymLinks)) {
        const auto value = readJson(file.absoluteFilePath(), error);
        const auto plugin = value.value(QStringLiteral("plugin")).toString();
        if (value.value(QStringLiteral("version")).toInt() != 1 || !Plugins::validId(plugin) || file.completeBaseName() != plugin)
            return fail(error, QStringLiteral("回滚恢复日志损坏，原文件保留"));
        const auto backup = value.value(QStringLiteral("backup")).toString();
        const auto version = versionDirectory(plugin, backup);
        if (version.isEmpty()) return fail(error, QStringLiteral("回滚备份不可用"));
        const auto saved = QDir(version).filePath(QStringLiteral("package"));
        const auto record = readJson(QDir(version).filePath(QStringLiteral("record.json")), error);
        if (record.isEmpty() || fingerprint(saved, error) != record.value(QStringLiteral("fingerprint")).toString().toLatin1())
            return fail(error, QStringLiteral("回滚备份损坏"));
        const auto active = ConfigManager::instance().config().pluginConfig.plugins.value(plugin).activeRevision;
        // If the revision pointer changed, the atomic publication already committed.
        if (active == value.value(QStringLiteral("activeRevision")).toString()) {
            const auto source = sourcePath(plugin);
            if (!QFileInfo::exists(source)) {
                if (!copyTree(saved, source, error)) return false;
            } else if (!restore(plugin, saved, error)) return false;
        }
        if (!QFile::remove(file.absoluteFilePath())) return fail(error, QStringLiteral("无法清理回滚恢复日志"));
    }
    return true;
}
