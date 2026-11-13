#include "core/organizer.hpp"
#include <filesystem>
#include <shlobj.h>
#include "core/classifier.hpp"
#include "core/strutil.hpp"

namespace lb {
namespace fs = std::filesystem;

static bool IsHiddenOrSystem(const fs::path& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES &&
           (a & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
}

// 移动文件后通知资源管理器刷新源/目标目录，避免桌面图标残留约 2 秒才消失
static void NotifyShellMove(const std::wstring& from, const std::wstring& to) {
    std::wstring dfrom = fs::path(from).parent_path().wstring();
    std::wstring dto = fs::path(to).parent_path().wstring();
    SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, dfrom.c_str(), nullptr);
    SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, dto.c_str(), nullptr);
}

// 递归复制目录树（供跨卷目录移动用）
static bool CopyTree(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to, ec);
    for (fs::directory_iterator it(from, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const fs::path d2 = to / it->path().filename();
        if (it->is_directory(ec)) {
            if (!CopyTree(it->path(), d2)) return false;
        } else {
            std::error_code ecf;
            fs::copy_file(it->path(), d2, fs::copy_options::overwrite_existing, ecf);
            if (ecf) return false;
        }
    }
    return !ec;
}

bool MovePath(const std::wstring& src, const std::wstring& dst) {
    std::error_code ec;
    if (fs::is_directory(src, ec)) {
        fs::path s(src), d(dst);
        if (s.root_path() == d.root_path())
            return MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_COPY_ALLOWED) != 0;
        // 跨卷目录：递归复制 + 删除源（MoveFileEx 跨卷移动目录不支持）
        if (!CopyTree(s, d)) {
            std::error_code ecc;
            fs::remove_all(d, ecc);  // 复制失败：清理半份副本，源保持完整（不丢数据）
            return false;
        }
        std::error_code ecr;
        fs::remove_all(src, ecr);
        return !ecr;
    }
    return MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_COPY_ALLOWED) != 0;
}

Organizer::Organizer(std::wstring desktopDir, std::wstring containerDir, const RuleSet& rules,
                     Journal& jr)
    : desktop_(std::move(desktopDir)),
      container_(std::move(containerDir)),
      rules_(rules),
      jr_(jr) {}

std::wstring Organizer::BoxDir(Category c) const {
    return container_ + L"\\" + CategoryName(c);
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
    if (!MovePath(src, dst)) return false;
    jr_.LogMove({src, dst});
    NotifyShellMove(src, dst);
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
        if (!MovePath(it->path().wstring(), target)) continue;
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
    if (!fs::exists(path, ec)) return false;
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
    if (!fs::exists(p, ec)) return false;
    // 只接受 <container>\<子目录>\ 内的文件，防止把任意路径当「放回」用
    std::wstring root = container_ + L"\\";  // 带分隔符，避免 LumenBoxFoo 之类前缀误判
    std::wstring parent = p.parent_path().wstring();
    if (parent.size() <= root.size()) return false;  // 容器根下不放文件
    if (_wcsnicmp(parent.c_str(), root.c_str(), root.size()) != 0) return false;
    jr_.BeginBatch();
    std::wstring dst = UniqueTarget((fs::path(desktop_) / p.filename()).wstring());
    if (!MovePath(p.wstring(), dst)) return false;
    jr_.LogMove({p.wstring(), dst});
    NotifyShellMove(p.wstring(), dst);
    return true;
}

std::vector<std::wstring> Organizer::FilesIn(Category c) const {
    std::vector<std::wstring> out;
    std::error_code ec;
    for (fs::directory_iterator it(BoxDir(c), fs::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec)) {
        out.push_back(it->path().wstring());
    }
    return out;
}

std::wstring Organizer::FolderDir(const std::wstring& folder) const {
    return container_ + L"\\" + folder;
}

std::vector<std::wstring> Organizer::FilesInFolder(const std::wstring& folder) const {
    std::vector<std::wstring> out;
    std::error_code ec;
    for (fs::directory_iterator it(FolderDir(folder), fs::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec)) {
        out.push_back(it->path().wstring());
    }
    return out;
}

bool Organizer::MoveToFolder(const std::wstring& path, const std::wstring& folder) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return false;
    std::wstring dir = FolderDir(folder);
    std::wstring parent = fs::path(path).parent_path().wstring();
    if (_wcsicmp(parent.c_str(), dir.c_str()) == 0) return false;  // 已在同盒不产生副本
    fs::create_directories(dir, ec);
    std::wstring dst = UniqueTarget(dir + L"\\" + fs::path(path).filename().wstring());
    if (!MovePath(path, dst)) return false;
    jr_.BeginBatch();  // 拖入也要成批，否则日志无批次头 → 无法撤销
    jr_.LogMove({path, dst});
    NotifyShellMove(path, dst);
    return true;
}

bool Organizer::CopyToFolder(const std::wstring& path, const std::wstring& folder) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return false;
    std::wstring dir = FolderDir(folder);
    fs::create_directories(dir, ec);
    std::wstring dst = UniqueTarget(dir + L"\\" + fs::path(path).filename().wstring());
    if (fs::is_directory(path, ec)) {
        if (!CopyTree(fs::path(path), fs::path(dst))) return false;
    } else {
        fs::copy_file(path, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) return false;
    }
    NotifyShellMove(path, dst);
    return true;  // 复制不写移动日志（撤销不覆盖复制操作）
}

std::wstring Organizer::MakeFolder(const std::wstring& base) {
    std::error_code ec;
    std::wstring cand = base;
    for (int i = 2; fs::exists(FolderDir(cand), ec); ++i)
        cand = base + L"_" + std::to_wstring(i);
    fs::create_directories(FolderDir(cand), ec);
    return cand;
}

bool Organizer::UndoLast() {
    auto batches = jr_.Batches();
    if (batches.empty()) return false;
    const auto& last = batches.back();
    bool allOk = true;
    for (auto it = last.rbegin(); it != last.rend(); ++it) {
        std::error_code ec;
        if (!fs::exists(it->dst, ec)) { allOk = false; continue; }  // 目标已被删/移走 → 无法还原
        // 原位被占 → 放回为唯一名（永不覆盖）
        std::wstring src = fs::exists(it->src, ec) ? UniqueTarget(it->src) : it->src;
        if (!MovePath(it->dst, src)) allOk = false;
    }
    return jr_.RemoveLastBatch() && allOk;
}

}  // namespace lb
