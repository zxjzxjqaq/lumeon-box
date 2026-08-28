#include "anim/spring_demo.hpp"

namespace lb {

static ComPtr<IDCompositionSurface> MakeDotSurface(GraphicsContext& gfx) {
    return gfx.DrawToSurface(32.f, 32.f, 1.f, [&](::ID2D1DeviceContext* ctx, float) {
        D2D1_GRADIENT_STOP gs[2] = {
            {0.0f, D2D1_COLOR_F{0.506f, 0.549f, 0.973f, 1.0f}},  // 靛 #818CF8
            {1.0f, D2D1_COLOR_F{0.506f, 0.549f, 0.973f, 0.0f}},
        };
        ComPtr<::ID2D1GradientStopCollection> stops;
        ctx->CreateGradientStopCollection(gs, 2, D2D1_GAMMA_2_2,
                                          D2D1_EXTEND_MODE_CLAMP, stops.GetAddressOf());
        ComPtr<::ID2D1RadialGradientBrush> grad;
        ctx->CreateRadialGradientBrush(
            D2D1::RadialGradientBrushProperties(D2D1::Point2F(16.f, 16.f),
                                                D2D1::Point2F(0.f, 0.f), 16.f, 16.f),
            stops.Get(), grad.GetAddressOf());
        ctx->FillEllipse(D2D1_ELLIPSE{{16.f, 16.f}, 16.f, 16.f}, grad.Get());
    });
}

bool SpringDemo::Init(GraphicsContext& gfx, float wDip, float hDip) {
    gfx_ = &gfx;
    w_ = wDip;
    h_ = hDip;
    auto surface = MakeDotSurface(gfx);
    if (!surface) return false;

    for (int i = 0; i < 3; ++i) {
        Dot d;
        d.x0 = 60.f + i * 90.f;
        if (FAILED(gfx.device->CreateVisual(d.visual.ReleaseAndGetAddressOf())))
            return false;
        d.visual->SetContent(surface.Get());
        d.visual->SetOffsetX(d.x0);
        d.visual->SetOffsetY(h_ * 0.55f);
        gfx.root->AddVisual(d.visual.Get(), TRUE, nullptr);
        dots_.push_back(std::move(d));
    }
    gfx.device->Commit();
    return true;
}

void SpringDemo::Trigger(HWND hwnd) {
    for (UINT i = 0; i < dots_.size(); ++i)
        SetTimer(hwnd, kTimerBase + i, 20 * i, nullptr);  // 20ms stagger，仅交互期间存在
}

void SpringDemo::OnTimer(HWND hwnd, UINT id) {
    if (!gfx_) return;
    if (id < kTimerBase || id >= kTimerBase + dots_.size()) return;
    KillTimer(hwnd, id);

    Dot& d = dots_[id - kTimerBase];
    float x1 = flipped_ ? w_ - 92.f - static_cast<float>(id - kTimerBase) * 90.f
                        : 60.f + static_cast<float>(id - kTimerBase) * 90.f;

    // 弹簧近似：三次多项式过冲 ~3% 后回落（设计 §7.2），DComp 合成线程执行。
    // 归一化解 s(τ)=1.6042τ³−4.2968τ²+3.6926τ 满足：s(0)=0, s(1)=1,
    // s'(0.72)=0, s(0.72)=1.03（峰值 +3% 位于 72% 时长处）。
    constexpr float T = 0.42f;  // 表达级时长（设计 §7.2：expressive 420ms）
    constexpr float A = 1.6042f, B = -4.2968f, C = 3.6926f;
    const float range = x1 - d.x0;
    ComPtr<IDCompositionAnimation> anim;
    if (FAILED(gfx_->device->CreateAnimation(anim.GetAddressOf()))) return;
    anim->Reset();
    anim->AddCubic(0.0, d.x0, range * C / T, range * B / (T * T), range * A / (T * T * T));
    anim->End(T, x1);  // 结束后保持在目标位
    d.visual->SetOffsetX(anim.Get());
    gfx_->device->Commit();
}

}  // namespace lb
