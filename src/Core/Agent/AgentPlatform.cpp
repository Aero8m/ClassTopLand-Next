#include "AgentPlatform.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <QTimer>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#include <io.h>
#endif

namespace AgentPlatform
{
namespace
{
constexpr int maxOutput = 256 * 1024;
constexpr int maxFileBytes = 2 * 1024 * 1024;
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

QString decodeEscapes(const QString &text)
{
    QString result;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.mid(i, 2) == QStringLiteral("_x") && i + 6 < text.size() && text[i + 6] == QLatin1Char('_')) {
            bool ok = false;
            const auto value = text.mid(i + 2, 4).toUShort(&ok, 16);
            if (ok) { result += QChar(value); i += 6; continue; }
        }
        result += text[i];
    }
    return result;
}

QString readableError(const QString &text, bool complete)
{
    const auto marker = QStringLiteral("#< CLIXML");
    QString result;
    qsizetype offset = 0;
    while (offset < text.size()) {
        const auto start = text.indexOf(marker, offset);
        if (start < 0) {
            const auto tail = text.mid(offset);
            if (complete || !marker.startsWith(tail.trimmed())) result += tail;
            break;
        }
        result += text.mid(offset, start - offset);
        auto xmlStart = start + marker.size();
        while (xmlStart < text.size() && text[xmlStart].isSpace()) ++xmlStart;
        const auto xmlEnd = text.indexOf(QStringLiteral("</Objs>"), xmlStart);
        if (xmlEnd < 0) { if (complete) result += text.mid(start); break; }
        const auto document = text.mid(xmlStart, xmlEnd + 7 - xmlStart);
        QXmlStreamReader reader(document);
        QString decoded, stream;
        int depth = 0;
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement()) {
                ++depth;
                if (depth == 2) stream = reader.attributes().value(QStringLiteral("S")).toString();
                const auto name = reader.name();
                if ((depth == 2 && name == QStringLiteral("S")) ||
                    (depth == 3 && (name == QStringLiteral("ToString"))) ||
                    (stream == QStringLiteral("progress") && (name == QStringLiteral("AV") || name == QStringLiteral("SD")))) {
                    const auto value = decodeEscapes(reader.readElementText());
                    --depth;
                    if (!value.isEmpty()) {
                        if (!stream.isEmpty() && stream.compare(QStringLiteral("Error"), Qt::CaseInsensitive) != 0)
                            decoded += QLatin1Char('[') + stream + QStringLiteral("] ");
                        decoded += value;
                        if (!decoded.endsWith(QLatin1Char('\n'))) decoded += QLatin1Char('\n');
                    }
                }
            } else if (reader.isEndElement()) --depth;
        }
        result += reader.hasError() ? text.mid(start, xmlEnd + 7 - start) : decoded;
        offset = xmlEnd + 7;
    }
    return result;
}

#ifdef Q_OS_WIN
struct Handle
{
    HANDLE value = nullptr;
    ~Handle() { if (valid()) CloseHandle(value); }
    bool valid() const { return value && value != INVALID_HANDLE_VALUE; }
};

QString windowsError(const QString &message)
{
    return QStringLiteral("%1（Windows 错误 %2）").arg(message).arg(GetLastError());
}

