#include "core/watcher.hpp"
#include <windows.h>
#include <filesystem>
#include "core/classifier.hpp"
#include "core/strutil.hpp"

namespace lb {
namespace fs = std::filesystem;

static bool IsHiddenOrSystem(const fs::path& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES &&
           (a & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
}

// file 是否落在 boxDir(<桌面>\LumenBox) 之内（含其类目子目录）——这些不参与桌面整理
static bool InsideBox(const std::wstring& path, const std::wstring& boxDir) {
    if (boxDir.empty()) return false;
    std::wstring parent = fs::path(path).parent_path().wstring();
    if (_wcsicmp(parent.c_str(), boxDir.c_str()) == 0) return true;
    std::wstring root = boxDir + L"\\";
    return parent.size() > root.size() &&
           _wcsicmp(parent.substr(0, root.size()).c_str(), root.c_str()) == 0;
}

std::vector<FileLanding> SuggestLandings(const std::vector<std::wstring>& paths,
                                         const RuleSet& rules,
                                         const std::wstring& boxDir) {
    std::vector<FileLanding> out;
    std::error_code ec;
    for (const auto& p : paths) {
        fs::path f(p);
        std::wstring name = f.filename().wstring();
        if (!fs::exists(f, ec)) continue;          // 已被移动/不存在
        if (fs::is_directory(f, ec)) continue;     // 只处理文件
        if (_wcsicmp(name.c_str(), L"LumenBox") == 0) continue;
        if (name.rfind(L"~$", 0) == 0) continue;   // Office 临时文件
        if (LowerExt(name) == L"lnk") continue;    // .lnk 默认不整理（保持原位）
        if (IsHiddenOrSystem(f)) continue;
        if (rules.IsNever(name)) continue;
        if (InsideBox(p, boxDir)) continue;
        Category c = ClassifyFile(name, rules);
        out.push_back({p, c == Category::None ? Category::Other : c});
    }
    return out;
}

}  // namespace lb
