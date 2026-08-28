#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include "ui/d2d_interop.hpp"

namespace lb {

struct SpikeConfig;

// 无边框透明窗：WS_EX_NOREDIRECTIONBITMAP，视觉由 DirectComposition 承载
class GlassWindow {
public:
    // on_key 收到 WM_HOTKEY 的热键 id（非虚拟键）；ESC 在内部直接退出
    bool Create(HINSTANCE hInst, const std::wstring& title, struct SpikeConfig& cfg,
                std::function<void(UINT id)> on_key);

    HWND hwnd() const { return hwnd_; }
    float DpiScale() const { return dpi_ / 96.f; }
    GraphicsContext& gfx() { return gfx_; }
    ComPtr<IDCompositionVisual> const& Root() const { return gfx_.root; }
    void OnTimer(std::function<void(HWND, UINT)> cb) { on_timer_ = std::move(cb); }
    // 鼠标消息（坐标已换算为 DIP）
    void OnMouse(std::function<void(UINT msg, float x, float y)> cb) { on_mouse_ = std::move(cb); }
    // WM_APP 区段消息透传（托盘回调等），返回值作为 WndProc 结果
    void OnApp(std::function<LRESULT(UINT msg, WPARAM wp, LPARAM lp)> cb) { on_app_ = std::move(cb); }
    // WM_NCHITTEST 委托：返回 HTTRANSPARENT 穿透 / HTCLIENT 交互（贴桌模式必须穿透空白区）
    void OnHitTest(std::function<int(float x, float y)> cb) { on_hittest_ = std::move(cb); }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void ApplyBackdrop();   // Win11 Acrylic → Win10 模糊 → 纯卡片底色，三级退化
    void SaveBounds();

    HWND hwnd_ = nullptr;
    struct SpikeConfig* cfg_ = nullptr;
    std::function<void(UINT id)> on_key_;
    std::function<void(HWND, UINT)> on_timer_;
    std::function<void(UINT, float, float)> on_mouse_;
    std::function<LRESULT(UINT, WPARAM, LPARAM)> on_app_;
    std::function<int(float, float)> on_hittest_;
    float dpi_ = 96.f;
    GraphicsContext gfx_;
};

}  // namespace lb
