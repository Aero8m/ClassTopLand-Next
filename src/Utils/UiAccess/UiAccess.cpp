#include "UiAccess.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QUuid>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <sddl.h>
#include <shlobj.h>
#include <shldisp.h>
#include <shlguid.h>
#include <exdisp.h>
#include <servprov.h>
#include <wrl/client.h>
#endif

namespace UiAccess {
#ifdef Q_OS_WIN
namespace {
using Microsoft::WRL::ComPtr;
constexpr DWORD timeoutMs = 30000;
constexpr auto prefix = "--ctl-uiaccess-";

struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void reset(HANDLE h = nullptr) {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = h;
    }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
QString systemError(DWORD code) {
    wchar_t* message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                   reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    QString result = message ? QString::fromWCharArray(message).trimmed() : QString();
    LocalFree(message);
    return QStringLiteral("%1（Windows 错误 %2）").arg(result).arg(code);
}
bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}
bool tokenFlag(HANDLE token, TOKEN_INFORMATION_CLASS kind) {
    DWORD flag = 0, length = 0;
    return GetTokenInformation(token, kind, &flag, sizeof(flag), &length) && flag != 0;
}
QString userSid(HANDLE token) {
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buffer(size);
    if (!size || !GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) return {};
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) return {};
    const QString result = QString::fromWCharArray(sid);
    LocalFree(sid);
    return result;
}
QString currentSid() {
    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) return {};
    return userSid(token.value);
}
bool lowIntegrity(HANDLE token) {
    DWORD size = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
    std::vector<BYTE> buffer(size);
    if (!size || !GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), size, &size)) return false;
    const auto sid = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer.data())->Label.Sid;
    const auto count = *GetSidSubAuthorityCount(sid);
    return count && *GetSidSubAuthority(sid, count - 1) < SECURITY_MANDATORY_MEDIUM_RID;
}
QString executable() {
    std::vector<wchar_t> path(32768);
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    return size && size < path.size() ? QString::fromWCharArray(path.data(), int(size)) : QString();
}
// Windows argv quoting, including trailing slashes and embedded quotes.
QString quote(const QString& value) {
    QString result = QStringLiteral("\"");
    int slashes = 0;
    for (const QChar c : value) {
        if (c == QLatin1Char('\\')) { ++slashes; continue; }
        result += QString(slashes * (c == QLatin1Char('"') ? 2 : 1), QLatin1Char('\\'));
        if (c == QLatin1Char('"')) result += QLatin1Char('\\');
        result += c;
        slashes = 0;
    }
    return result + QString(slashes * 2, QLatin1Char('\\')) + QLatin1Char('"');
}
QString commandLine(const QStringList& arguments) {
    QStringList encoded;
    for (const auto& argument : arguments) encoded.append(quote(argument));
    return encoded.join(QLatin1Char(' '));
}
QStringList processArguments(int, char**) {
    int count = 0;
    auto** values = CommandLineToArgvW(GetCommandLineW(), &count);
    QStringList result;
    if (values) {
        for (int i = 1; i < count; ++i) result.append(QString::fromWCharArray(values[i]));
        LocalFree(values);
    }
    return result;
}
DWORD waitFor(const std::vector<HANDLE>& handles, DWORD timeout, bool pump = false) {
    const ULONGLONG deadline = GetTickCount64() + timeout;
    while (true) {
        const ULONGLONG now = GetTickCount64();
        const DWORD remaining = DWORD(deadline > now ? deadline - now : 0);
        const DWORD result = WaitForMultipleObjects(DWORD(handles.size()), handles.data(), FALSE,
                                                    pump ? qMin<DWORD>(remaining, 50) : remaining);
        if (result != WAIT_TIMEOUT || !remaining || GetTickCount64() >= deadline) return result;
        if (pump && QCoreApplication::instance())
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
}

// Independent implementation of the token approach described by
// https://github.com/killtimer0/uiaccess (no upstream source is vendored).
DWORD makeUiAccessToken(Handle& output) {
    Handle self;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &self.value)) return GetLastError();
    DWORD session = 0, size = 0;
    if (!GetTokenInformation(self.value, TokenSessionId, &session, sizeof(session), &size)) return GetLastError();
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) return GetLastError();
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    Handle system;
    PRIVILEGE_SET privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Control = PRIVILEGE_SET_ALL_NECESSARY;
    privileges.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!LookupPrivilegeValueW(nullptr, SE_TCB_NAME, &privileges.Privilege[0].Luid)) return GetLastError();
    for (BOOL more = Process32FirstW(snapshot.value, &entry); more; more = Process32NextW(snapshot.value, &entry)) {
        if (_wcsicmp(entry.szExeFile, L"winlogon.exe") != 0) continue;
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
        Handle token;
        if (!process || !OpenProcessToken(process.value, TOKEN_QUERY | TOKEN_DUPLICATE, &token.value)) continue;
        DWORD candidateSession = 0;
        BOOL hasPrivilege = FALSE;
        if (!GetTokenInformation(token.value, TokenSessionId, &candidateSession, sizeof(candidateSession), &size) ||
            candidateSession != session || !PrivilegeCheck(token.value, &privileges, &hasPrivilege) || !hasPrivilege) continue;
        if (!DuplicateTokenEx(token.value, TOKEN_IMPERSONATE, nullptr, SecurityImpersonation,
                              TokenImpersonation, &system.value)) return GetLastError();
        break;
    }
    if (!system) return ERROR_NOT_FOUND;
    if (!SetThreadToken(nullptr, system.value)) return GetLastError();
    DWORD error = ERROR_SUCCESS;
    if (!DuplicateTokenEx(self.value, TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT,
                          nullptr, SecurityImpersonation, TokenPrimary, &output.value)) error = GetLastError();
    else {
        DWORD access = 1;
        if (!SetTokenInformation(output.value, TokenUIAccess, &access, sizeof(access))) error = GetLastError();
    }
    if (!RevertToSelf()) {
        // A broker must never continue with a SYSTEM impersonation left active.
        const DWORD revertError = GetLastError();
        ExitProcess(revertError);
    }
    return error;
}
DWORD launchDirect(HANDLE token, const QStringList& arguments, Handle& process) {
    const std::wstring path = executable().toStdWString();
    std::wstring command = commandLine(QStringList{QString::fromStdWString(path)} + arguments).toStdWString();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    const BOOL launched = token
        ? CreateProcessAsUserW(token, path.c_str(), command.data(), nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)
        : CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info);
    if (!launched) return GetLastError();
    CloseHandle(info.hThread);
    process.reset(info.hProcess);
    return ERROR_SUCCESS;
}
HRESULT launchViaExplorer(const QStringList& arguments) {
    // Obtain the desktop Explorer's automation object, rather than constructing
    // Shell.Application in this elevated process (which can stay elevated).
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct ComScope { bool owned; ~ComScope() { if (owned) CoUninitialize(); } } scope{SUCCEEDED(initialized)};
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return initialized;
    ComPtr<IShellWindows> windows;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows));
    if (FAILED(hr)) return hr;
    VARIANT location{}, root{};
    location.vt = VT_I4;
    location.lVal = CSIDL_DESKTOP;
    long hwnd = 0;
    ComPtr<IDispatch> desktop;
    hr = windows->FindWindowSW(&location, &root, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &desktop);
    if (FAILED(hr) || !desktop) return FAILED(hr) ? hr : E_FAIL;
    ComPtr<IServiceProvider> provider;
    if (FAILED(hr = desktop.As(&provider))) return hr;
    ComPtr<IShellBrowser> browser;
    if (FAILED(hr = provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser)))) return hr;
    ComPtr<IShellView> view;
    if (FAILED(hr = browser->QueryActiveShellView(&view))) return hr;
    ComPtr<IDispatch> background;
    if (FAILED(hr = view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background)))) return hr;
    ComPtr<IShellFolderViewDual> folder;
    if (FAILED(hr = background.As(&folder))) return hr;
    ComPtr<IDispatch> application;
    if (FAILED(hr = folder->get_Application(&application))) return hr;
    ComPtr<IShellDispatch2> shell;
    if (FAILED(hr = application.As(&shell))) return hr;
    const auto path = executable().toStdWString();
    const auto parameters = commandLine(arguments).toStdWString();
    const auto directory = QDir::toNativeSeparators(QDir::currentPath()).toStdWString();
    BSTR file = SysAllocString(path.c_str());
    VARIANT args{}, cwd{}, verb{}, show{};
    args.vt = cwd.vt = verb.vt = VT_BSTR;
    args.bstrVal = SysAllocString(parameters.c_str());
    cwd.bstrVal = SysAllocString(directory.c_str());
    verb.bstrVal = SysAllocString(L"open");
    show.vt = VT_I4;
    show.lVal = SW_HIDE;
    hr = shell->ShellExecute(file, args, cwd, verb, show);
    SysFreeString(file);
    VariantClear(&args); VariantClear(&cwd); VariantClear(&verb);
    return hr;
}
}

