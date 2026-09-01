#include "core/organizer.hpp"
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

Organizer::Organizer(std::wstring desktopDir, const RuleSet& rules, Journal& jr)
    : desktop_(std::move(desktopDir)), rules_(rules), jr_(jr) {}

std::wstring Organizer::BoxDir(Category c) const {
    return desktop_ + L"\\LumenBox\\" + CategoryName(c);
}

std::wstring Organizer::UniqueTarget(const std::wstring& dst) const {
    std::error_code ec;
    if (!fs::exists(dst, ec)) return dst;
    fs::path p(dst);
    std::wstring stem = p.stem().wstring(), ext = p.extension().wstring();
    for (int i = 2;; ++i) {
        std::wstring cand =
            p.parent_path().wstring() + L"\\" + stem + L" (" + std::to_wstring(i) + L")" + ext;
        if (!fs::exists(cand, ec)) return cand;
    }
}

bool Organizer::MoveOne(const std::wstring& src, Category c) {
    std::wstring dir = BoxDir(c);
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::wstring dst = UniqueTarget(dir + L"\\" + fs::path(src).filename().wstring());
    if (!MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_COPY_ALLOWED)) return false;
    jr_.LogMove({src, dst});
    return true;
}

Organizer::Summary Organizer::OrganizeAll() {
    Summary sum;
    std::error_code ec;
    std::vector<MoveRecord> pending;
    for (fs::directory_iterator it(desktop_, fs::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec)) {
        std::wstring name = it->path().filename().wstring();
        if (it->is_directory(ec)) continue;
        if (_wcsicmp(name.c_str(), L"LumenBox") == 0) continue;  // 格子数据目录本身
        if (name.rfind(L"~$", 0) == 0) continue;                 // Office 临时文件
        if (LowerExt(name) == L"lnk") continue;                  // 快捷方式保持原位
        if (IsHiddenOrSystem(it->path())) continue;
        if (rules_.IsNever(name)) continue;                      // 用户白名单
        Category c = ClassifyFile(name, rules_);
        std::wstring dir = BoxDir(c);
        fs::create_directories(dir, ec);
        std::wstring target = UniqueTarget(dir + L"\\" + name);
        if (!MoveFileExW(it->path().c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED)) continue;
        pending.push_back({it->path().wstring(), target});
        sum.byCategory[CategoryName(c)]++;
        sum.moved++;
    }
    if (!pending.empty()) {
        jr_.BeginBatch();
        for (auto& r : pending) jr_.LogMove(r);
    }
    return sum;
}

bool Organizer::MoveToCategory(const std::wstring& path, Category c) {
    if (c == Category::None) return false;
    std::error_code ec;
    if (!fs::exists(path, ec) || fs::is_directory(path, ec)) return false;
    // 已在目标盒目录：视为同盒（拖到自己所在盒），不动，避免生成 “(2)” 副本
    std::wstring dir = BoxDir(c);
    std::wstring parent = fs::path(path).parent_path().wstring();
    if (_wcsicmp(parent.c_str(), dir.c_str()) == 0) return false;
    jr_.BeginBatch();
    return MoveOne(path, c);
}

bool Organizer::MoveBackToDesktop(const std::wstring& path) {
    std::error_code ec;
    fs::path p(path);
    if (!fs::exists(p, ec) || fs::is_directory(p, ec)) return false;
    // 只接受 <desktop>\LumenBox\<类目>\ 内的文件，防止把任意路径当「放回」用
    std::wstring root = (fs::path(desktop_) / L"LumenBox").wstring();
    std::wstring parent = p.parent_path().wstring();
    if (_wcsicmp(parent.substr(0, root.size()).c_str(), root.c_str()) != 0) return false;
    if (parent.size() <= root.size() + 1) return false;  // LumenBox 根下不放文件
    jr_.BeginBatch();
    std::wstring dst = UniqueTarget((fs::path(desktop_) / p.filename()).wstring());
    if (!MoveFileExW(p.c_str(), dst.c_str(), MOVEFILE_COPY_ALLOWED)) return false;
    jr_.LogMove({p.wstring(), dst});
    return true;
}

std::vector<std::wstring> Organizer::FilesIn(Category c) const {
    std::vector<std::wstring> out;
    std::error_code ec;
    for (fs::directory_iterator it(BoxDir(c), fs::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) out.push_back(it->path().wstring());
    }
    return out;
}

bool Organizer::UndoLast() {
    auto batches = jr_.Batches();
    if (batches.empty()) return false;
    const auto& last = batches.back();
    for (auto it = last.rbegin(); it != last.rend(); ++it) {
        std::error_code ec;
        // 原位被占 → 放回为唯一名（永不覆盖）
        std::wstring src = fs::exists(it->src, ec) ? UniqueTarget(it->src) : it->src;
        MoveFileExW(it->dst.c_str(), src.c_str(), MOVEFILE_COPY_ALLOWED);
    }
    return jr_.RemoveLastBatch();
}

}  // namespace lb