QString credentialTarget(const QString &id, const QString &endpoint)
{
    const auto hash = QCryptographicHash::hash(endpoint.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("ClassTopLand Next/Agent/%1/%2").arg(id, QString::fromLatin1(hash));
}

QString finalPath(HANDLE handle)
{
    std::wstring buffer(32768, L'\0');
    const auto count = GetFinalPathNameByHandleW(handle, buffer.data(), DWORD(buffer.size()), FILE_NAME_NORMALIZED);
    if (!count || count >= buffer.size()) return {};
    return QString::fromWCharArray(buffer.data(), int(count));
}

QString directoryPath(const QString &directory)
{
    const auto path = QDir::toNativeSeparators(directory).toStdWString();
    Handle handle;
    handle.value = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    return handle.valid() && GetFileInformationByHandle(handle.value, &info) &&
           (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? finalPath(handle.value) : QString();
}

QString approvedDirectory(const QString &directory)
{
    const auto actual = directoryPath(directory);
    const auto native = QDir::toNativeSeparators(QDir::cleanPath(directory));
    const auto expected = native.startsWith(QStringLiteral("\\\\"))
        ? QStringLiteral("\\\\?\\UNC\\") + native.mid(2)
        : QStringLiteral("\\\\?\\") + native;
    return actual.compare(expected, Qt::CaseInsensitive) == 0 ? actual : QString();
}

bool isInside(const QString &root, const QString &target, bool includeRoot = false)
{
    if (root.isEmpty() || target.isEmpty()) return false;
    if (includeRoot && target.compare(root, Qt::CaseInsensitive) == 0) return true;
    const auto prefix = root.endsWith(QLatin1Char('\\')) ? root : root + QLatin1Char('\\');
    return target.startsWith(prefix, Qt::CaseInsensitive);
}

QString resolveRelative(const QString &root, const QString &relative)
{
    const auto input = QDir::fromNativeSeparators(relative);
    if (input.contains(QChar::Null) || input.startsWith(QStringLiteral("//?/")) ||
        input.startsWith(QStringLiteral("//./")) || input.split(QLatin1Char('/')).contains(QStringLiteral(".."))) return {};
    if (QDir::isRelativePath(input) && root.isEmpty()) return {};
    const auto path = QDir::cleanPath(QDir::isRelativePath(input)
        ? QDir(root).absoluteFilePath(input.isEmpty() ? QStringLiteral(".") : input) : input);
    // Reject drive-relative paths, alternate data streams and Win32 path aliases.
    if (!QFileInfo(path).isAbsolute() || path.mid(2).contains(QLatin1Char(':'))) return {};
    for (const auto &part : path.split(QLatin1Char('/')))
        if (part.endsWith(QLatin1Char(' ')) || part.endsWith(QLatin1Char('.'))) return {};
    return path;
}
#endif
}

bool canExecute()
{
#ifdef Q_OS_WIN
    Handle token;
    TOKEN_ELEVATION elevation{};
    DWORD uiAccess = 0, size = 0;
    return OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value) &&
           GetTokenInformation(token.value, TokenElevation, &elevation, sizeof(elevation), &size) &&
           GetTokenInformation(token.value, TokenUIAccess, &uiAccess, sizeof(uiAccess), &size) &&
           !elevation.TokenIsElevated && !uiAccess;
#else
    return false;
#endif
}

QString readKey(const QString &id, const QString &endpoint)
{
#ifdef Q_OS_WIN
    const auto target = credentialTarget(id, endpoint).toStdWString();
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) return {};
    const auto key = QString::fromUtf8(reinterpret_cast<const char *>(credential->CredentialBlob),
                                       credential->CredentialBlobSize);
    CredFree(credential);
    return key;
#else
    Q_UNUSED(id)
    Q_UNUSED(endpoint)
    return {};
#endif
}

