#include "core/appindex.hpp"
#include <windows.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <filesystem>
#include <algorithm>
#include <cwctype>
#pragma comment(lib, "uuid")
#pragma comment(lib, "shell32")

namespace lb {
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

static std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

// 解析 .lnk 的目标路径；失败/无目标返回空串
static std::wstring ResolveLnk(const std::wstring& lnk) {
    ComPtr<IShellLinkW> sl;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(sl.GetAddressOf()))))
        return {};
    ComPtr<IPersistFile> pf;
    if (FAILED(sl.As(&pf))) return {};
    if (FAILED(pf->Load(lnk.c_str(), STGM_READ))) return {};
    wchar_t buf[MAX_PATH]{};
    if (FAILED(sl->GetPath(buf, MAX_PATH, nullptr, SLGP_UNCPRIORITY))) return {};
    return buf;
}

void AddUniqueItem(std::vector<AppItem>& items, AppItem it) {
    if (it.path.empty()) return;
    std::wstring key = ToLower(it.path);
    for (auto& e : items)
        if (ToLower(e.path) == key) return;   // 去重（首个优先）
    items.push_back(std::move(it));
}

void AppIndex::Build() {
    // 1) 开始菜单 Programs\*.lnk（当前用户 + 所有用户）
    std::vector<fs::path> roots;
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &raw))) {
        roots.emplace_back(raw);
        CoTaskMemFree(raw);
        raw = nullptr;
    }
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, nullptr, &raw))) {
        roots.emplace_back(raw);
        CoTaskMemFree(raw);
        raw = nullptr;
    }
    std::error_code ec;
    for (auto& root : roots) {
        for (fs::recursive_directory_iterator it(
                 root, fs::directory_options::skip_permission_denied, ec),
             end;
             !ec && it != end; it.increment(ec)) {
            if (it->is_directory(ec)) continue;
            fs::path p = it->path();
            if (_wcsicmp(p.extension().c_str(), L".lnk") != 0) continue;
            std::wstring target = ResolveLnk(p.wstring());
            if (target.empty()) target = p.wstring();  // 退化：以快捷方式自身作为目标
            AddUniqueItem(items_, {p.stem().wstring(), target});
        }
    }

    // 2) 注册表 App Paths（HKLM + HKCU）——“默认值”存可执行路径，子键名即 exe 名
    const wchar_t* base = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths";
    for (HKEY hive : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        HKEY hk = nullptr;
        if (RegOpenKeyExW(hive, base, 0, KEY_READ, &hk) != ERROR_SUCCESS) continue;
        for (DWORD i = 0;; ++i) {
            wchar_t name[256]{};
            DWORD nlen = 256;
            if (RegEnumKeyW(hk, i, name, nlen) == ERROR_NO_MORE_ITEMS) break;
            if (RegEnumKeyW(hk, i, name, nlen) != ERROR_SUCCESS) continue;
            HKEY sk = nullptr;
            std::wstring sub = std::wstring(base) + L"\\" + name;
            if (RegOpenKeyExW(hive, sub.c_str(), 0, KEY_READ, &sk) != ERROR_SUCCESS) continue;
            wchar_t def[MAX_PATH]{};
            DWORD dlen = sizeof(def);
            DWORD dtype = REG_SZ;
            LONG r = RegQueryValueExW(sk, nullptr, nullptr, &dtype,
                                      reinterpret_cast<LPBYTE>(def), &dlen);
            RegCloseKey(sk);
            if (r == ERROR_SUCCESS && def[0])
                AddUniqueItem(items_, {std::wstring(name), def});
        }
        RegCloseKey(hk);
    }
}

std::vector<AppItem> SearchItems(const std::vector<AppItem>& items,
                                 const std::wstring& q) {
    std::wstring query = ToLower(q);
    std::vector<AppItem> out;
    for (auto& it : items) {
        std::wstring n = ToLower(it.name);
        std::wstring p = ToLower(fs::path(it.path).filename().wstring());
        if (query.empty() || n.find(query) != std::wstring::npos ||
            p.find(query) != std::wstring::npos)
            out.push_back(it);
    }
    std::stable_sort(out.begin(), out.end(), [&](const AppItem& a, const AppItem& b) {
        bool pa = ToLower(a.name).rfind(query, 0) == 0;
        bool pb = ToLower(b.name).rfind(query, 0) == 0;
        if (pa != pb) return pa;   // 前缀命中优先
        std::wstring na = ToLower(a.name), nb = ToLower(b.name);
        if (na != nb) return na < nb;
        return a.path < b.path;
    });
    return out;
}

std::vector<AppItem> SearchFiles(const std::vector<std::wstring>& files,
                                 const std::wstring& q) {
    std::wstring query = ToLower(q);
    std::vector<AppItem> out;
    for (auto& f : files) {
        std::wstring name = fs::path(f).filename().wstring();
        if (query.empty() || ToLower(name).find(query) != std::wstring::npos)
            out.push_back({name, f});
    }
    std::stable_sort(out.begin(), out.end(), [&](const AppItem& a, const AppItem& b) {
        bool pa = ToLower(a.name).rfind(query, 0) == 0;
        bool pb = ToLower(b.name).rfind(query, 0) == 0;
        if (pa != pb) return pa;
        std::wstring na = ToLower(a.name), nb = ToLower(b.name);
        if (na != nb) return na < nb;
        return a.path < b.path;
    });
    return out;
}

}  // namespace lb
