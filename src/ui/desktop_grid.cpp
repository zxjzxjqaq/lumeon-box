#include "ui/desktop_grid.hpp"
#include <algorithm>
#include <filesystem>
#include <shellapi.h>
#include <dwrite.h>
#pragma comment(lib, "shell32")
#pragma comment(lib, "dwrite")

namespace lb {
namespace fs = std::filesystem;

static std::wstring Ellipsize(const std::wstring& s, size_t maxChars) {
    if (s.size() <= maxChars) return s;
    return s.substr(0, maxChars - 1) + L"…";
}

bool DesktopGrid::Init(GraphicsContext& gfx, ComPtr<IDCompositionVisual> root, float dpi) {
    gfx_ = &gfx;
    root_ = root;
    dpi_ = dpi;
    hwnd_ = nullptr;

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
        return false;
    dwrite_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.f,
                              L"zh-cn", headerFmt_.GetAddressOf());
    dwrite_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f,
                              L"zh-cn", rowFmt_.GetAddressOf());

    const Category cats[kBoxCount] = {Category::Doc, Category::Image, Category::Video,
                                      Category::Audio, Category::Archive,
                                      Category::Installer, Category::Other};
    for (int i = 0; i < kBoxCount; ++i) {
        Box b;
        b.cat = cats[i];
        if (FAILED(gfx.device->CreateVisual(b.visual.GetAddressOf()))) return false;
        b.visual->SetOffsetX(0.f);
        b.visual->SetOffsetY(0.f);
        root_->AddVisual(b.visual.Get(), TRUE, nullptr);
        boxes_.push_back(std::move(b));
    }
    gfx.device->Commit();
    return true;
}

float DesktopGrid::HeightOf(const Box& b) const {
    if (b.collapsed) return kHeaderH;
    size_t rows = std::min<size_t>(b.files.size(), kMaxRows);
    return kHeaderH + kPad + static_cast<float>(rows) * kRowH + 4.f;
}

void DesktopGrid::Relayout() {
    float x = 0, y = 0, rowMaxH = 0;
    int col = 0;
    const float totalW = kPerRow * kBoxW + (kPerRow - 1) * kGap;
    for (auto& b : boxes_) {
        float h = HeightOf(b);
        b.rect = {x, y, x + kBoxW, y + h};
        b.visual->SetOffsetX(x);
        b.visual->SetOffsetY(y);
        x += kBoxW + kGap;
        rowMaxH = std::max(rowMaxH, h);
        if (++col == kPerRow) {
            x = 0;
            y += rowMaxH + kGap;
            rowMaxH = 0;
            col = 0;
        }
    }
    float extentH = y + (col ? rowMaxH : 0);
    gfx_->device->Commit();
    if (on_extent_) on_extent_(totalW, extentH);
}

void DesktopGrid::RedrawBox(int i) {
    Box& b = boxes_[i];
    float w = kBoxW, h = HeightOf(b);
    auto surface = gfx_->DrawToSurface(w, h, dpi_, [&](::ID2D1DeviceContext* ctx, float) {
        ctx->Clear(nullptr);

        ComPtr<::ID2D1SolidColorBrush> fill;
        ctx->CreateSolidColorBrush(
            D2D1_COLOR_F{1.f, 1.f, 1.f, b.hovered ? 0.09f : 0.05f}, fill.GetAddressOf());
        ComPtr<::ID2D1SolidColorBrush> line;
        ctx->CreateSolidColorBrush(
            D2D1_COLOR_F{1.f, 1.f, 1.f, b.hovered ? 0.20f : 0.11f}, line.GetAddressOf());
        ComPtr<::ID2D1SolidColorBrush> text;
        ctx->CreateSolidColorBrush(D2D1_COLOR_F{1.f, 1.f, 1.f, 0.88f}, text.GetAddressOf());
        ComPtr<::ID2D1SolidColorBrush> dim;
        ctx->CreateSolidColorBrush(D2D1_COLOR_F{1.f, 1.f, 1.f, 0.55f}, dim.GetAddressOf());

        const D2D1_ROUNDED_RECT rr{{0.5f, 0.5f, w - 0.5f, h - 0.5f}, 12.f, 12.f};
        ctx->FillRoundedRectangle(rr, fill.Get());

        // 悬停/常驻描边：悬停走极光渐变
        if (b.hovered) {
            D2D1_GRADIENT_STOP gs[3] = {
                {0.0f, D2D1_COLOR_F{0.133f, 0.827f, 0.933f, 0.9f}},
                {0.5f, D2D1_COLOR_F{0.506f, 0.549f, 0.973f, 0.9f}},
                {1.0f, D2D1_COLOR_F{0.753f, 0.518f, 0.988f, 0.9f}},
            };
            ComPtr<::ID2D1GradientStopCollection> stops;
            ctx->CreateGradientStopCollection(gs, 3, D2D1_GAMMA_2_2,
                                              D2D1_EXTEND_MODE_CLAMP, stops.GetAddressOf());
            ComPtr<::ID2D1LinearGradientBrush> border;
            ctx->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0),
                                                    D2D1::Point2F(w, h)),
                stops.Get(), border.GetAddressOf());
            ctx->DrawRoundedRectangle(rr, border.Get(), 1.2f);
        } else {
            ctx->DrawRoundedRectangle(rr, line.Get(), 1.f);
        }

        // 头栏：类名 + 计数
        std::wstring title = CategoryName(b.cat);
        ctx->DrawText(title.c_str(), (UINT32)title.size(), headerFmt_.Get(),
                      {12.f, 6.f, w - 40.f, kHeaderH}, text.Get());
        std::wstring count = std::to_wstring(b.files.size());
        ctx->DrawText(count.c_str(), (UINT32)count.size(), headerFmt_.Get(),
                      {w - 34.f, 6.f, w - 10.f, kHeaderH}, dim.Get());
        ctx->DrawLine({10.f, kHeaderH}, {w - 10.f, kHeaderH}, line.Get(), 0.8f);

        // 行
        if (!b.collapsed) {
            size_t rows = std::min<size_t>(b.files.size(), kMaxRows);
            for (size_t r = 0; r < rows; ++r) {
                std::wstring name = Ellipsize(fs::path(b.files[r]).filename().wstring(), 22);
                float y = kHeaderH + kPad * 0.4f + static_cast<float>(r) * kRowH;
                ctx->DrawText(name.c_str(), (UINT32)name.size(), rowFmt_.Get(),
                              {14.f, y, w - 12.f, y + kRowH + 3.f}, text.Get());
            }
            if (b.files.size() > kMaxRows) {
                std::wstring more = L"+ " + std::to_wstring(b.files.size() - kMaxRows) + L" 更多";
                float y = kHeaderH + kPad * 0.4f + static_cast<float>(kMaxRows) * kRowH;
                ctx->DrawText(more.c_str(), (UINT32)more.size(), rowFmt_.Get(),
                              {14.f, y, w - 12.f, y + kRowH + 3.f}, dim.Get());
            }
        }
    });
    if (surface) {
        b.visual->SetContent(surface.Get());
        gfx_->device->Commit();
    }
}