bool saveKey(const QString &id, const QString &endpoint, const QString &key)
{
#ifdef Q_OS_WIN
    auto target = credentialTarget(id, endpoint).toStdWString();
    auto bytes = key.toUtf8();
    if (bytes.isEmpty() || bytes.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) return false;
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.CredentialBlobSize = DWORD(bytes.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(bytes.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    const bool saved = CredWriteW(&credential, 0);
    SecureZeroMemory(bytes.data(), size_t(bytes.size()));
    return saved;
#else
    Q_UNUSED(id)
    Q_UNUSED(endpoint)
    Q_UNUSED(key)
    return false;
#endif
}

bool removeKey(const QString &id, const QString &endpoint)
{
#ifdef Q_OS_WIN
    const auto target = credentialTarget(id, endpoint).toStdWString();
    return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
#else
    Q_UNUSED(id)
    Q_UNUSED(endpoint)
    return true;
#endif
}

QString resolvePath(const QString &root, const QString &path, bool allowMissing, QString *error)
{
    const auto resolved =
#ifdef Q_OS_WIN
        resolveRelative(root, path);
#else
        QString();
        Q_UNUSED(root) Q_UNUSED(path)
#endif
    if (resolved.isEmpty()) {
        fail(error, QStringLiteral("[INVALID_PATH] 请提供有效绝对路径，或相对于当前编辑目录的路径；不支持 ..、设备路径和备用数据流"));
        return {};
    }
    return safePath({}, resolved, allowMissing, error);
}

QString safePath(const QString &root, const QString &relative, bool allowMissing, QString *error)
{
#ifdef Q_OS_WIN
    if (!canExecute()) { fail(error, QStringLiteral("[MODE_DISABLED] 管理员或 UIAccess 模式禁止 Agent 文件操作")); return {}; }
    const auto path = resolveRelative(root, relative);
    if (path.isEmpty()) { fail(error, QStringLiteral("[INVALID_PATH] 无效路径；无工作区时必须使用绝对路径")); return {}; }
    if (!root.isEmpty()) {
        const auto base = QDir::cleanPath(QDir::fromNativeSeparators(root));
        const auto prefix = base.endsWith(QLatin1Char('/')) ? base : base + QLatin1Char('/');
        if (path.compare(base, Qt::CaseInsensitive) != 0 && !path.startsWith(prefix, Qt::CaseInsensitive)) {
            fail(error, QStringLiteral("[ACCESS_REQUIRED] 路径超出授权目录")); return {};
        }
    }
    auto current = path;
    while (!QFileInfo::exists(current) && !QFileInfo(current).isSymLink()) {
        if (!allowMissing || QFileInfo(current).absolutePath() == current) {
            fail(error, QStringLiteral("[NOT_FOUND] 文件或目录不存在：%1").arg(path)); return {};
        }
        current = QFileInfo(current).absolutePath();
    }
    for (auto part = current;; part = QFileInfo(part).absolutePath()) {
        const auto attributes = GetFileAttributesW(QDir::toNativeSeparators(part).toStdWString().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            fail(error, QStringLiteral("[INVALID_PATH] 无法访问路径或不允许符号链接、重解析点：%1").arg(part)); return {};
        }
        if (QFileInfo(part).absolutePath() == part) break;
    }
    if (current != path && !QFileInfo(current).isDir()) {
        fail(error, QStringLiteral("[INVALID_PATH] 父路径不是目录")); return {};
    }
    const auto parent = QFileInfo(current).isDir() ? current : QFileInfo(current).absolutePath();
    if (approvedDirectory(parent).isEmpty()) {
        fail(error, QStringLiteral("[INVALID_PATH] 路径解析发生变化或不是普通目录")); return {};
    }
    return path;
#else
    Q_UNUSED(root) Q_UNUSED(relative) Q_UNUSED(allowMissing)
    fail(error, QStringLiteral("[MODE_DISABLED] 仅 Windows 支持文件操作")); return {};
#endif
}

QString readText(const QString &root, const QString &relative, QString *error)
{
#ifdef Q_OS_WIN
    const auto path = safePath(root, relative, false, error);
    if (path.isEmpty()) return {};
    const auto base = approvedDirectory(root.isEmpty() ? QFileInfo(path).absolutePath() : root);
    QFile file(path);
    if (canExecute() && !base.isEmpty() && !path.isEmpty() && QFileInfo(path).isFile() &&
        file.open(QIODevice::ReadOnly) &&
        isInside(base, finalPath(reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()))))) {
        const auto data = file.read(maxFileBytes + 1);
        QStringDecoder decode(QStringDecoder::Utf8);
        const auto text = QString(decode(data));
        if (data.size() <= maxFileBytes && !text.contains(QChar::Null) && !decode.hasError() &&
            file.error() == QFileDevice::NoError)
            return text;
        fail(error, QStringLiteral("[UNSUPPORTED_FILE] 仅支持 2 MiB 以内的 UTF-8 文本文件"));
        return {};
    }
#else
    Q_UNUSED(root)
    Q_UNUSED(relative)
#endif
    if (!error || error->isEmpty()) fail(error, QStringLiteral("[IO_ERROR] 文件不可读或读取失败"));
    return {};
}

