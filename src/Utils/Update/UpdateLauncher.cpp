#include "UpdateWindows.h"

#include <array>
#include <bcrypt.h>
#include <cwchar>
#include <memory>
#include <shellapi.h>
#include <string_view>
#include <vector>

namespace {
using namespace UpdateWindows;

HANDLE inherited(const wchar_t* value)
{
    return reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(std::wcstoull(value, nullptr, 10)));
}

void report(const std::wstring& message)
{
    const auto log = L"ClassTopLand Next 更新: " + message + L"\n";
    OutputDebugStringW(log.c_str());
    MessageBoxW(nullptr, message.c_str(), L"ClassTopLand Next 更新", MB_OK | MB_ICONWARNING);
}

bool installerName(std::wstring_view name)
{
    constexpr std::wstring_view prefix = L"ClassTopLand-Next-setup_build_";
    constexpr std::wstring_view suffix = L"-amd64.exe";
    if (name.size() < prefix.size() + suffix.size() + 7 || name.size() > prefix.size() + suffix.size() + 40
        || name.substr(0, prefix.size()) != prefix || name.substr(name.size() - suffix.size()) != suffix)
        return false;
    const auto commit = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
    return commit.find_first_not_of(L"0123456789abcdef") == std::wstring_view::npos;
}

bool matches(HANDLE file, unsigned long long size, std::wstring_view expected)
{
    LARGE_INTEGER actual{};
    if (!GetFileSizeEx(file, &actual) || actual.QuadPart < 0
        || static_cast<unsigned long long>(actual.QuadPart) != size || expected.size() != 64) return false;

    BCRYPT_ALG_HANDLE provider = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    DWORD objectLength = 0, returned = 0;
    bool ok = BCryptGetProperty(provider, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
                                sizeof(objectLength), &returned, 0) >= 0;
    std::vector<UCHAR> object(objectLength);
    if (ok) ok = BCryptCreateHash(provider, &hash, object.data(), objectLength, nullptr, 0, 0) >= 0;

    std::array<UCHAR, 128 * 1024> block{};
    DWORD count = 0;
    while (ok) {
        ok = ReadFile(file, block.data(), DWORD(block.size()), &count, nullptr) != FALSE;
        if (!ok || !count) break;
        ok = BCryptHashData(hash, block.data(), count, 0) >= 0;
    }
    std::array<UCHAR, 32> digest{};
    if (ok) ok = BCryptFinishHash(hash, digest.data(), DWORD(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(provider, 0);

    constexpr wchar_t hex[] = L"0123456789abcdef";
    for (size_t index = 0; ok && index < digest.size(); ++index)
        ok = expected[2 * index] == hex[digest[index] >> 4] && expected[2 * index + 1] == hex[digest[index] & 15];
    return ok;
}

DWORD install(const std::wstring& installer, const std::wstring& directory)
{
    const auto parameters = L"/SILENT /SP- /LANG=chinese /NORESTART /RESTARTEXITCODE=3010 "
                            L"/NOCLOSEAPPLICATIONS /NORESTARTAPPLICATIONS /LOG /DIR=" + quote(directory);
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    execute.lpVerb = L"runas";
    execute.lpFile = installer.c_str();
    execute.lpParameters = parameters.c_str();
    execute.lpDirectory = directory.c_str();
    execute.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&execute)) return GetLastError() == ERROR_CANCELLED ? ERROR_CANCELLED : ERROR_INSTALL_FAILURE;
    Handle process(execute.hProcess);
    DWORD result = ERROR_INSTALL_FAILURE;
    if (process && WaitForSingleObject(process.get(), INFINITE) == WAIT_OBJECT_0)
        GetExitCodeProcess(process.get(), &result);
    return result;
}

bool restart(const std::wstring& application, const std::wstring& workingDirectory,
             int argc, wchar_t* const* argv)
{
    std::wstring command = quote(application);
    for (int index = 8; index < argc; ++index) command += L" " + quote(argv[index]);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const auto working = GetFileAttributesW(workingDirectory.c_str());
    if (!CreateProcessW(application.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        working != INVALID_FILE_ATTRIBUTES && (working & FILE_ATTRIBUTE_DIRECTORY)
                            ? workingDirectory.c_str() : nullptr, &startup, &process))
        return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    const std::unique_ptr<wchar_t*, decltype(&LocalFree)> arguments(
        CommandLineToArgvW(GetCommandLineW(), &argc), LocalFree);
    if (!arguments || argc < 8) return 1;
    auto* argv = arguments.get();
    Handle parent(inherited(argv[1])), commit(inherited(argv[2])), gate(inherited(argv[3]));
    if (!parent || !commit || !gate) return 1;
    if (WaitForSingleObject(parent.get(), INFINITE) != WAIT_OBJECT_0
        || WaitForSingleObject(commit.get(), 0) != WAIT_OBJECT_0) return 1;

    const auto directory = registeredDirectory();
    if (directory.empty()) {
        report(L"无法读取原安装目录。请手动启动或重新安装 ClassTopLand Next。");
        return 1;
    }

    const auto module = modulePath();
    const auto split = module.find_last_of(L"\\/");
    if (split == std::wstring::npos || !installerName(argv[4])) return 1;
    const auto stage = module.substr(0, split);
    const auto installer = stage + L"\\" + argv[4];
    Handle stageLock(CreateFileW(stage.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    Handle package(CreateFileW(installer.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    BY_HANDLE_FILE_INFORMATION stageInfo{}, fileInfo{};
    const auto expectedSize = std::wcstoull(argv[5], nullptr, 10);
    const bool valid = stageLock && package && GetFileInformationByHandle(stageLock.get(), &stageInfo)
        && !(stageInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        && GetFileInformationByHandle(package.get(), &fileInfo)
        && !(fileInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        && expectedSize > 0 && expectedSize <= 256 * 1024 * 1024 && matches(package.get(), expectedSize, argv[6]);

    const DWORD result = valid ? install(installer, directory) : ERROR_INVALID_DATA;
    package.reset();
    stageLock.reset();
    DeleteFileW(installer.c_str());
    Handle marker(CreateFileW((stage + L"\\" + Completed).c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    marker.reset();

    if (result == 3010 || result == 8) {
        gate.reset();
        report(L"安装需要重新启动 Windows。请保存其他工作，再手动重启。");
        return int(result);
    }
    gate.reset();
    const bool started = restart(directory + L"\\" + AppName, argv[7], argc, argv);
    if (!started)
        report(L"无法启动 ClassTopLand Next。安装结果代码：" + std::to_wstring(result) + L"。请尝试手动启动。");
    else if (result != ERROR_SUCCESS && result != ERROR_CANCELLED)
        report(L"更新安装未完成。结果代码：" + std::to_wstring(result) + L"。");
    return started ? int(result) : 1;
}
