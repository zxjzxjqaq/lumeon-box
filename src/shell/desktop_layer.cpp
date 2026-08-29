#include "shell/desktop_layer.hpp"

namespace lb {

// 图标层宿主：包含 SHELLDLL_DefView 的 WorkerW（0x052C 后）或 Progman 本身。
// 必须挂在它上面而不是壁纸层——OLE 拖放命中走最顶层窗口，挂图标层之下
// 收不到任何拖放（S4 实测确认）；空白区靠 WM_NCHITTEST 的 HTTRANSPARENT
// 保持图标可点，点击语义与「图标层之下」等价。
static HWND FindIconLayerHost() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return nullptr;
    // 0x052C：统一窗口结构（Wallpaper Engine 同款手法），图标层移入独立 WorkerW
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);

    struct Ctx {
        HWND host = nullptr;
    };
    Ctx ctx;
    EnumWindows(
        +[](HWND hwnd, LPARAM lp) -> BOOL {
            if (FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr)) {
                reinterpret_cast<Ctx*>(lp)->host = hwnd;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.host ? ctx.host : progman;
}

bool AttachToDesktopLayer(HWND hwnd) {
    HWND host = FindIconLayerHost();
    if (!host) return false;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~WS_POPUP) | WS_CHILD);
    if (!SetParent(hwnd, host)) {
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);  // 失败还原样式
        return false;
    }
    // 顶层子窗口：盖过图标层，拖放才能命中；空白区穿透见 glass_window 的 hit-test 委托
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    return true;
}

void UseBottomMode(HWND hwnd) {
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~WS_CHILD) | WS_POPUP);
    SetParent(hwnd, nullptr);
    SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
}

bool RestoreIndependent(HWND hwnd) {
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~WS_CHILD) | WS_POPUP);
    if (!SetParent(hwnd, nullptr)) return false;
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
    return true;
}

}  // namespace lb
