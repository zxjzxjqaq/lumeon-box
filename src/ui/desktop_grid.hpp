#pragma once
#include <dwrite.h>
#include <functional>
#include <string>
#include <vector>
#include "ui/d2d_interop.hpp"
#include "core/organizer.hpp"

namespace lb {

// 桌面格子层：7 个分类玻璃卡，每格独立 DComp 视觉；支持悬停/折叠/双击打开。
// 布局为每行 4 格的网格，锚定由外部（on_extent 回调）完成。
class DesktopGrid {
public:
    static constexpr int kBoxCount = 7;
    static constexpr int kPerRow = 4;
    static constexpr float kBoxW = 200.f, kGap = 14.f;
    static constexpr float kHeaderH = 34.f, kRowH = 22.f, kPad = 12.f;
    static constexpr int kMaxRows = 12;
    static constexpr UINT kTimerBase = 200;   // AnimateIn 逐格定时器

    bool Init(GraphicsContext& gfx, ComPtr<IDCompositionVisual> root, float dpi);
    void SetOrg(Organizer* org) { org_ = org; }
    void SetOnExtent(std::function<void(float w, float h)> cb) { on_extent_ = std::move(cb); }
    void SetHittest(std::function<int(float x, float y)> cb) { hittest_out_ = std::move(cb); }

    void Refresh();                    // 从 Organizer 拉取内容并重排重绘
    void AnimateIn(HWND hwnd);         // 整理后逐格浮入（30ms stagger）
    void OnTimer(HWND hwnd, UINT id);
    void OnMouse(HWND hwnd, UINT msg, float x, float y);
    int BoxAt(float x, float y) const; // 命中测试（供窗口 HTTEST 委托）

private:
    struct Box {
        Category cat{};
        std::vector<std::wstring> files;   // 绝对路径（已排序）
        bool collapsed = false;
        bool hovered = false;
        D2D1_RECT_F rect{};                // DIP，窗口相对
        ComPtr<IDCompositionVisual> visual;
    };

    float HeightOf(const Box& b) const;
    void Relayout();
    void RedrawBox(int i);
    int RowIndexAt(const Box& b, float x, float y) const;   // -1=无  -2=头栏
    void OpenFile(const std::wstring& path) const;

    GraphicsContext* gfx_ = nullptr;
    ComPtr<IDCompositionVisual> root_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<IDWriteTextFormat> headerFmt_, rowFmt_;
    float dpi_ = 96.f;
    std::vector<Box> boxes_;
    int hovered_ = -1;
    Organizer* org_ = nullptr;
    HWND hwnd_ = nullptr;
    std::function<void(float, float)> on_extent_;
    std::function<int(float, float)> hittest_out_;
};

}  // namespace lb
