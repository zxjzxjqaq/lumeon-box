#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>
#include "core/appindex.hpp"

namespace lb {

// 快速搜索聚光灯（Ctrl+Space）：顶部毛玻璃条 + 结果列表（应用在前、桌面文件在后）。
// 普通 popup + 圆角 region + GDI+ 绘制，键盘直达（输入/上下/回车/Esc），避免分层窗焦点问题。
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
    void Refresh();
    void Paint();
    void MoveToTop();
    void Launch(int i);

    static constexpr int kW = 560, kHeaderH = 46, kRowH = 36, kMaxRows = 9;
    int kH = 0;
    HWND hwnd_ = nullptr;
    const AppIndex* apps_ = nullptr;
    std::function<std::vector<std::wstring>()> filesFn_;
    std::vector<std::wstring> files_;
    std::vector<AppItem> hits_;
    std::wstring query_;
    int sel_ = 0;
    bool created_ = false;
    bool caret_on_ = false;
};

}  // namespace lb
