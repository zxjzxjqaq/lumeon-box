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

// file 是否落在 boxDir(<容器根>=%APPDATA%\LumenBox) 之内（含其类目子目录）——这些不参与桌面整理
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

bool Watcher::Start(const std::wstring& dir, HWND notify, UINT msg, bool recursive) {
    if (running_) return false;
    dir_ = dir;
    notify_ = notify;
    msg_ = msg;
    recursive_ = recursive;
    // 打开目录侦听句柄（主线程），Stop 时关闭以解除阻塞读；worker 用其本地副本
    dir_handle_ = CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (dir_handle_ == INVALID_HANDLE_VALUE) return false;
    running_ = true;
    th_ = std::thread(&Watcher::ThreadProc, this);
    return true;
}

void Watcher::Stop() {
    running_ = false;
    HANDLE d = dir_handle_;
    if (d != INVALID_HANDLE_VALUE) CloseHandle(d);  // 阻断中的 ReadDirectoryChangesW 随即失败返回
    dir_handle_ = INVALID_HANDLE_VALUE;
    if (th_.joinable()) th_.join();
}

DWORD WINAPI Watcher::ThreadProc(void* p) {
    static_cast<Watcher*>(p)->ThreadMain();
    return 0;
}

void Watcher::ThreadMain() {
    HANDLE dir = dir_handle_;   // 本地副本：Stop 关闭后，后续读立即失败（无成员竞争）
    if (dir == INVALID_HANDLE_VALUE) {
        running_ = false;
        return;
    }
    const DWORD flags = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                        FILE_NOTIFY_CHANGE_CREATION;
    char buf[64 * 1024];
    while (running_) {
        DWORD bytes = 0;
        if (!ReadDirectoryChangesW(dir, buf, sizeof buf, recursive_ ? TRUE : FALSE, flags, &bytes,
                                   nullptr, nullptr)) {
            if (GetLastError() == ERROR_OPERATION_ABORTED || !running_) break;
            break;  // 其它错误：结束监听（静默降级）
        }
        auto* batch = new std::vector<std::wstring>();
        DWORD off = 0;
        for (;;) {
            auto* fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buf + off);
            // FileName 为 UTF-16、无末尾 NUL
            std::wstring name(fni->FileName, fni->FileNameLength / sizeof(wchar_t));
            batch->push_back(dir_ + L"\\" + name);
            if (!fni->NextEntryOffset) break;
            off += fni->NextEntryOffset;
        }
        if (batch->empty()) {
            delete batch;
            continue;
        }
        if (!PostMessageW(notify_, msg_, 0, reinterpret_cast<LPARAM>(batch)))
            delete batch;  // 目标窗口已销毁：释放，避免泄漏
    }
    running_ = false;
}

}  // namespace lb
