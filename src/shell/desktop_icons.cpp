#include "shell/desktop_icons.hpp"
#include <commctrl.h>

namespace lb {

namespace {
HWND g_notify = nullptr;
UINT g_msg = 0;
HHOOK g_hook = nullptr;
HANDLE g_thread = nullptr;
DWORD g_tid = 0;
HANDLE g_ready = nullptr;

// 定位桌面图标视图（SHELLDLL_DefView）。系统更新可能把它挂在 Progman 或某个 WorkerW 下。
HWND FindDesktopIcons() {
    HWND prog = FindWindowW(L"Progman", nullptr);
    if (!prog) return nullptr;
    HWND def = FindWindowExW(prog, nullptr, L"SHELLDLL_DefView", nullptr);
    if (def) return def;
    for (HWND w = GetWindow(prog, GW_HWNDNEXT); w; w = GetWindow(w, GW_HWNDNEXT)) {
        HWND child = FindWindowExW(w, nullptr, L"SHELLDLL_DefView", nullptr);
        if (child) return child;
    }
    return nullptr;
}

// 点是否落在桌面图标上（图标列表的 HitTest）
bool PointOverIcon(const POINT& pt, HWND list) {
    if (!list) return false;
    POINT c = pt;
    ScreenToClient(list, &c);
    LVHITTESTINFO hti{};
    hti.pt = c;
    return ListView_HitTest(list, &hti) != -1;
}

// 双键命中的是「桌面空白」（而非图标、非其它窗口）
bool IsOverDesktopBlank(const POINT& pt) {
    HWND def = FindDesktopIcons();
    if (!def) return false;
    HWND w = WindowFromPoint(pt);
    bool onDesktop = false;
    for (HWND a = w; a; a = GetAncestor(a, GA_PARENT)) {
        if (a == def) {
            onDesktop = true;
            break;
        }
    }
    if (!onDesktop) return false;  // 点在其它窗口（我们格子/文件管理器等）
    HWND list = FindWindowExW(def, nullptr, L"SysListView32", nullptr);
    if (PointOverIcon(pt, list)) return false;  // 图标上：交给 Explorer 打开
    return true;
}

LRESULT CALLBACK HookProc(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0 && wp == WM_LBUTTONDBLCLK) {
        auto* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lp);
        if (IsOverDesktopBlank(ms->pt)) PostMessageW(g_notify, g_msg, 0, 0);
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

DWORD WINAPI HookThread(LPVOID) {
    HINSTANCE hmod = GetModuleHandleW(nullptr);
    g_hook = SetWindowsHookExW(WH_MOUSE_LL, HookProc, hmod, 0);
    if (g_ready) SetEvent(g_ready);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }
    return 0;
}
}  // namespace

bool ToggleDesktopIcons() {
    HWND def = FindDesktopIcons();
    if (!def) return false;
    bool vis = IsWindowVisible(def);
    ShowWindow(def, vis ? SW_HIDE : SW_SHOW);
    return true;
}

bool InstallHideHook(HWND notify, UINT msg) {
    if (g_hook || g_thread) return true;
    g_notify = notify;
    g_msg = msg;
    g_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, HookThread, nullptr, 0, &g_tid);
    if (!g_thread) {
        if (g_ready) CloseHandle(g_ready);
        g_ready = nullptr;
        return false;
    }
    if (g_ready) WaitForSingleObject(g_ready, 1000);
    if (g_ready) {
        CloseHandle(g_ready);
        g_ready = nullptr;
    }
    return g_hook != nullptr;
}

void RemoveHideHook() {
    if (g_tid) PostThreadMessageW(g_tid, WM_QUIT, 0, 0);
    if (g_thread) {
        WaitForSingleObject(g_thread, 1000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    g_notify = nullptr;
    g_msg = 0;
}

bool HideHookInstalled() { return g_hook != nullptr; }

}  // namespace lb
