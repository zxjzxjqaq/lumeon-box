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
// 布局恒为整齐的 2×4 槽位网格（8 槽 7 格 + 1 空槽）；拖卡片到另一格 = 互换槽位，
// 拖到空槽 = 移动，拖出网格 = 归位；空隙拖动整窗；顺序与折叠态持久化。
class DesktopGrid {
public:
    static constexpr int kBoxCount = 7;
    static constexpr int kSlotCount = 8;  // 2 行 × 4 列
    static constexpr int kPerRow = 4;
    static constexpr float kBoxW = 200.f, kGap = 14.f;
    static constexpr float kHeaderH = 34.f, kRowH = 22.f, kPad = 12.f;
    static constexpr int kMaxRows = 12;
    static constexpr UINT kTimerBase = 200;   // AnimateIn 逐格定时器

    bool Init(GraphicsContext& gfx, ComPtr<IDCompositionVisual> root, float dpi);
    void SetOrg(Organizer* org) { org_ = org; }
    void SetOnExtent(std::function<void(float w, float h)> cb) { on_extent_ = std::move(cb); }
    void SetHittest(std::function<int(float x, float y)> cb) { hittest_out_ = std::move(cb); }
    // 拖动结束，宿主应持久化；window_moved=整窗位移（取消自动锚定），false=槽位交换/折叠
    void SetOnMoved(std::function<void(bool window_moved)> cb) { on_moved_ = std::move(cb); }
    // 自定义分类规则由宿主写入（现留接口）；格子不再按格式归类
    void SetOnRule(std::function<void(const std::wstring& ext, Category cat)> cb) {
        on_rule_ = std::move(cb);
    }
    // 每格自定义显示名（类目键 → 名称）；须在首次 Refresh 前注入
    void SetBoxNames(const std::map<std::string, std::string>& names);
    // 头栏右键「重命名此格」：宿主需持久化；「清空此格」：宿主把文件放回桌面
    void SetOnBoxRenamed(
        std::function<void(const std::wstring& catKey, const std::wstring& newName)> cb) {
        on_box_renamed_ = std::move(cb);
    }
    void SetOnClearBox(std::function<void(int boxIdx)> cb) { on_clear_box_ = std::move(cb); }
    // 启动时注入槽位顺序与折叠态（须在首次 Refresh 前调用；order 含 8 项，空串=空槽）
    void SetSlots(const std::vector<std::string>& order,
                  const std::vector<std::string>& collapsed);
    void SetWindowHandle(HWND hwnd) { hwnd_ = hwnd; }
    // 当前槽位顺序与折叠态（供宿主持久化）
    std::vector<std::string> SlotOrder() const;
    std::vector<std::string> CollapsedKeys() const;

    void Refresh();                    // 从 Organizer 拉取内容并重排重绘
    void AnimateIn(HWND hwnd);         // 整理后逐格浮入（30ms stagger）
    void OnTimer(HWND hwnd, UINT id);
    void OnMouse(HWND hwnd, UINT msg, float x, float y);
    int BoxAt(float x, float y) const; // 命中测试，客户区坐标（含 bbox 偏移换算）
    Category CategoryAt(int i) const { return boxes_[i].cat; }
    void SetDragBox(int i);            // 拖拽悬停高亮（-1 清除）

private:
    struct Box {
        Category cat{};
        int slot = 0;                      // 槽位 0..7（槽位模型：拖拽换位）
        std::vector<std::wstring> files;   // 绝对路径（已排序）
        bool collapsed = false;
        bool hovered = false;
        D2D1_RECT_F rect{};                // DIP，grid 坐标
        ComPtr<IDCompositionVisual> visual;
    };

    int BoxAtSlot(int slot) const;          // 槽位 → 格子下标（空槽 -1）
    int NearestSlot(float x, float y) const; // 落点最近槽位（过远 -1）
    int BoxAtGrid(float x, float y) const;  // 命中测试，grid 坐标
    float HeightOf(const Box& b) const;
    void LayoutGridPositions();  // 按槽位顺序排布（2 行，行高取该行最高）→ 写入 rect
    void Relayout();             // 计算 bbox 并更新视觉偏移；定位回调/自定位
    void RedrawBox(int i);
    int RowIndexAt(const Box& b, float x, float y) const;   // -1=无  -2=头栏
    void OpenFile(const std::wstring& path) const;
    void RowMenu(HWND hwnd, int boxIdx, const std::wstring& path);  // 行右键：打开/位置/放回
    void HeaderMenu(HWND hwnd, int boxIdx);                 // 头栏右键：重命名/清空
    std::wstring DisplayName(Category c) const;             // 自定义名或默认类目名

    GraphicsContext* gfx_ = nullptr;
    ComPtr<IDCompositionVisual> root_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<IDWriteTextFormat> headerFmt_, rowFmt_;
    IconCache icons_;
    float dpi_ = 96.f;
    std::vector<Box> boxes_;
    std::map<std::wstring, std::wstring> boxNames_;   // 类目键(宽) → 显示名
    D2D1_RECT_F bbox_{};         // 全部格子的包围盒（grid 坐标，DIP）
    int hovered_ = -1;
    int drag_box_ = -1;
    // 拖动状态：box_drag_>=0 拖单格（松手换槽位）；=-1 且 dragging_ 拖整窗（空隙）
    int box_drag_ = -1;
    int drag_slot0_ = -1;        // 拖动起始槽位（落点无效时归位）
    bool drag_on_header_ = false;
    bool dragging_ = false;
    bool drag_moved_ = false;
    POINT drag_cursor0_{};
    POINT drag_delta_{};         // 拖动中的光标累计位移（物理像素）
    RECT drag_rect0_{};
    D2D1_RECT_F box_rect0_{};
    Organizer* org_ = nullptr;
    HWND hwnd_ = nullptr;
    std::function<void(float, float)> on_extent_;
    std::function<int(float, float)> hittest_out_;
    std::function<void(bool)> on_moved_;
    std::function<void(const std::wstring&, Category)> on_rule_;
    std::function<void(const std::wstring&, const std::wstring&)> on_box_renamed_;
    std::function<void(int)> on_clear_box_;
};

}  // namespace lb