struct Handshake {
    QString name, sid, dataDirectory;
    QStringList arguments;
    DWORD parentPid = 0;
    bool targetEnabled = false;
    Handle ready, go, canceled, mapping, launchedProcess, parent;
    struct Result { DWORD error; DWORD childPid; };
    Result* result = nullptr;
    ~Handshake() { if (result) UnmapViewOfFile(result); }
    bool open(bool create, QString* error) {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
        if (create) {
            // Medium integrity permits the ordinary successor to acknowledge a
            // channel created by its elevated predecessor. DACL remains per-user.
            const auto sddl = QStringLiteral("D:P(A;;GA;;;SY)(A;;GA;;;%1)S:(ML;;NW;;;ME)").arg(sid).toStdWString();
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                                      &descriptor, nullptr))
                return fail(error, systemError(GetLastError()));
            attributes.lpSecurityDescriptor = descriptor;
        }
        auto event = [&](Handle& handle, const QString& suffix) {
            const auto path = (name + suffix).toStdWString();
            handle.reset(create ? CreateEventW(&attributes, TRUE, FALSE, path.c_str())
                                : OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, path.c_str()));
            return bool(handle);
        };
        const bool events = event(ready, QStringLiteral(".ready")) && event(go, QStringLiteral(".go")) &&
                            event(canceled, QStringLiteral(".cancel"));
        const auto mapName = (name + QStringLiteral(".result")).toStdWString();
        if (events) mapping.reset(create ? CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE,
                                                              0, sizeof(Result), mapName.c_str())
                                         : OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, mapName.c_str()));
        const DWORD nativeError = GetLastError();
        LocalFree(descriptor);
        if (!events || !mapping) return fail(error, systemError(nativeError));
        result = static_cast<Result*>(MapViewOfFile(mapping.value, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(Result)));
        if (!result) return fail(error, systemError(GetLastError()));
        if (create) *result = {};
        return true;
    }
    void reject(DWORD error) {
        if (result) result->error = error;
        if (canceled) SetEvent(canceled.value);
    }
    QStringList childArguments(bool broker = false) const {
        QStringList values{
            QStringLiteral("--ctl-uiaccess-channel"), name,
            QStringLiteral("--ctl-uiaccess-parent"), QString::number(parentPid),
            QStringLiteral("--ctl-uiaccess-user"), sid,
            QStringLiteral("--ctl-uiaccess-data"), dataDirectory,
            QStringLiteral("--ctl-uiaccess-mode"), targetEnabled ? QStringLiteral("on") : QStringLiteral("off")};
        if (broker) values.append(QStringLiteral("--ctl-uiaccess-broker"));
        return values + arguments;
    }
};