bool writeText(const QString &root, const QString &relative, const QString &content, QString *error)
{
#ifdef Q_OS_WIN
    const auto bytes = content.toUtf8();
    if (bytes.size() > maxFileBytes)
        return fail(error, QStringLiteral("仅支持写入 2 MiB 以内的 UTF-8 文本文件"));
    if (content.contains(QChar::Null))
        return fail(error, QStringLiteral("写入内容包含无效字符"));
    const auto path = safePath(root, relative, true, error);
    const auto base = approvedDirectory(root.isEmpty() ? QFileInfo(path).absolutePath() : root);
    if (!canExecute() || base.isEmpty() || path.isEmpty())
        return fail(error, QStringLiteral("文件不可写、路径越界或当前模式禁止执行"));
    const auto attributes = GetFileAttributesW(QDir::toNativeSeparators(path).toStdWString().c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (attributes & FILE_ATTRIBUTE_DIRECTORY)
            return fail(error, QStringLiteral("目标路径是目录，不是文件"));
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
            return fail(error, QStringLiteral("目标文件是符号链接或重解析点"));
    }
    const auto parent = directoryPath(QFileInfo(path).absolutePath());
    if (parent.isEmpty() || !isInside(base, parent, true))
        return fail(error, QStringLiteral("父目录不存在或路径越界"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, file.errorString().isEmpty() ? QStringLiteral("无法写入文件") : file.errorString());
    return true;
#else
    Q_UNUSED(root)
    Q_UNUSED(relative)
    Q_UNUSED(content)
    return fail(error, QStringLiteral("仅 Windows 支持写入工作区文件"));
#endif
}

QString listDirectory(const QString &root, const QString &relative, QString *error)
{
#ifdef Q_OS_WIN
    const auto path = safePath(root, relative, false, error);
    if (path.isEmpty()) return {};
    const auto target = directoryPath(path);
    if (!target.isEmpty()) {
        const auto pattern = (target + (target.endsWith(QLatin1Char('\\')) ? QString() : QStringLiteral("\\")) +
                              QLatin1Char('*')).toStdWString();
        WIN32_FIND_DATAW entry{};
        const auto handle = FindFirstFileW(pattern.c_str(), &entry);
        if (handle == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND) fail(error, windowsError(QStringLiteral("无法列出目录")));
            return {};
        }
        QStringList result;
        do {
            const auto name = QString::fromWCharArray(entry.cFileName);
            if (name == QStringLiteral(".") || name == QStringLiteral("..") ||
                (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            if (result.size() == 100) {
                result.append(QStringLiteral("…（仅显示前 100 项）"));
                break;
            }
            result.append(name + (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? QStringLiteral("/") : QString()));
        } while (FindNextFileW(handle, &entry));
        FindClose(handle);
        return result.join(QLatin1Char('\n'));
    }
#else
    Q_UNUSED(root)
    Q_UNUSED(relative)
#endif
    fail(error, QStringLiteral("[NOT_DIRECTORY] 目标不是可读取的目录"));
    return {};
}

bool openApplication(const QString &file, QString *error)
{
#ifdef Q_OS_WIN
    if (canExecute() && QFileInfo(file).isFile() &&
        file.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        const auto path = QDir::toNativeSeparators(file).toStdWString();
        const auto directory = QDir::toNativeSeparators(QFileInfo(file).absolutePath()).toStdWString();
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(path.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(),
                           &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return true;
        }
        return fail(error, windowsError(QStringLiteral("无法启动应用")));
    }
#else
    Q_UNUSED(file)
#endif
    return fail(error, QStringLiteral("需要可访问的 .exe 文件及普通用户权限"));
}

struct Command::State
{
    QElapsedTimer elapsed;
    QByteArray output, errorOutput;
    QString outputText, errorText;
    QString lastOutput, lastError;
    qint64 lastElapsed = -1;
    bool truncated = false;
    int timeoutSeconds = 60;
    QStringDecoder decoder{QStringDecoder::Utf8}, errorDecoder{QStringDecoder::Utf8};
#ifdef Q_OS_WIN
    Handle job, process, outputRead, errorRead;
#endif
};

Command::Command(QObject *parent) : QObject(parent)
{
    connect(&timer_, &QTimer::timeout, this, &Command::poll);
}
Command::~Command() { stop(); }

bool Command::start(const QString &script, const QString &directory, QString *error, int timeoutSeconds)
{
#ifdef Q_OS_WIN
    if (!canExecute() || state_ || approvedDirectory(directory).isEmpty())
        return fail(error, QStringLiteral("命令不可用或工作目录不存在"));
    auto state = std::make_unique<State>();
    state->timeoutSeconds = qBound(1, timeoutSeconds, 600);
    state->job.value = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!state->job.valid() ||
        !SetInformationJobObject(state->job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return fail(error, windowsError(QStringLiteral("无法创建命令进程组")));

    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle outputWrite, errorWrite, input;
    if (!CreatePipe(&state->outputRead.value, &outputWrite.value, &inherit, 0) ||
        !SetHandleInformation(state->outputRead.value, HANDLE_FLAG_INHERIT, 0))
        return fail(error, windowsError(QStringLiteral("无法创建命令输出管道")));
    if (!CreatePipe(&state->errorRead.value, &errorWrite.value, &inherit, 0) ||
        !SetHandleInformation(state->errorRead.value, HANDLE_FLAG_INHERIT, 0))
        return fail(error, windowsError(QStringLiteral("无法创建命令错误管道")));
    input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!input.valid()) return fail(error, windowsError(QStringLiteral("无法初始化命令输入")));

    DWORD_PTR bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> attributes(bytes);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &bytes))
        return fail(error, windowsError(QStringLiteral("无法初始化进程属性")));
    auto handles = std::vector<HANDLE>{input.value, outputWrite.value, errorWrite.value};
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   handles.data(), sizeof(HANDLE) * handles.size(),
                                   nullptr, nullptr)) {
        DeleteProcThreadAttributeList(list);
        return fail(error, windowsError(QStringLiteral("无法设置进程句柄")));
    }
    wchar_t systemDirectory[MAX_PATH];
    const auto length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    const auto program = length && length < MAX_PATH
        ? QString::fromWCharArray(systemDirectory, int(length)) +
              QStringLiteral("\\WindowsPowerShell\\v1.0\\powershell.exe")
        : QString();
    const auto fullScript = QStringLiteral("[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)\n") + script;
    const QByteArray encoded(reinterpret_cast<const char *>(fullScript.utf16()), fullScript.size() * 2);
    auto command = (QLatin1Char('"') + program +
        QStringLiteral("\" -NoLogo -NoProfile -NonInteractive -EncodedCommand ") +
        QString::fromLatin1(encoded.toBase64())).toStdWString();
    const auto cwd = QDir::toNativeSeparators(directory).toStdWString();
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdOutput = outputWrite.value;
    startup.StartupInfo.hStdError = errorWrite.value;
    startup.lpAttributeList = list;
    PROCESS_INFORMATION process{};
    const bool created = !program.isEmpty() &&
        CreateProcessW(program.toStdWString().c_str(), command.data(), nullptr, nullptr, TRUE,
                       CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                       nullptr, cwd.c_str(), &startup.StartupInfo, &process);
    DeleteProcThreadAttributeList(list);
    if (!created) return fail(error, windowsError(QStringLiteral("无法启动 PowerShell")));
    Handle thread;
    thread.value = process.hThread;
    state->process.value = process.hProcess;
    if (!AssignProcessToJobObject(state->job.value, state->process.value) ||
        ResumeThread(thread.value) == DWORD(-1)) {
        const auto message = windowsError(QStringLiteral("无法安全启动命令进程"));
        TerminateProcess(state->process.value, 1);
        return fail(error, message);
    }
    state_ = std::move(state);
    state_->elapsed.start();
    timer_.start(50);
    return true;
