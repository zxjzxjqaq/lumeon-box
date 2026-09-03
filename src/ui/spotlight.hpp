#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>
#include "core/appindex.hpp"

namespace lb {

// 快速搜索聚光灯（Ctrl+Space）：顶部深色玻璃条 + 结果列表（仅容器内文件）。
// 分层窗口（UpdateLayeredWindow）+ GDI+，静态光标，仅在内容变化时整帧原子重绘——
// 无系统擦除、无定时器，避免闪烁；键盘直达（输入/上下/回车/Esc），高亮随鼠标悬停移动。
class Spotlight {
public:
    bool Create(HINSTANCE hInst);
    void Toggle();
    bool Visible() const { return created_ && IsWindowVisible(hwnd_); }
    void SetApps(const AppIndex* apps) { apps_ = apps; }
    void SetFilesFn(std::function<std::vector<std::wstring>()> fn) { filesFn_ = std::move(fn); }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void OnOpen();
    void OnClose();
    void Refresh();   // 更新 hits_ 并整帧重绘
    void Draw();      // GDI+ → 位图 → UpdateLayeredWindow
    void MoveToTop();
    void Launch(int i);
    void Validate();  // WM_PAINT：仅清除更新区，避免无效区忙循环

    static constexpr int kW = 560, kHeaderH = 46, kRowH = 36, kMaxRows = 9;
    int kH = 0;
    float scale_ = 1.f;  // DPI 缩放（GetDpiForSystem/96），窗口与绘制统一放大
    HWND hwnd_ = nullptr;
    const AppIndex* apps_ = nullptr;
    std::function<std::vector<std::wstring>()> filesFn_;
    std::vector<std::wstring> files_;
    std::vector<AppItem> hits_;
    std::wstring query_;
    int sel_ = 0;
    bool caret_on_ = true;  // 光标闪烁（定时器切换）
    bool created_ = false;
};

}  // namespace lb