bool supported() { return true; }
bool enabled() {
    Handle token;
    return OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value) && tokenFlag(token.value, TokenUIAccess);
}

Startup inspectStartup(int argc, char** argv, const QString& defaultDataDirectory) {
    Startup startup;
    startup.dataDirectory = defaultDataDirectory;
    const auto values = processArguments(argc, argv);
    auto handoff = std::make_shared<Handshake>();
    bool internal = false, broker = false, valid = true;
    QString mode;
    QStringList seen;
    for (int i = 0; i < values.size(); ++i) {
        const auto& value = values.at(i);
        if (!value.startsWith(QLatin1String(prefix))) { startup.arguments.append(value); continue; }
        internal = true;
        if (seen.contains(value)) { valid = false; break; }
        seen.append(value);
        if (value == QStringLiteral("--ctl-uiaccess-broker")) { broker = true; continue; }
        if (++i == values.size()) { valid = false; break; }
        const auto& argument = values.at(i);
        if (value == QStringLiteral("--ctl-uiaccess-channel")) handoff->name = argument;
        else if (value == QStringLiteral("--ctl-uiaccess-parent")) handoff->parentPid = argument.toUInt(&valid);
        else if (value == QStringLiteral("--ctl-uiaccess-user")) handoff->sid = argument;
        else if (value == QStringLiteral("--ctl-uiaccess-data")) handoff->dataDirectory = argument;
        else if (value == QStringLiteral("--ctl-uiaccess-mode")) mode = argument;
        else valid = false;
        if (!valid) break;
    }
    if (!internal) return startup;
    startup.exitRequested = true;
    startup.exitCode = ERROR_INVALID_PARAMETER;
    if (!valid || !handoff->name.startsWith(QStringLiteral("Local\\ClassTopLand.UiAccess.")) ||
        !handoff->parentPid || handoff->parentPid == GetCurrentProcessId() ||
        !QDir::isAbsolutePath(handoff->dataDirectory) || (mode != QStringLiteral("on") && mode != QStringLiteral("off")))
        return startup;
    // Reject cross-account UAC before touching the original user's channel/data.
    if (handoff->sid != currentSid()) { startup.exitCode = ERROR_ACCESS_DENIED; return startup; }
    QString error;
    if (!handoff->open(false, &error)) return startup;
    handoff->targetEnabled = mode == QStringLiteral("on");
    handoff->arguments = startup.arguments;
    handoff->parent.reset(OpenProcess(SYNCHRONIZE, FALSE, handoff->parentPid));
    if (!handoff->parent || WaitForSingleObject(handoff->parent.value, 0) != WAIT_TIMEOUT) {
        handoff->reject(ERROR_INVALID_HANDLE); return startup;
    }
    if (broker) {
        Handle token, child;
        DWORD code = handoff->targetEnabled ? makeUiAccessToken(token) : ERROR_INVALID_PARAMETER;
        if (!code) code = launchDirect(token.value, handoff->childArguments(), child);
        if (!code) {
            const DWORD result = waitFor({handoff->ready.value, handoff->canceled.value, child.value}, timeoutMs);
            if (result != WAIT_OBJECT_0) {
                code = result == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_PROCESS_ABORTED;
                if (handoff->result->error) code = handoff->result->error;
            }
        }
        if (code) handoff->reject(code);
        startup.exitCode = int(code);
        return startup;
    }
    if (enabled() != handoff->targetEnabled) {
        handoff->reject(ERROR_ACCESS_DENIED); startup.exitCode = ERROR_ACCESS_DENIED; return startup;
    }
    startup.dataDirectory = handoff->dataDirectory;
    startup.handshake = handoff;
    startup.exitRequested = false;
    return startup;
}