void DesktopGrid::Refresh() {
    if (!org_) return;
    for (auto& b : boxes_) {
        b.files = org_->FilesIn(b.cat);
        std::sort(b.files.begin(), b.files.end(), [](const std::wstring& a, const std::wstring& c) {
            return _wcsicmp(fs::path(a).filename().c_str(), fs::path(c).filename().c_str()) < 0;
        });
    }
    Relayout();
    for (int i = 0; i < kBoxCount; ++i) RedrawBox(i);
}

int DesktopGrid::BoxAt(float x, float y) const {
    for (int i = 0; i < kBoxCount; ++i) {
        const auto& r = boxes_[i].rect;
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return i;
    }
    return -1;
}

int DesktopGrid::RowIndexAt(const Box& b, float x, float y) const {
    float relY = y - b.rect.top - kHeaderH;
    if (relY < 0) return -2;  // 头栏
    int idx = static_cast<int>(relY / kRowH);
    size_t shown = std::min<size_t>(b.files.size(), kMaxRows);
    if (idx < 0 || idx >= static_cast<int>(shown)) return -1;
    return idx;
}

void DesktopGrid::OpenFile(const std::wstring& path) const {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void DesktopGrid::OnMouse(HWND hwnd, UINT msg, float x, float y) {
    hwnd_ = hwnd;
    int b = BoxAt(x, y);
    switch (msg) {
    case WM_MOUSEMOVE:
        if (b != hovered_) {
            int old = hovered_;
            hovered_ = b;
            if (old >= 0) RedrawBox(old);
            if (b >= 0) RedrawBox(b);
        }
        break;
    case WM_LBUTTONUP: {
        if (b >= 0 && RowIndexAt(boxes_[b], x, y) == -2) {  // 头栏点击 = 折叠
            boxes_[b].collapsed = !boxes_[b].collapsed;
            Relayout();
            RedrawBox(b);
        }
        break;
    }
    case WM_LBUTTONDBLCLK: {
        if (b >= 0) {
            int row = RowIndexAt(boxes_[b], x, y);
            if (row >= 0) OpenFile(boxes_[b].files[row]);
        }
        break;
    }
    }
}

void DesktopGrid::AnimateIn(HWND hwnd) {
    for (UINT i = 0; i < kBoxCount; ++i)
        SetTimer(hwnd, kTimerBase + i, 30 * i, nullptr);
    hwnd_ = hwnd;
}

void DesktopGrid::OnTimer(HWND hwnd, UINT id) {
    if (id < kTimerBase || id >= kTimerBase + kBoxCount) return;
    KillTimer(hwnd, id);
    Box& b = boxes_[id - kTimerBase];

    constexpr float T = 0.24f, A = 1.6042f, B = -4.2968f, C = 3.6926f;
    ComPtr<IDCompositionAnimation> op, oy;
    if (FAILED(gfx_->device->CreateAnimation(op.GetAddressOf()))) return;
    if (FAILED(gfx_->device->CreateAnimation(oy.GetAddressOf()))) return;
    op->Reset();
    op->AddCubic(0.0, 0.f, 1.f / T, 0.f, 0.f);  // 透明度线性 0→1
    op->End(T, 1.f);
    oy->Reset();
    oy->AddCubic(0.0, b.rect.top + 14.f, -14.f * C / T, 14.f * -B / (T * T),
                 14.f * -A / (T * T * T));  // y: +14 → rect.top，ease-out
    oy->End(T, b.rect.top);
    b.visual->SetOffsetY(oy.Get());
    if (ComPtr<IDCompositionVisual3> v3; SUCCEEDED(b.visual.As(&v3)))
        v3->SetOpacity(op.Get());  // 动画版 SetOpacity 在 Visual3
    gfx_->device->Commit();
}

}  // namespace lb
