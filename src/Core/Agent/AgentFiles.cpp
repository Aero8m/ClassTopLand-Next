#include "AgentFiles.h"
#include "AgentPlatform.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QSet>

namespace
{
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
QByteArray hash(const QString &path, QString *error)
{
    if (!QFileInfo::exists(path)) return QByteArrayLiteral("missing");
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) { fail(error, file.errorString()); return {}; }
    return hash.result().toHex();
}
QString key(const QString &path)
{
#ifdef Q_OS_WIN
    return QDir::cleanPath(path).toCaseFolded();
#else
    return QDir::cleanPath(path);
#endif
}
}

QJsonObject AgentFiles::read(const QString &root, const QJsonObject &args, QString *error)
{
    const auto relative = args.value(QStringLiteral("path")).toString();
    const auto path = AgentPlatform::safePath(root, relative, false, error);
    if (path.isEmpty()) return {};
    if (!QFileInfo(path).isFile() || QFileInfo(path).size() > 16*1024*1024) { fail(error, QStringLiteral("[UNSUPPORTED_FILE] 文件不可读或超过16 MiB")); return {}; }
    const auto before = hash(path, error);
    if (before.isEmpty()) return {};
    if (args.value(QStringLiteral("metadata_only")).toBool()) {
        if (!QFileInfo(path).isFile() || QFileInfo(path).size() > 16*1024*1024) { fail(error, QStringLiteral("资源不是文件或超过 16 MiB")); return {}; }
        observed_[key(path)] = before;
        return {{QStringLiteral("fingerprint"), QString::fromLatin1(before)}, {QStringLiteral("size"), QFileInfo(path).size()},
                {QStringLiteral("metadata_only"), true}, {QStringLiteral("truncated"), false}};
    }
    const auto text = AgentPlatform::readText(root, relative, error);
    if ((error && !error->isEmpty()) || before.isEmpty()) return {};
    if (hash(path, error) != before) { fail(error, QStringLiteral("CONFLICT：读取期间文件发生变化")); return {}; }
    observed_[key(path)] = before;
    const auto lines = text.split(QLatin1Char('\n'));
    const int start = args.value(QStringLiteral("start_line")).toInt(1);
    const int count = args.value(QStringLiteral("line_count")).toInt(200);
    if (start < 1 || count < 1 || count > 2000) { fail(error, QStringLiteral("[INVALID_ARGUMENT] 行号从 1 开始，每次最多 2000 行")); return {}; }
    QString output;
    int end = qMin(lines.size(), qsizetype(start - 1) + count);
    for (int i = start - 1; i < end; ++i) {
        if (output.size() + lines[i].size() > 64*1024) {
            if (output.isEmpty()) { fail(error, QStringLiteral("[UNSUPPORTED_FILE] 单行超过 64 Ki 字符，无法按行读取；请缩小任务或使用支持该格式的工具")); return {}; }
            end = i; break;
        }
        output += QString::number(i+1) + QStringLiteral(": ") + lines[i] + QLatin1Char('\n');
    }
    return {{QStringLiteral("text"), output}, {QStringLiteral("fingerprint"), QString::fromLatin1(before)},
            {QStringLiteral("total_lines"), lines.size()}, {QStringLiteral("next_line"), end < lines.size() ? end+1 : 0},
            {QStringLiteral("truncated"), end < lines.size()}};
}
QJsonObject AgentFiles::search(const QString &root, const QJsonObject &args, bool contents,
                               QString *error, const std::atomic_bool *cancel)
{
    const auto relative = args.value(QStringLiteral("path")).toString(QStringLiteral("."));
    const auto path = AgentPlatform::safePath(root, relative, false, error);
    if (path.isEmpty()) return {};
    if (!QFileInfo(path).isDir()) { fail(error, QStringLiteral("[NOT_DIRECTORY] 搜索目标不是目录")); return {}; }
    const auto pattern = args.value(QStringLiteral("pattern")).toString();
    const auto glob = args.value(QStringLiteral("glob")).toString(QStringLiteral("*"));
    if (contents && pattern.isEmpty()) { fail(error, QStringLiteral("[INVALID_ARGUMENT] 搜索内容不能为空")); return {}; }
    const QRegularExpression matcher(QRegularExpression::wildcardToRegularExpression(glob), QRegularExpression::CaseInsensitiveOption);
    const int offset = args.value(QStringLiteral("offset")).toInt(0), limit = args.value(QStringLiteral("limit")).toInt(100);
    if (offset < 0 || offset > 100000 || limit < 1 || limit > 200) { fail(error, QStringLiteral("[INVALID_ARGUMENT] 无效的分页参数")); return {}; }
    constexpr int maxScannedFiles = 50000;
    constexpr qint64 maxScanMs = 5000;
    QElapsedTimer timer;
    timer.start();
    int scanned = 0;
    QStringList paths;
    QDirIterator it(path, QDir::Files | QDir::NoSymLinks | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            fail(error, QStringLiteral("搜索已取消"));
            return {};
        }
        const auto file = it.next();
        if (++scanned > maxScannedFiles || (scanned % 256 == 0 && timer.elapsed() > maxScanMs)) {
            fail(error, QStringLiteral("搜索范围过大（已扫描超过 %1 个文件或超过 %2 秒），请缩小目录 path 或使用更具体的匹配条件")
                            .arg(maxScannedFiles).arg(maxScanMs / 1000));
            return {};
        }
        const auto rel = root.isEmpty() ? file : QDir(root).relativeFilePath(file);
        if (rel.split(QLatin1Char('/')).contains(QStringLiteral(".git"))) continue;
        if (matcher.match(QFileInfo(file).fileName()).hasMatch() || matcher.match(rel).hasMatch()) paths.append(rel);
        if (paths.size() > 10000) { fail(error, QStringLiteral("搜索超过 10000 个文件，请缩小目录")); return {}; }
    }
    paths.sort();
    QJsonArray matches;
    int found = 0;
    bool more = false;
    for (const auto &file : paths) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            fail(error, QStringLiteral("搜索已取消"));
            return {};
        }
        if (contents && timer.elapsed() > 8000) {
            more = true;
            break;
        }
        QString failure;
        if (AgentPlatform::safePath(root, file, false, &failure).isEmpty()) continue;
        if (!contents) {
            if (found++ < offset) continue;
            if (matches.size() == limit) { more = true; break; }
            matches.append(file); continue;
        }
        const auto text = AgentPlatform::readText(root, file, &failure);
        if (!failure.isEmpty()) continue;
        const auto lines = text.split(QLatin1Char('\n'));
        for (qsizetype i = 0; i < lines.size(); ++i) {
            if (!lines[i].contains(pattern, Qt::CaseInsensitive)) continue;
            if (found++ < offset) continue;
            if (matches.size() == limit) { more = true; break; }
            matches.append(QJsonObject{{QStringLiteral("path"), file}, {QStringLiteral("line"), i+1},
                                      {QStringLiteral("text"), lines[i].left(500)}});
        }
        if (more) break;
    }
    return {{QStringLiteral("matches"), matches}, {QStringLiteral("truncated"), more},
            {QStringLiteral("next_offset"), more ? offset + matches.size() : -1}};
}

