#pragma once
#include <dwrite.h>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "ui/d2d_interop.hpp"
#include "ui/icons.hpp"
#include "ui/calendar.hpp"
#include "core/config.hpp"
#include "core/organizer.hpp"
#include "ui/shell_ctx_menu.hpp"

namespace lb {

// 桌面格子层：容器玻璃卡，每格独立 DComp 视觉。
// 布局：从左到右排、满 4 个换行、不满的末行右对齐；拖卡片到另一格 = 互换位置，
// 拖出网格 = 归位；空隙拖动整窗；顺序与折叠态持久化。
class DesktopGrid {
public:
    ~DesktopGrid();  // 在 cpp 中定义（需要 ShellCtxMenu 完整类型）
    static constexpr int kMaxBoxes = 24;  // 容器上限
    static constexpr int kPerRow = 4;
    static constexpr float kBoxW = 200.f, kGap = 14.f;
    static constexpr float kHeaderH = 34.f, kRowH = 22.f, kPad = 12.f;
    static constexpr float kDateH = 26.f;  // 日程格日期条高
    static constexpr int kMaxRows = 12;
    static constexpr UINT kTimerBase = 200;    // AnimateIn 逐格定时器
    static constexpr UINT kCollapseTimer = 300;   // 单击格头折叠延时(约250ms)
    static constexpr UINT kCaretTimer = 310;   // 编辑光标闪烁(530ms)
    static constexpr UINT kScrollbarFade = 320; // 停止滚动后开始淡出(1100ms)
    static constexpr UINT kScrollbarTick = 330; // 滚动条淡入淡出动画帧(~30ms)
    static constexpr UINT kCollapseDelay = 250;   // ms
    static constexpr UINT kCaretBlink = 530;   // ms

    bool Init(GraphicsContext& gfx, ComPtr<IDCompositionVisual> root, float dpi);
    void SetOrg(Organizer* org) { org_ = org; }
    void SetOnExtent(std::function<void(float w, float h)> cb) { on_extent_ = std::move(cb); }
    void SetHittest(std::function<int(float x, float y)> cb) { hittest_out_ = std::move(cb); }
    // 拖动结束，宿主应持久化；window_moved=整窗位移（取消自动锚定），false=盒子交换/折叠
    void SetOnMoved(std::function<void(bool window_moved)> cb) { on_moved_ = std::move(cb); }
    // 盒子清单变化（新增/删除/重排/重命名/折叠）→ 宿主持久化 cfg.boxes
    void SetOnBoxesChanged(std::function<void()> cb) { on_boxes_changed_ = std::move(cb); }
    // 头栏右键「清空此格」：宿主把文件放回桌面
    void SetOnClearBox(std::function<void(int boxIdx)> cb) { on_clear_box_ = std::move(cb); }
    // 从配置注入盒子清单（须在首次 Refresh 前调用）
    void SetBoxes(const std::vector<BoxDef>& defs);
    void SetWindowHandle(HWND hwnd) { hwnd_ = hwnd; }
    void SetTasksFile(const std::wstring& p) { tasks_file_ = p; }  // 日程格数据文件
    // 当前盒子清单（供宿主持久化）；折叠态含在 BoxDef.collapsed 里
    std::vector<BoxDef> BoxesOut() const;
    int BoxCount() const { return (int)boxes_.size(); }
    BoxDef BoxDefAt(int i) const;
    // 新建数据容器：folder 由 org_.MakeFolder 生成；追加到末尾，返回其下标
    int AddBox(const std::wstring& name);
    void RemoveBox(int i);   // 移除盒子（文件放回桌面由宿主处理），并清空其文件夹

