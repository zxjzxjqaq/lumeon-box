#pragma once
#include <vector>
#include "ui/d2d_interop.hpp"

namespace lb {

// F10 触发：3 个光点飞往对面，stagger 20ms。
// 弹簧感用单条三次贝塞尔近似（过冲 ~3% 后回落），动画在 DWM 合成线程执行；
// stagger 用一次性 WM_TIMER 编排（仅交互期间存在）。
class SpringDemo {
public:
    static constexpr UINT kTimerBase = 100;

    bool Init(GraphicsContext& gfx, float wDip, float hDip);
    void Trigger(HWND hwnd);            // 批量挂 3 个 20ms 间隔的一次性定时器
    void OnTimer(HWND hwnd, UINT id);   // 定时器到点 → 对应光点起跳

private:
    struct Dot {
        ComPtr<IDCompositionVisual> visual;
        float x0 = 0;
    };
    std::vector<Dot> dots_;
    GraphicsContext* gfx_ = nullptr;
    bool flipped_ = false;
    float w_ = 0, h_ = 0;
};

}  // namespace lb
