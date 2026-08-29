#include "core/autostart.hpp"
#include <windows.h>

namespace lb {

namespace {

constexpr wchar_t kRunKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring ReadValue(const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return L"";
    DWORD type = 0, bytes = 0;
    std::wstring out;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS &&
        type == REG_SZ) {
        out.resize(bytes / sizeof(wchar_t));
        if (RegQueryValueExW(key, name, nullptr, &type,
                             reinterpret_cast<BYTE*>(out.data()), &bytes) == ERROR_SUCCESS) {
            while (!out.empty() && out.back() == L'\0') out.pop_back();
        } else {
            out.clear();
        }
    }
    RegCloseKey(key);
    return out;
}

bool WriteValue(const wchar_t* name, const std::wstring& data) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return false;
    bool ok = RegSetValueExW(key, name, 0, REG_SZ,
                             reinterpret_cast<const BYTE*>(data.c_str()),
                             static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t))) ==
              ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}

bool DeleteValue(const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return false;
    bool ok = RegDeleteValueW(key, name) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}

}  // namespace

Autostart::Autostart(const wchar_t* runValueName) : value_(runValueName) {}

bool Autostart::IsEnabled() const { return !ReadValue(value_.c_str()).empty(); }

bool Autostart::Enable() {
    wchar_t exe[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return false;
    return WriteValue(value_.c_str(), std::wstring(L"\"") + exe + L"\"");
}

bool Autostart::Disable() { return DeleteValue(value_.c_str()); }

std::wstring Autostart::Path() const { return ReadValue(value_.c_str()); }

}  // namespace lb