    void Refresh();                    // 从 Organizer 拉取内容并重排重绘
    void AnimateIn(HWND hwnd);         // 整理后逐格浮入（30ms stagger）
    void OnTimer(HWND hwnd, UINT id);
    void OnMouse(HWND hwnd, UINT msg, float x, float y);
    void OnWheel(int delta, float x, float y);   // 鼠标滚轮：滚动光标所在格的内容
    // 就地重命名格名的键盘入口（玻璃窗 WM_CHAR/WM_KEYDOWN/WM_KILLFOCUS → 这里）
    bool OnKeyboard(UINT msg, UINT wp);
    int BoxAt(float x, float y) const; // 命中测试，客户区坐标（含 bbox 偏移换算）
    std::wstring FolderAt(int i) const { return boxes_[i].folder; }
    bool IsTasksAt(int i) const { return boxes_[i].isTasks; }
    std::wstring NameAt(int i) const { return boxes_[i].name; }
    void BeginRename(int i) { if (i >= 0 && i < (int)boxes_.size()) StartRename(i); }
    void SetDragBox(int i);            // 拖拽悬停高亮（-1 清除）
    // 外壳菜单在弹出期间由玻璃窗转发 WM_INITMENUPOPUP/DRAWITEM/MEASUREITEM/MENUCHAR
    LRESULT OnShellMenuMsg(UINT msg, WPARAM wp, LPARAM lp);

private:
    struct Box {
        std::string id;        // 稳定唯一键（tasks / b1 / b2 ...）
        std::wstring name;     // 显示名（用户可就地重命名）
        std::wstring folder;   // 容器内子目录（data 盒）；tasks 盒为空
        bool isTasks = false;
        std::vector<std::wstring> files;   // 绝对路径（已排序）
        bool collapsed = false;
        bool hovered = false;
        int scroll = 0;                    // 内容滚动偏移（文件/事项超出上限时）
        D2D1_RECT_F rect{};                // DIP，grid 坐标
        ComPtr<IDCompositionVisual> visual;
    };

    // —— 日程格（左上「文档」格）：选日期 → 该日期工作事项 → 打勾完成 ——
    struct TaskRec {
        std::wstring text;
        bool done = false;
    };
    static bool IsTasksBox(const Box& b) { return b.isTasks; }
    enum class TaskHit {
        None,      // 空格体（不可交互）
        Header,    // 头栏（折叠/拖动/改名）
        PrevDate,  // ◀
        NextDate,  // ▶
        DateLabel, // 日期文字（双击回今天）
        Row,       // 事项行文本（双击编辑）
        Check,     // 事项行打勾
        AddRow,    // ＋添加事项
    };
    TaskHit TaskHitTest(const Box& b, float x, float y, int& taskIdx) const;
    float TasksHeight(const Box& b) const;   // 日程格高度（含日期条+事项+添加行）
    void DrawTasksBox(::ID2D1DeviceContext* ctx, int ti, float w, float h) const;  // 日程内容
    std::wstring SelectedKey() const;        // "YYYY-MM-DD"
    void LoadTasks();
    void SaveTasks();
    void StartTaskEdit(int boxIdx, int rowIdx);  // rowIdx<0 → 新增行
    void CommitTaskEdit();
    void CancelTaskEdit();
    void ResetToToday();

    int BoxAtGrid(float x, float y) const;  // 命中测试，grid 坐标
    float HeightOf(const Box& b) const;
    void LayoutGridPositions();  // 从左到右排、满 4 个换行；不满的末行右对齐（空位在左）
    void Relayout();             // 计算 bbox 并更新视觉偏移；定位回调/自定位
    void RedrawBox(int i);
    void DrawScroll(::ID2D1DeviceContext* ctx, float w, float y0, float y1, int shown,
                    int total, int scroll, float alpha) const;
    void ClampScroll(Box& b);                    // 按内容量裁剪滚动偏移
    int RowIndexAt(const Box& b, float x, float y) const;   // -1=无  -2=头栏
    void OpenFile(const std::wstring& path) const;
    void RowMenu(HWND hwnd, int boxIdx, const std::wstring& path);  // 行右键：打开/位置/放回
    void HeaderMenu(HWND hwnd, int boxIdx);                 // 头栏右键：重命名/清空
    std::wstring DisplayName(const Box& b) const;               // 该盒显示名（就地编辑后的 name）
    std::wstring EllipsizeW(const std::wstring& s, float maxW) const;  // 按像素宽度截断+省略号
    void StartRename(int boxIdx);     // 就地编辑格名（双击头栏 / 托盘新建后调用）
    void CommitRename();              // 提交编辑（回车/点击别处/失焦）
    void CancelRename();              // 取消（Esc）
    float HeaderTextWidth(const std::wstring& s) const;     // DWrite 测宽（光标/选中定位）
    float RowTextWidth(const std::wstring& s) const;        // 行字号（日程编辑光标定位）

