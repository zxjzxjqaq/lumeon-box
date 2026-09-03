#pragma once
#include <windows.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include "core/rules.hpp"

namespace lb {

// 一条「文件落桌面 → 建议归位」记录
struct FileLanding {
    std::wstring path;   // 宽字符绝对路径
    Category cat;        // 建议归类（永不为 None）
};

// 给定一批候选路径，返回建议归位的落地记录。
// 与 Organizer::OrganizeAll 的跳过语义保持一致：
//   跳过：不存在 / 目录 / 隐藏 / 系统 / ~$ 临时 / LumenBox 目录本身的同名文件 /
//         白名单(IsNever) / .lnk(默认不整理) / boxDir(<容器根>) 之内。
//   归类：ClassifyFile；None 归一为 Other（文件必有归属）。
std::vector<FileLanding> SuggestLandings(const std::vector<std::wstring>& paths,
                                         const RuleSet& rules,
                                         const std::wstring& boxDir);

// 后台监听桌面目录：ReadDirectoryChangesW（只监桌面顶层：创建/改名/目录事件）。
// 每收一批变更，用 PostMessageW 把一个 new std::vector<std::wstring> 发给 notify 窗口
// （消息参数 msg_，LPARAM 指向该 vector；接收方负责 delete）。
// 任务方在 UI 线程做 500ms 防抖合并 + SuggestLandings 过滤。
class Watcher {
public:
    ~Watcher() { Stop(); }

    bool Start(const std::wstring& dir, HWND notify, UINT msg, bool recursive = false);
    void Stop();

private:
    static DWORD WINAPI ThreadProc(void* p);
    void ThreadMain();

    std::wstring dir_;
    HWND notify_ = nullptr;
    UINT msg_ = 0;
    bool recursive_ = false;
    HANDLE dir_handle_ = INVALID_HANDLE_VALUE;   // 主线程打开，Stop 时 CancelIoEx+关闭（解除阻塞读）
    HANDLE cancel_event_ = nullptr;              // Stop 置位以唤醒等待中的线程（覆盖竞态）
    std::thread th_;
    std::atomic<bool> running_{false};
};

}  // namespace lb