#else
    Q_UNUSED(script)
    Q_UNUSED(directory)
    Q_UNUSED(timeoutSeconds)
    return fail(error, QStringLiteral("仅 Windows 支持执行系统命令"));
#endif
}

void Command::stop()
{
    timer_.stop();
    state_.reset();
}

QJsonObject Command::result(bool complete) const
{
    if (!state_) return {};
    return {{QStringLiteral("stdout"), state_->outputText},
            {QStringLiteral("stderr"), readableError(state_->errorText, complete)},
            {QStringLiteral("elapsed_ms"), state_->elapsed.elapsed()},
            {QStringLiteral("truncated"), state_->truncated}};
}

void Command::poll()
{
#ifdef Q_OS_WIN
    if (!state_) return;
    const auto drain = [this](HANDLE pipe, QByteArray &bytes, QString &text, QStringDecoder &decoder) {
        DWORD available = 0, count = 0;
        char buffer[4096];
        for (int i = 0; i < 16 && PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) && available; ++i) {
            if (!ReadFile(pipe, buffer, qMin<DWORD>(available, sizeof(buffer)), &count, nullptr)) break;
            const auto remaining = qMax<qsizetype>(0, maxOutput - state_->output.size() - state_->errorOutput.size());
            const auto kept = qMin<qsizetype>(remaining, count);
            bytes.append(buffer, kept);
            state_->truncated |= kept < count;
            if (kept) text += decoder(QByteArrayView(buffer, kept));
        }
    };
    drain(state_->outputRead.value, state_->output, state_->outputText, state_->decoder);
    drain(state_->errorRead.value, state_->errorOutput, state_->errorText, state_->errorDecoder);
    const bool exited = WaitForSingleObject(state_->process.value, 0) == WAIT_OBJECT_0;
    const bool expired = state_->elapsed.elapsed() >= state_->timeoutSeconds * 1000LL;
    DWORD available = 0;
    bool pending = PeekNamedPipe(state_->outputRead.value, nullptr, 0, nullptr, &available, nullptr) && available;
    pending |= PeekNamedPipe(state_->errorRead.value, nullptr, 0, nullptr, &available, nullptr) && available;
    auto snapshot = result();
    if (state_->lastOutput != state_->outputText || state_->lastError != snapshot.value(QStringLiteral("stderr")).toString() ||
        state_->elapsed.elapsed() / 1000 != state_->lastElapsed) {
        state_->lastOutput = state_->outputText;
        state_->lastError = snapshot.value(QStringLiteral("stderr")).toString();
        state_->lastElapsed = state_->elapsed.elapsed() / 1000;
        emit outputReceived(snapshot);
        if (!state_) return;
    }
    if (!expired && ((exited && pending) || !exited)) return;
    const bool timedOut = expired && (!exited || pending);
    DWORD code = 0;
    if (!GetExitCodeProcess(state_->process.value, &code)) code = 1;
    snapshot.insert(QStringLiteral("stderr"), readableError(state_->errorText, true));
    snapshot.insert(QStringLiteral("timed_out"), timedOut);
    if (!timedOut) snapshot.insert(QStringLiteral("exit_code"), qint64(code));
    const auto error = timedOut ? QStringLiteral("[TIMEOUT] PowerShell 执行超时")
        : code != 0 ? QStringLiteral("PowerShell 退出码 %1").arg(code) : QString();
    stop();
    emit finished(snapshot, error);
#endif
}
}

