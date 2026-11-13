#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include "ui/d2d_interop.hpp"

namespace lb {

// 深色玻璃背景：Win11 走 DWM Acrylic，Win10 走 SetWindowCompositionAttribute
// 模糊，失败则无系统模糊（卡片自绘底色保底）。供玻璃窗族复用。
void ApplyGlassBackdrop(HWND hwnd);

struct SpikeConfig;

// 无边框透明窗：WS_EX_NOREDIRECTIONBITMAP，视觉由 DirectComposition 承载
class GlassWindow {
public:
    // on_key 收到 WM_HOTKEY 的热键 id（非虚拟键）；ESC 在内部直接退出
    // 可重复调用：主窗口被外部销毁后重建（Explorer 重启恢复）
    bool Create(HINSTANCE hInst, const std::wstring& title, struct SpikeConfig& cfg,
                std::function<void(UINT id)> on_key);

    HWND hwnd() const { return hwnd_; }
    float DpiScale() const { return dpi_ / 96.f; }
    GraphicsContext& gfx() { return gfx_; }
    ComPtr<IDCompositionVisual> const& Root() const { return gfx_.root; }
    void OnTimer(std::function<void(HWND, UINT)> cb) { on_timer_ = std::move(cb); }
    // 鼠标消息（坐标已换算为 DIP）
    void OnMouse(std::function<void(UINT msg, float x, float y)> cb) { on_mouse_ = std::move(cb); }
    // 滚轮（delta 为 120 的倍数；坐标已换算为 DIP）
    void OnWheel(std::function<void(int delta, float x, float y)> cb) { on_wheel_ = std::move(cb); }
    // WM_APP 区段消息透传（托盘回调等），返回值作为 WndProc 结果
    void OnApp(std::function<LRESULT(UINT msg, WPARAM wp, LPARAM lp)> cb) { on_app_ = std::move(cb); }
    // 外壳上下文菜单消息转发（WM_INITMENUPOPUP/WM_DRAWITEM/WM_MEASUREITEM/WM_MENUCHAR）
    void OnMenu(std::function<LRESULT(UINT msg, WPARAM wp, LPARAM lp)> cb) { on_menu_ = std::move(cb); }
    // 键盘输入（WM_CHAR/WM_KEYDOWN/WM_KILLFOCUS）→ 就地重命名等；返回 true 表示已处理
    void OnKeyboard(std::function<bool(UINT msg, UINT wp)> cb) { on_keyboard_ = std::move(cb); }
    // WM_NCHITTEST 委托：返回 HTTRANSPARENT 穿透 / HTCLIENT 交互（贴桌模式必须穿透空白区）
    void OnHitTest(std::function<int(float x, float y)> cb) { on_hittest_ = std::move(cb); }
    // DPI 变更（拖到不同缩放显示器）：回调缩放因子 scale=dpi/96，宿主据此重缩放网格
    void OnDpiChanged(std::function<void(float scale)> cb) { on_dpi_ = std::move(cb); }

    // 主窗口被外部销毁（父窗口 WorkerW 随 Explorer 重启级联销毁）时回调，
    // 用于重建窗口而非退出进程；返回 true 表示已接管（不退出消息循环）。
    void OnDestroyed(std::function<bool()> cb) { on_destroyed_ = std::move(cb); }
    // 标记「用户主动退出」：此时 WM_DESTROY 才真正退出消息循环
    void SetQuitOnDestroy(bool q) { quit_on_destroy_ = q; }
    // 重建后刷新句柄（窗口被销毁后由宿主重新 Create）
    void SetHwnd(HWND h) { hwnd_ = h; }

    HINSTANCE Instance() const { return hinst_; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void ApplyBackdrop();   // Win11 Acrylic → Win10 模糊 → 纯卡片底色，三级退化
    void SaveBounds();

    HWND hwnd_ = nullptr;
    HINSTANCE hinst_ = nullptr;
    bool quit_on_destroy_ = false;
    std::function<bool()> on_destroyed_;
    struct SpikeConfig* cfg_ = nullptr;
    std::function<void(UINT id)> on_key_;
    std::function<void(HWND, UINT)> on_timer_;
    std::function<void(UINT, float, float)> on_mouse_;
    std::function<void(int, float, float)> on_wheel_;
    std::function<LRESULT(UINT, WPARAM, LPARAM)> on_app_;
    std::function<LRESULT(UINT, WPARAM, LPARAM)> on_menu_;
    std::function<bool(UINT, UINT)> on_keyboard_;
    std::function<int(float, float)> on_hittest_;
    std::function<void(float)> on_dpi_;
    float dpi_ = 96.f;
    GraphicsContext gfx_;
};

}  // namespace lb