    GraphicsContext* gfx_ = nullptr;
    ComPtr<IDCompositionVisual> root_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<IDWriteTextFormat> headerFmt_, rowFmt_;
    IconCache icons_;
    float dpi_ = 96.f;
    std::vector<Box> boxes_;
    D2D1_RECT_F bbox_{};         // 全部格子的包围盒（grid 坐标，DIP）
    int hovered_ = -1;
    int drag_box_ = -1;
    // 拖动状态：box_drag_>=0 拖单盒（松手换位）；=-1 且 dragging_ 拖整窗（空隙）
    int box_drag_ = -1;
    int drag_index0_ = -1;       // 拖动起始盒子下标（落点无效时归位）
    bool drag_on_header_ = false;
    bool dragging_ = false;
    bool drag_moved_ = false;
    POINT drag_cursor0_{};
    POINT drag_delta_{};         // 拖动中的光标累计位移（物理像素）
    RECT drag_rect0_{};
    D2D1_RECT_F box_rect0_{};
    // 就地重命名格名（双击头栏）：编辑态 + 缓冲 + 光标/选中
    bool renaming_ = false;
    int rename_box_ = -1;
    std::wstring rename_buf_;
    int rename_caret_ = 0;
    int rename_selA_ = 0, rename_selB_ = 0;   // 选中区间（selA<selB 时有选中）
    bool caret_on_ = true;                    // 编辑光标闪烁
    float sb_alpha_ = 0.f;                    // 滚动条当前透明度(0..1)
    float sb_target_ = 0.f;                   // 滚动条目标透明度(淡入→1 淡出→0)
    int scrollbar_box_ = -1;                  // 正在显示滚动条的格
    int collapse_pending_ = -1;               // 延时折叠中的格（双击则取消并改名）
    // 日程格（左上「文档」格）：当前选中日期 + 任务数据 + 就地编辑态
    int curY_ = 0, curM_ = 0, curD_ = 0;      // 当前选中日期（未初始化=今天）
    std::map<std::wstring, std::vector<TaskRec>> tasks_;  // "YYYY-MM-DD" → 事项
    std::wstring tasks_file_;                 // %APPDATA%\LumenBox\tasks.json
    bool tasks_editing_ = false;
    int task_edit_box_ = -1;
    int task_edit_row_ = -1;                  // <0 → 新增行
    std::wstring task_buf_;
    int task_caret_ = 0;
    int task_selA_ = 0, task_selB_ = 0;
    Calendar calendar_;                        // 点击日期条 → 月历选择
    // 文件级拖动：按下文件行 → 移动超阈值即发起 OLE 拖动（拖出/拖到其他格）
    bool file_press_ = false;
    std::wstring pending_file_;
    Organizer* org_ = nullptr;
    HWND hwnd_ = nullptr;
    std::function<void(float, float)> on_extent_;
    std::function<int(float, float)> hittest_out_;
    std::function<void(bool)> on_moved_;
    std::function<void()> on_boxes_changed_;
    std::function<void(int)> on_clear_box_;
    std::unique_ptr<ShellCtxMenu> shell_menu_;   // 文件外壳上下文菜单（右键时构建）
};

}  // namespace lb
