#pragma once
#include <windows.h>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "core/clipboard_store.hpp"

namespace lb {

// 剪贴板历史面板（Ctrl+B）：毛玻璃卡片列表 + 快捷复制按钮。
// 工程模式与 ui/spotlight 完全一致：分层窗口（UpdateLayeredWindow）+ GDI+，
// 内容变化才整帧原子重绘——无系统擦除、无空闲定时器，避免闪烁。
// 新增：滚轮/PGUP·PGDN 滚动、类型过滤 Tab、置顶/删除/清空、缩略图会话内缓存。
class ClipPanel {
public:
    void SetStore(ClipStore* s) { store_ = s; }
    // 复制回写由宿主注入（CopyClipToClipboard + store.LoadPayload 的包装）
    void SetCopyFn(std::function<bool(const ClipEntry&)> fn) { copyFn_ = std::move(fn); }
    // 暂停态读写由宿主注入（同步 config 持久化）
    void SetPausedApi(std::function<bool()> get, std::function<void(bool)> set) {
        pausedGet_ = std::move(get);
        pausedSet_ = std::move(set);
    }

    void Toggle();
    bool Visible() const { return created_ && IsWindowVisible(hwnd_); }
    // 有新条目落库（WM_APP+6）→ 打开时实时刷新
    void OnStoreChanged();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void OnOpen();
    void OnClose();
    void MoveToTop();  // 呼出定位：常规 1/3 锚点、高面板居中，夹进工作区
    void ClampIntoWork();  // 打开时窗口增高：夹回工作区避免遮挡
    void Refresh();   // 重算过滤行、夹取选择/滚动、整帧重绘
    void Draw();
    void FitHeight(); // 依可见行数调整窗口高度
    void ClearThumbs();
    void* GetThumb(const ClipEntry& e);  // GDI+ Bitmap*（会话内缓存）
    bool CopyRow(int row);
    std::vector<int> VisibleRows() const;  // 过滤后的 items_ 下标
    int VisibleCount() const;              // 当前窗口高度下可见行数
    std::vector<std::wstring> DropPaths(const ClipEntry& e) const;  // HDROP 载荷路径（缓存）
    void* GetFileThumb(const std::wstring& path);  // 原图片文件直接出缩略图（缓存）
    // 展示归类：全部路径都是图片文件的 files 条目按"图片"呈现（标签/筛选/悬停一致）
    std::string EffType(const ClipEntry& e) const;

    // 悬停放大预览：图片条目出原图（等比放大到 460×560 内），文本/文件条目出全文/
    // 路径列表。悬停 400ms 后弹出在行旁，移开即收。
    static LRESULT CALLBACK PreviewWndProc(HWND, UINT, WPARAM, LPARAM);
    void EnsurePreview();
    void ShowPreview(int row);
    void HidePreview();
    void DrawPreview();
    void UpdateHover(int hrow);

    // 复制提示（Toast）：复制成功后光标旁显示"已复制"，约 1.1s 自动消失
    static LRESULT CALLBACK ToastWndProc(HWND, UINT, WPARAM, LPARAM);
    void EnsureToast();
    void ShowToast(const std::wstring& text);
    void HideToast();
    void DrawToast();

    // 命中测试（逻辑坐标）；返回行为约定见 cpp
    int HitHeader(int x, int y) const;
    int HitRow(int x, int y) const;

    static constexpr int kW = 560, kHeaderH = 48, kRowH = 68;
    float scale_ = 1.f;
    HWND hwnd_ = nullptr;
    bool created_ = false;
    ClipStore* store_ = nullptr;
    std::function<bool(const ClipEntry&)> copyFn_;
    std::function<bool()> pausedGet_;
    std::function<void(bool)> pausedSet_;

    std::vector<ClipEntry> items_;   // store 快照
    std::vector<int> rows_;          // 过滤后的 items_ 下标（新→旧）
    int filter_ = 0;                 // 0=全部 1=文本 2=图片 3=文件
    int sel_ = 0;
    int scroll_ = 0;                 // 首个可见行下标
    bool clearArm_ = false;          // 清空二次确认臂
    bool closeHot_ = false;          // 关闭按钮悬停态
    int winH_ = 0;                   // 当前逻辑高度
    std::map<std::string, void*> thumbs_;  // id / "f:"+path → Gdiplus::Bitmap*
    mutable std::map<std::string, std::vector<std::wstring>> pathCache_;  // id → HDROP 路径

    // 悬停放大预览窗
    HWND pwnd_ = nullptr;
    int prow_ = -1;      // 预览对应的行
    int hoverRow_ = -1;  // 当前悬停行（rows_ 下标）
    bool trackMouse_ = false;
    int pmode_ = 0;      // 0=无 1=图片 2=文本 3=多图网格
    int pw_ = 0, ph_ = 0, pdw_ = 0, pdh_ = 0;  // 预览窗 / 内容逻辑尺寸
    void* pimg_ = nullptr;  // 当前放大图 Gdiplus::Bitmap*（独占，隐藏即释放）
    std::wstring ptext_;    // 文本全文 / 路径列表
    // 多图网格预览（pmode_==3）：每格一图 + 底部文件名
    std::vector<void*> pimgs_;
    std::vector<std::wstring> pnames_;
    int pcols_ = 2;

    // 复制提示窗
    HWND twnd_ = nullptr;
    int toastW_ = 0, toastH_ = 0;
    std::wstring toastText_;

    // 滚动条：滚动时显示，停止约 0.8s 后淡出
    void ScrollPulse();
    float scrollAlpha_ = 0.f;  // 当前不透明度 0..1
    int scrollIdleMs_ = 0;     // 距上次滚动的毫秒数
    bool scrollFading_ = false;
};

}  // namespace lb
