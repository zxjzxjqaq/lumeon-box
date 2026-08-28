#include "ui/card_visual.hpp"
#include <dwrite.h>
#pragma comment(lib, "dwrite")

namespace lb {

bool CreateCardVisual(GraphicsContext& gfx, ComPtr<IDCompositionVisual>& out,
                      float wDip, float hDip, float dpiScale) {
    auto surface = gfx.DrawToSurface(wDip, hDip, dpiScale,
                                     [&](::ID2D1DeviceContext* ctx, float) {
        ctx->Clear(nullptr);  // 全透明底：DWM 玻璃从此透出

        const float inset = 8.f, r = 16.f;
        const D2D1_RECT_F card{inset, inset, wDip - inset, hDip - inset};
        const D2D1_ROUNDED_RECT rr{card, r, r};

        // 1) 卡面：白 7%
        ComPtr<::ID2D1SolidColorBrush> fill;
        ctx->CreateSolidColorBrush(D2D1_COLOR_F{1.f, 1.f, 1.f, 0.07f}, fill.GetAddressOf());
        ctx->FillRoundedRectangle(rr, fill.Get());

        // 2) 内发光描边：白 12%，内缩 1px
        ComPtr<::ID2D1SolidColorBrush> glow;
        ctx->CreateSolidColorBrush(D2D1_COLOR_F{1.f, 1.f, 1.f, 0.12f}, glow.GetAddressOf());
        const D2D1_ROUNDED_RECT inner{
            {card.left + 1, card.top + 1, card.right - 1, card.bottom - 1}, r - 1, r - 1};
        ctx->DrawRoundedRectangle(inner, glow.Get(), 1.f);

        // 3) 极光渐变描边：#22D3EE → #818CF8 → #C084FC（设计 §7.1）
        D2D1_GRADIENT_STOP gs[3] = {
            {0.0f, D2D1_COLOR_F{0.133f, 0.827f, 0.933f, 1.f}},
            {0.5f, D2D1_COLOR_F{0.506f, 0.549f, 0.973f, 1.f}},
            {1.0f, D2D1_COLOR_F{0.753f, 0.518f, 0.988f, 1.f}},
        };
        ComPtr<::ID2D1GradientStopCollection> stops;
        ctx->CreateGradientStopCollection(gs, 3, D2D1_GAMMA_2_2,
                                          D2D1_EXTEND_MODE_CLAMP, stops.GetAddressOf());
        ComPtr<::ID2D1LinearGradientBrush> border;
        ctx->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(card.left, card.top),
                                                D2D1::Point2F(card.right, card.bottom)),
            stops.Get(), border.GetAddressOf());
        ctx->DrawRoundedRectangle(rr, border.Get(), 1.f);

        // 4) 标题（Segoe UI Variable 缺失时 DWrite 自动回退）
        ComPtr<::IDWriteFactory> dwrite;
        if (SUCCEEDED(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED, __uuidof(::IDWriteFactory),
                reinterpret_cast<::IUnknown**>(dwrite.GetAddressOf())))) {
            ComPtr<::IDWriteTextFormat> fmt;
            if (SUCCEEDED(dwrite->CreateTextFormat(
                    L"Segoe UI Variable Display", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20.f, L"zh-cn",
                    fmt.GetAddressOf()))) {
                ctx->DrawText(L"LumenBox · Spike", 16, fmt.Get(),
                              {card.left + 24, card.top + 20, card.right - 24,
                               card.top + 60},
                              glow.Get());
            }
        }
    });
    if (!surface) return false;

    if (FAILED(gfx.device->CreateVisual(out.ReleaseAndGetAddressOf()))) return false;
    out->SetContent(surface.Get());
    gfx.device->Commit();
    return true;
}

}  // namespace lb