bool finishStartup(const Startup& startup, QString* error) {
    const auto& handoff = startup.handshake;
    if (!handoff) return true;
    handoff->result->childPid = GetCurrentProcessId();
    if (!SetEvent(handoff->ready.value)) return fail(error, systemError(GetLastError()));
    const DWORD decision = waitFor({handoff->go.value, handoff->canceled.value, handoff->parent.value}, timeoutMs, true);
    if (decision != WAIT_OBJECT_0) {
        handoff->reject(decision == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_CANCELLED);
        return fail(error, QStringLiteral("重启交接已取消或超时。"));
    }
    if (waitFor({handoff->parent.value, handoff->canceled.value}, timeoutMs, true) != WAIT_OBJECT_0)
        return fail(error, QStringLiteral("旧实例尚未退出，已取消新实例启动。"));
    return true;
}

Restart::Restart() = default;
Restart::~Restart() { if (!committed_) cancel(); }
bool Restart::prepare(bool enable, const QString& dataDirectory, const QStringList& arguments, QString* error) {
    if (error) error->clear();
    cancel();
    committed_ = false;
    Handle self;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &self.value))
        return fail(error, QStringLiteral("读取当前进程令牌失败：%1").arg(systemError(GetLastError())));
    if (lowIntegrity(self.value))
        return fail(error, QStringLiteral("当前程序以低完整性级别运行，无法进行 UIAccess 权限切换。"
                                         "请从正常桌面环境重新构建并启动，检查 EXE 是否带有 Low 完整性标签。"));
    if (tokenFlag(self.value, TokenIsAppContainer))
        return fail(error, QStringLiteral("当前程序运行在 AppContainer 沙箱中，无法进行 UIAccess 权限切换。"
                                         "请从 CLion 或资源管理器正常启动程序。"));
    auto handoff = std::make_shared<Handshake>();
    handoff->name = QStringLiteral("Local\\ClassTopLand.UiAccess.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    handoff->sid = currentSid();
    handoff->parentPid = GetCurrentProcessId();
    handoff->targetEnabled = enable;
    handoff->dataDirectory = QDir(dataDirectory).absolutePath();
    handoff->arguments = arguments;
    if (handoff->sid.isEmpty()) return fail(error, QStringLiteral("无法读取当前用户标识。"));
    QString channelError;
    if (!handoff->open(true, &channelError))
        return fail(error, QStringLiteral("创建重启交接通道失败：%1").arg(channelError));
    handshake_ = handoff;
    DWORD code = ERROR_SUCCESS;
    if (enable && !enabled()) {
        if (tokenFlag(self.value, TokenElevation)) code = launchDirect(nullptr, handoff->childArguments(true), handoff->launchedProcess);
        else {
            const auto path = executable().toStdWString();
            const auto parameters = commandLine(handoff->childArguments(true)).toStdWString();
            const auto directory = QDir::toNativeSeparators(QDir::currentPath()).toStdWString();
            SHELLEXECUTEINFOW execute{};
            execute.cbSize = sizeof(execute);
            execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
            execute.lpVerb = L"runas";
            execute.lpFile = path.c_str();
            execute.lpParameters = parameters.c_str();
            execute.lpDirectory = directory.c_str();
            execute.nShow = SW_HIDE;
            if (!ShellExecuteExW(&execute)) code = GetLastError();
            else handoff->launchedProcess.reset(execute.hProcess);
        }
    } else if (!enable && (enabled() || tokenFlag(self.value, TokenElevation))) {
        const HRESULT hr = launchViaExplorer(handoff->childArguments());
        if (FAILED(hr)) code = DWORD(hr);
    } else {
        Handle token;
        if (enable && !DuplicateTokenEx(self.value, TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY,
                                        nullptr, SecurityImpersonation, TokenPrimary, &token.value)) code = GetLastError();
        if (!code) code = launchDirect(token.value, handoff->childArguments(), handoff->launchedProcess);
    }
    if (!code) {
        std::vector<HANDLE> waits{handoff->ready.value, handoff->canceled.value};
        if (handoff->launchedProcess) waits.push_back(handoff->launchedProcess.value);
        const DWORD result = waitFor(waits, timeoutMs, true);
        if (result != WAIT_OBJECT_0) {
            code = result == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_PROCESS_ABORTED;
            if (handoff->result->error) code = handoff->result->error;
            else if (result == WAIT_OBJECT_0 + 2) GetExitCodeProcess(handoff->launchedProcess.value, &code);
            if (!code) code = ERROR_PROCESS_ABORTED;
        }
    }
    if (code) {
        cancel();
        return fail(error, code == ERROR_CANCELLED ? QStringLiteral("已取消管理员授权，继续使用原来的置顶模式。")
                    : code == ERROR_ACCESS_DENIED ? QStringLiteral("权限切换被拒绝。请使用当前用户的管理员授权，或检查系统安全策略。")
                    : QStringLiteral("权限切换失败：%1").arg(systemError(code)));
    }
    return true;
}
void Restart::commit() {
    if (handshake_ && SetEvent(handshake_->go.value)) committed_ = true;
}
void Restart::cancel() {
    if (handshake_ && !committed_) handshake_->reject(ERROR_CANCELLED);
    handshake_.reset();
}
#else
struct Handshake {};
bool supported() { return false; }
bool enabled() { return false; }
Startup inspectStartup(int argc, char** argv, const QString& directory) {
    Startup startup;
    startup.dataDirectory = directory;
    for (int i = 1; i < argc; ++i) startup.arguments.append(QString::fromLocal8Bit(argv[i]));
    return startup;
}
bool finishStartup(const Startup&, QString*) { return true; }
Restart::Restart() = default;
Restart::~Restart() = default;
bool Restart::prepare(bool, const QString&, const QStringList&, QString* error) {
    if (error) *error = QStringLiteral("UIAccess 仅支持 Windows。");
    return false;
}
void Restart::commit() {}
void Restart::cancel() {}
#endif
}
