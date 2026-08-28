#include "shell/desktop_layer.hpp"

namespace lb {

static HWND FindWorkerWForDesktop() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return nullptr;
    // 0x052C：让系统在壁纸与图标之间生成/刷新 WorkerW（Wallpaper Engine 同款手法）
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);

    struct Ctx {
        HWND workerAfter = nullptr;
    };
    Ctx ctx;
    EnumWindows(
        +[](HWND hwnd, LPARAM lp) -> BOOL {
            if (FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr)) {
                // 图标层 WorkerW 之后的下一个 WorkerW 才是壁纸层
                auto* c = reinterpret_cast<Ctx*>(lp);
                c->workerAfter = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr);
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.workerAfter;
}

bool AttachToDesktopLayer(HWND hwnd) {
    HWND worker = FindWorkerWForDesktop();
    if (!worker) return false;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~WS_POPUP) | WS_CHILD);
    if (!SetParent(hwnd, worker)) {
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);  // 失败还原样式
        return false;
    }
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
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
