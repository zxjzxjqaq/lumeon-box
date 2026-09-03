#pragma once
// 宽字符/UTF-8 转换与路径小工具（header-only）
#include <windows.h>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace lb {

inline std::string ToUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n,
                        nullptr, nullptr);
    return s;
}

inline std::wstring ToWide(std::string_view u) {
    if (u.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, u.data(), static_cast<int>(u.size()),
                                nullptr, 0);
    std::wstring s(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u.data(), static_cast<int>(u.size()), s.data(), n);
    return s;
}

// 小写扩展名（不带点）；无扩展名返回空串
inline std::wstring LowerExt(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    size_t sep = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (sep != std::wstring::npos && dot < sep + 1))
        return L"";
    std::wstring e = path.substr(dot + 1);
    for (auto& ch : e) ch = static_cast<wchar_t>(std::towlower(ch));
    return e;
}

// 容器文件显示排序：文件夹优先 → 按扩展名(格式) → 同格式按名称（均忽略大小写）。
inline bool DisplayFileLess(const std::wstring& a, const std::wstring& b) {
    std::error_code ec;
    bool da = std::filesystem::is_directory(std::filesystem::path(a), ec);
    bool db = std::filesystem::is_directory(std::filesystem::path(b), ec);
    if (da != db) return da > db;  // 文件夹优先
    std::wstring na = std::filesystem::path(a).filename().wstring();
    std::wstring nb = std::filesystem::path(b).filename().wstring();
    if (da) return _wcsicmp(na.c_str(), nb.c_str()) < 0;  // 文件夹间按名称
    int ce = _wcsicmp(LowerExt(na).c_str(), LowerExt(nb).c_str());
    if (ce != 0) return ce < 0;                            // 按格式(扩展名)
    return _wcsicmp(na.c_str(), nb.c_str()) < 0;           // 同格式按名称
}

}  // namespace lb