bool AgentFiles::apply(const QString &root, const QJsonArray &changes, QString *error, bool preflightOnly)
{
    if (changes.isEmpty() || changes.size() > 64) return fail(error, QStringLiteral("每批接受 1～64 个文件变更"));
    struct Change { QString path, relative; QByteArray before; bool existed; QByteArray content; bool remove; };
    QList<Change> prepared;
    QSet<QString> targets;
    qint64 batchBytes = 0;
    const auto prepare = [&](const QString &relative, const QByteArray &content, bool remove, const QString &expected, bool requireMissing) {
        const auto path = AgentPlatform::safePath(root, relative, true, error);
        if (path.isEmpty()) return false;
        if (QFileInfo(path).isDir() || targets.contains(key(path))) return fail(error, QStringLiteral("目标是目录或同批重复修改：%1").arg(relative));
        const bool exists = QFileInfo::exists(path);
        if (exists && QFileInfo(path).size() > 16*1024*1024) return fail(error, QStringLiteral("文件超过16 MiB"));
        if (requireMissing && exists) return fail(error, QStringLiteral("CONFLICT：目标文件已存在"));
        const auto current = hash(path, error);
        if (current.isEmpty()) return false;
        const auto baseline = expected.isEmpty() ? observed_.value(key(path)) : expected.toLatin1();
        if (exists && baseline.isEmpty()) return fail(error, QStringLiteral("CONFLICT：修改现有文件前请使用 read_file 读取"));
        if (!baseline.isEmpty() && baseline != current) return fail(error, QStringLiteral("CONFLICT：文件已被外部修改，请重新读取"));
        if (remove && !exists) return fail(error, QStringLiteral("文件不存在"));
        QByteArray original;
        if (exists) { QFile file(path); if (file.size() > 16*1024*1024 || !file.open(QIODevice::ReadOnly)) return fail(error, QStringLiteral("[UNSUPPORTED_FILE] 文件不可读或超过16 MiB")); original = file.readAll(); }
        batchBytes += original.size() + content.size();
        if (batchBytes > 32*1024*1024) return fail(error, QStringLiteral("本批修改超过32 MiB，请拆分"));
        prepared.append({path, relative, original, exists, content, remove}); targets.insert(key(path)); return true;
    };
    for (const auto &entry : changes) {
        if (!entry.isObject()) return fail(error, QStringLiteral("变更必须是对象"));
        const auto value = entry.toObject();
        const auto action = value.value(QStringLiteral("action")).toString(QStringLiteral("write"));
        const auto relative = value.value(QStringLiteral("path")).toString();
        const auto expected = value.value(QStringLiteral("expected_fingerprint")).toString();
        if (action == QStringLiteral("rename")) {
            const auto path = AgentPlatform::safePath(root, relative, false, error);
            if (path.isEmpty()) return false;
            QFile file(path);
            if (file.size() > 16*1024*1024 || !file.open(QIODevice::ReadOnly)) return fail(error, QStringLiteral("[UNSUPPORTED_FILE] 文件不可读或超过16 MiB"));
            const auto content = file.readAll();
            if (!prepare(relative, {}, true, expected, false) ||
                !prepare(value.value(QStringLiteral("new_path")).toString(), content, false, {}, true)) return false;
        } else if (action == QStringLiteral("write") || action == QStringLiteral("delete")) {
            if (action == QStringLiteral("write") && !value.value(QStringLiteral("content")).isString())
                return fail(error, QStringLiteral("write 需要 content 字符串"));
            const auto content = value.value(QStringLiteral("content")).toString();
            if (action == QStringLiteral("write") && (content.contains(QChar::Null) || content.toUtf8().size() > 2*1024*1024))
                return fail(error, QStringLiteral("仅支持 2 MiB 内的 UTF-8 文本"));
            if (!prepare(relative, content.toUtf8(), action == QStringLiteral("delete"), expected, false)) return false;
        } else return fail(error, QStringLiteral("[INVALID_ARGUMENT] 未知文件操作"));
    }
    // Verify the complete batch again immediately before its first mutation.
    for (const auto &change : prepared) {
        const auto expected = change.existed ? QCryptographicHash::hash(change.before, QCryptographicHash::Sha256).toHex() : QByteArrayLiteral("missing");
        if (hash(change.path, error) != expected) return fail(error, QStringLiteral("CONFLICT：提交前文件发生变化"));
    }
    if (preflightOnly) return true;
    QList<QString> directories;
    int applied = 0;
    QString failure;
    for (const auto &change : prepared) {
        bool ok = false;
        if (change.remove) ok = QFile::remove(change.path);
        else {
            auto parent = QFileInfo(change.path).absolutePath();
            QStringList missing;
            while (!QFileInfo::exists(parent)) { missing.prepend(parent); parent = QFileInfo(parent).absolutePath(); }
            for (const auto &dir : missing) { if (!QDir().mkdir(dir)) break; directories.append(dir); }
            if (!AgentPlatform::safePath(root, change.relative, true, &failure).isEmpty()) {
                QSaveFile file(change.path);
                ok = file.open(QIODevice::WriteOnly) && file.write(change.content) == change.content.size() && file.commit();
                if (!ok) failure = file.errorString();
            }
        }
        if (!ok) { if (failure.isEmpty()) failure = QStringLiteral("文件操作失败：%1").arg(change.relative); break; }
        ++applied;
    }
    if (applied != prepared.size()) {
        for (int i = applied - 1; i >= 0; --i) {
            const auto &change = prepared[i];
            if (change.existed) {
                QSaveFile file(change.path);
                if (!file.open(QIODevice::WriteOnly) || file.write(change.before) != change.before.size() || !file.commit())
                    failure += QStringLiteral("；恢复失败：") + change.relative;
            } else if (QFileInfo::exists(change.path) && !QFile::remove(change.path)) failure += QStringLiteral("；清理失败：") + change.relative;
        }
        for (auto i = directories.crbegin(); i != directories.crend(); ++i) QDir().rmdir(*i);
        return fail(error, failure);
    }
    for (const auto &change : prepared) observed_[key(change.path)] = hash(change.path, error);
    return !error || error->isEmpty();
}
