#include "ui/glass_window.hpp"
#include "core/config.hpp"
#include "core/stage_log.hpp"
#include <dwmapi.h>
#include <windowsx.h>
#pragma comment(lib, "dwmapi")

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_TRANSIENTWINDOW
#define DWMSBT_TRANSIENTWINDOW 3  // Acrylic
#endif

namespace lb {

static bool IsWin11OrGreater() {
    // RtlGetVersion 不受兼容性清单影响，返回真实版本
    OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto fn = reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    return fn && fn(&vi) == 0 && vi.dwBuildNumber >= 22000;
}

bool GlassWindow::Create(HINSTANCE hInst, const std::wstring& title,
                         SpikeConfig& cfg, std::function<void(UINT)> on_key) {
    cfg_ = &cfg;
    on_key_ = std::move(on_key);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpfnWndProc = &GlassWindow::WndProc;
    wc.style = CS_DBLCLKS;  // 生成 WM_LBUTTONDBLCLK：支持双击打开格子内文件
    wc.hInstance = hInst;
    wc.lpszClassName = L"LumenBoxGlass";
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW,
                            wc.lpszClassName, title.c_str(), WS_POPUP,
                            cfg.x, cfg.y, cfg.w, cfg.h, nullptr, nullptr, hInst, this);
    if (!hwnd_) return false;
    dpi_ = static_cast<float>(GetDpiForWindow(hwnd_));
    StageTrace("c:window-hwnd");

    if (!gfx_.InitForWindow(hwnd_)) return false;
    StageTrace("c:dcomp-target");

    // 去掉窗口级系统玻璃：只显示卡片自身的玻璃视觉，不再有包裹 7 格的外围大玻璃矩形。
    // （原 DwmExtendFrameIntoClientArea(MARGINS{-1}) + ApplyBackdrop() 会使整窗为玻璃矩形）
    ShowWindow(hwnd_, SW_SHOW);
    StageTrace("c:show");
    return true;
}

void ApplyGlassBackdrop(HWND hwnd) {
    if (IsWin11OrGreater()) {
        INT32 backdrop = DWMSBT_TRANSIENTWINDOW;
        if (SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                                            &backdrop, sizeof(backdrop))))
            return;
    }
    // Win10 退化：SetWindowCompositionAttribute（未公开 API，动态加载）
    struct ACCENT_POLICY { INT32 State; INT32 Flags; UINT32 GradientColor; INT32 AnimationId; };
    struct WCA_DATA { INT32 Attrib; PVOID pvData; SIZE_T cbData; };
    using SetWCAFN = BOOL(WINAPI*)(HWND, const WCA_DATA*);
    auto fn = reinterpret_cast<SetWCAFN>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
    if (fn) {
        constexpr INT32 ACCENT_ENABLE_ACRYLICBLURBEHIND = 4;
        ACCENT_POLICY p{ACCENT_ENABLE_ACRYLICBLURBEHIND, 2, 0xB0101010, 0};  // ABGR
        WCA_DATA d{19 /*WCA_ACCENT_POLICY*/, &p, sizeof(p)};
        fn(hwnd, &d);
    }
    // 三级退化：不做任何系统模糊，卡片自绘底色保证可读
}

void GlassWindow::ApplyBackdrop() {
    ApplyGlassBackdrop(hwnd_);
}

LRESULT GlassWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<GlassWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(
                reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_NCHITTEST:
        if (self && self->on_hittest_) {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            return self->on_hittest_(pt.x / self->dpi_ * 96.f, pt.y / self->dpi_ * 96.f);
        }
        return HTCAPTION;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONUP:
        if (self && self->on_mouse_) {
            self->on_mouse_(msg, static_cast<short>(LOWORD(lp)) / self->dpi_ * 96.f,
                            static_cast<short>(HIWORD(lp)) / self->dpi_ * 96.f);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow(hwnd);
        return 0;
    case WM_HOTKEY:
        if (self && self->on_key_) self->on_key_(static_cast<UINT>(wp));
        return 0;
    case WM_TIMER:
        if (self && self->on_timer_) self->on_timer_(hwnd, static_cast<UINT>(wp));
        return 0;
    case WM_DESTROY:
        if (self) self->SaveBounds();
        PostQuitMessage(0);
        return 0;
    }
    if (msg >= WM_APP && msg <= 0xBFFF && self && self->on_app_)
        return self->on_app_(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void GlassWindow::SaveBounds() {
    RECT r{};
    GetWindowRect(hwnd_, &r);
    cfg_->x = r.left;
    cfg_->y = r.top;
    cfg_->w = r.right - r.left;
    cfg_->h = r.bottom - r.top;
}

}  // namespace lb
