#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

namespace UpdateWindows {
inline constexpr auto GateName = L"Global\\ClassTopLand-Next-Update-{DDB10F20-69A5-4AA0-81B5-901684311554}";
inline constexpr auto LauncherName = L"ClassTopLand-Next-Update.exe";
inline constexpr auto AppName = L"ClassTopLand-Next.exe";
inline constexpr auto Completed = L"complete";
inline constexpr auto ExitAfterUpdate = L"--ctl-autobuild-exit";

class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void reset(HANDLE value = nullptr)
    {
        if (*this) CloseHandle(value_);
        value_ = value;
    }
    HANDLE release()
    {
        HANDLE value = value_;
        value_ = nullptr;
        return value;
    }
private:
    HANDLE value_;
};

inline std::wstring registeredDirectory()
{
    constexpr auto key = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{DDB10F20-69A5-4AA0-81B5-901684311554}_is1";
    wchar_t path[32768];
    DWORD size = sizeof(path);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"InstallLocation",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, path, &size) != ERROR_SUCCESS)
        return {};
    std::wstring result(path);
    while (result.size() > 3 && (result.back() == L'\\' || result.back() == L'/')) result.pop_back();
    return result;
}

inline std::wstring quote(const std::wstring& arg)
{
    std::wstring result(1, L'"');
    size_t backslashes = 0;
    for (const wchar_t character : arg) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        result.append(backslashes * (character == L'"' ? 2 : 1) + (character == L'"' ? 1 : 0), L'\\');
        result.push_back(character);
        backslashes = 0;
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

inline std::wstring modulePath()
{
    wchar_t path[32768];
    const auto length = GetModuleFileNameW(nullptr, path, DWORD(sizeof(path) / sizeof(*path)));
    return length && length < sizeof(path) / sizeof(*path) ? std::wstring(path, length) : std::wstring();
}
}
