#pragma once
#include <windows.h>
#include <functional>
#include <vector>
#include "core/watcher.hpp"  // FileLanding

namespace lb {

// 归位提示气泡：玻璃卡 + 「归位/忽略」按钮，右上角常驻可交互。
// 复用分层窗口技术（GDI+ → UpdateLayeredWindow）。
class HintToast {
public:
    // 显示提示。接受时调 on_accept(landings 副本)，忽略/超时/关闭调 on_dismiss。
    // 已有提示则替换之。
    void Show(std::vector<FileLanding> landings,
              std::function<void(std::vector<FileLanding>)> on_accept,
              std::function<void()> on_dismiss);
    void Dismiss();  // 立即隐藏并触发 on_dismiss
    bool Visible() const { return created_ && IsWindowVisible(hwnd_); }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    bool Create();
    void Draw();
    void Layout();
    std::wstring BodyText() const;

    static constexpr int kW = 384;
    int kH = 104;   // 依内容略调
    HWND hwnd_ = nullptr;
    std::vector<FileLanding> landings_;
    std::function<void(std::vector<FileLanding>)> on_accept_;
    std::function<void()> on_dismiss_;
    RECT accept_{}, dismiss_{};
    bool accept_hot_ = false, dismiss_hot_ = false;
    bool created_ = false;
};

}  // namespace lb
