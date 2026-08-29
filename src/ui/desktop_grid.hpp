#pragma once
#include <dwrite.h>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "ui/d2d_interop.hpp"
#include "ui/icons.hpp"
#include "core/config.hpp"
#include "core/organizer.hpp"

namespace lb {

// 桌面格子层：7 个分类玻璃卡，每格独立 DComp 视觉。
// 两种布局：网格模式（4+3 自动排布，anchored 时整窗锚定右下）与
// 自由模式（每格独立位置，头栏拖动单格移动，空隙拖动整体移动，位置持久化）。
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
    // 拖动结束（单格或整窗），宿主应持久化
    void SetOnMoved(std::function<void()> cb) { on_moved_ = std::move(cb); }
    // 启动时注入自由布局状态（须在首次 Refresh 前调用）
    void SetLayout(bool free, const std::map<std::string, BoxState>& states);
    // 自由模式的窗口定位基准与宿主窗口句柄（须在首次 Refresh 前调用）
    void SetOriginPx(int x, int y, HWND hwnd) {
        origin_px_ = {x, y};
        hwnd_ = hwnd;
    }
    // 当前布局状态（供宿主持久化）
    struct SavedLayout {
        bool free = false;
        std::map<std::string, BoxState> boxes;
    };
    SavedLayout Layout() const;

    void Refresh();                    // 从 Organizer 拉取内容并重排重绘
    void AnimateIn(HWND hwnd);         // 整理后逐格浮入（30ms stagger）
    void OnTimer(HWND hwnd, UINT id);
    void OnMouse(HWND hwnd, UINT msg, float x, float y);
    int BoxAt(float x, float y) const; // 命中测试（供窗口 HTTEST 委托）
    Category CategoryAt(int i) const { return boxes_[i].cat; }
    void SetDragBox(int i);            // 拖拽悬停高亮（-1 清除）

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
    void LayoutGridPositions();  // 4+3 网格排布 → 写入各 box rect
    void Relayout();             // 计算 bbox：网格模式通知宿主定位，自由模式自定位
    void RedrawBox(int i);
    int RowIndexAt(const Box& b, float x, float y) const;   // -1=无  -2=头栏
    void OpenFile(const std::wstring& path) const;
    void RowMenu(HWND hwnd, const std::wstring& path);      // 行右键：打开/位置/放回

    GraphicsContext* gfx_ = nullptr;
    ComPtr<IDCompositionVisual> root_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<IDWriteTextFormat> headerFmt_, rowFmt_;
    IconCache icons_;
    float dpi_ = 96.f;
    std::vector<Box> boxes_;
    D2D1_RECT_F bbox_{};         // 全部格子的包围盒（grid 坐标，DIP）
    bool free_ = false;          // 自由布局模式
    POINT origin_px_{};          // 自由模式窗口定位基准（物理像素）
    int hovered_ = -1;
    int drag_box_ = -1;
    // 拖动状态：box_drag_>=0 拖单格；=-1 且 dragging_ 拖整窗（空隙）
    int box_drag_ = -1;
    bool dragging_ = false;
    bool drag_moved_ = false;
    POINT drag_cursor0_{};
    RECT drag_rect0_{};
    D2D1_RECT_F box_rect0_{};
    Organizer* org_ = nullptr;
    HWND hwnd_ = nullptr;
    std::function<void(float, float)> on_extent_;
    std::function<int(float, float)> hittest_out_;
    std::function<void()> on_moved_;
};

}  // namespace lb
