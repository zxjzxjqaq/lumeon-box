#pragma once
#include <windows.h>
#include <functional>

namespace lb {

// 月历弹窗（点击日期条打开）：深色圆角卡，与容器同色系。
// 选某天 → on_pick(y,m,d)；◀/▶ 切月；「今天」「取消」；Esc 关闭。
// 模型式小弹窗，关闭后按 day-pick 结果回写日程格日期。
class Calendar {
public:
    void SetOnPick(std::function<void(int y, int m, int d)> cb) { on_pick_ = std::move(cb); }
    void Open(HWND owner, int y, int m, int d, int px, int py);  // px,py=屏幕左上角
    void Close();      // 隐藏（取消不回调）
    bool IsOpen() const { return hwnd_ && IsWindowVisible(hwnd_); }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    HWND hwnd_ = nullptr;
    float scale_ = 1.f;  // 显示器 DPI 缩放（窗口/绘制/命中统一放大，防模糊）
    int viewY_ = 0, viewM_ = 0;   // 显示年月
    int selY_ = 0, selM_ = 0, selD_ = 0;  // 当前选中日期
    std::function<void(int, int, int)> on_pick_;
};

}  // namespace lb
