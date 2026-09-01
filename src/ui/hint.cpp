#include "ui/hint.hpp"
#include <windowsx.h>
#include <filesystem>
#include <objidl.h>
#include <gdiplus.h>
#include "core/rules.hpp"  // CategoryName

#pragma comment(lib, "gdiplus")

namespace lb {

namespace {
constexpr int kPad = 16;
constexpr int kChipH = 30;
constexpr int kChipW = 72;
constexpr int kChipGap = 10;

ULONG_PTR g_token = 0;
void EnsureGdiplus() {
    if (!g_token) {
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&g_token, &si, nullptr);
    }
}

Gdiplus::Color Color(int a, int r, int g, int b) {
    return Gdiplus::Color(static_cast<BYTE>(a), static_cast<BYTE>(r), static_cast<BYTE>(g),
                          static_cast<BYTE>(b));
}
}  // namespace

std::wstring HintToast::BodyText() const {
    if (landings_.empty()) return L"";
    if (landings_.size() == 1) {
        std::wstring name = std::filesystem::path(landings_[0].path).filename().wstring();
        return name + L"  →  归入「" + CategoryName(landings_[0].cat) + L"」?";
    }
    return L"共 " + std::to_wstring(landings_.size()) + L" 个桌面文件，分别归入对应格子?";
}

void HintToast::Layout() {
    int bottom = kH - kPad;
    dismiss_.left = kW - kPad - kChipW;
    dismiss_.top = bottom - kChipH;
    dismiss_.right = kW - kPad;
    dismiss_.bottom = bottom;
    accept_.left = dismiss_.left - kChipGap - kChipW;
    accept_.top = dismiss_.top;
    accept_.right = dismiss_.left - kChipGap;
    accept_.bottom = dismiss_.bottom;
}

void HintToast::Show(std::vector<FileLanding> landings,
                     std::function<void(std::vector<FileLanding>)> on_accept,
                     std::function<void()> on_dismiss) {
    landings_ = std::move(landings);
    on_accept_ = std::move(on_accept);
    on_dismiss_ = std::move(on_dismiss);
    // 高度 = 标题区 + 1 行正文 + 按钮区
    kH = 34 + 22 + kChipH + 26;
    if (!created_) {
        if (!Create()) return;
        created_ = true;
    }
    Layout();
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.right - kW - 16;
    int y = work.top + 16;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, kW, kH, SWP_NOACTIVATE);
    KillTimer(hwnd_, 1);
    SetTimer(hwnd_, 1, 12000, nullptr);  // 12s 自动忽略
    ShowWindow(hwnd_, SW_SHOW);
    Draw();
}

void HintToast::Dismiss() {
    if (!hwnd_) return;
    KillTimer(hwnd_, 1);
    ShowWindow(hwnd_, SW_HIDE);
    if (on_dismiss_) {
        auto cb = std::move(on_dismiss_);
        on_dismiss_ = nullptr;
        on_accept_ = nullptr;
        cb();
    }
}

bool HintToast::Create() {
    EnsureGdiplus();
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LumenBoxHint";
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                            wc.lpszClassName, L"LumenBox 提示", WS_POPUP,
                            CW_USEDEFAULT, CW_USEDEFAULT, kW, kH, nullptr, nullptr,
                            wc.hInstance, this);
    return hwnd_ != nullptr;
}

void HintToast::Draw() {
    EnsureGdiplus();
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kW;
    bi.bmiHeader.biHeight = -kH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC hdcMem = CreateCompatibleDC(nullptr);
    HBITMAP dib = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    auto old = (HBITMAP)SelectObject(hdcMem, dib);
    {
        Gdiplus::Graphics g(hdcMem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        // 玻璃卡面（与格子卡同参：#0B0E14 0.62α + 极光描边）
        Gdiplus::SolidBrush fill(Color(158, 11, 14, 20));
        Gdiplus::Pen stroke(Color(60, 34, 211, 238), 1.f);  // 极光青描边
        Gdiplus::GraphicsPath path;
        float r = 12.f;
        Gdiplus::RectF rr(0.5f, 0.5f, kW - 1.f, kH - 1.f);
        path.AddArc(rr.X, rr.Y, r, r, 180, 90);
        path.AddArc(rr.X + rr.Width - r, rr.Y, r, r, 270, 90);
        path.AddArc(rr.X + rr.Width - r, rr.Y + rr.Height - r, r, r, 0, 90);
        path.AddArc(rr.X, rr.Y + rr.Height - r, r, r, 90, 90);
        path.CloseFigure();
        g.FillPath(&fill, &path);
        g.DrawPath(&stroke, &path);

        // 标题
        Gdiplus::SolidBrush title(Gdiplus::Color(235, 238, 245));
        Gdiplus::Font fT(L"Microsoft YaHei UI", 9.5f, Gdiplus::FontStyleBold,
                         Gdiplus::UnitPixel);
        g.DrawString(L"新桌面文件", -1, &fT, Gdiplus::PointF(kPad, 12), &title);

        // 正文
        Gdiplus::SolidBrush body(Gdiplus::Color(200, 205, 215));
        Gdiplus::Font fB(L"Microsoft YaHei UI", 9.f, Gdiplus::FontStyleRegular,
                         Gdiplus::UnitPixel);
        Gdiplus::RectF box(kPad, 34.f, kW - 2.f * kPad, 22.f);
        g.DrawString(BodyText().c_str(), -1, &fB, box, nullptr, &body);

        // 按钮
        Layout();
        Gdiplus::SolidBrush acc_fill(accept_hot_ ? Color(70, 34, 211, 238)
                                                 : Color(50, 255, 255, 255));
        Gdiplus::SolidBrush dis_fill(dismiss_hot_ ? Color(60, 255, 255, 255)
                                                  : Color(36, 255, 255, 255));
        Gdiplus::Font fBtn(L"Microsoft YaHei UI", 9.f, Gdiplus::FontStyleRegular,
                           Gdiplus::UnitPixel);
        Gdiplus::SolidBrush btnText(Gdiplus::Color(235, 238, 245));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        auto chip = [&](const RECT& rc, Gdiplus::SolidBrush& f, const wchar_t* t) {
            Gdiplus::RectF cr((float)rc.left, (float)rc.top, (float)(rc.right - rc.left),
                              (float)(rc.bottom - rc.top));
            Gdiplus::GraphicsPath cp;
            float cr2 = 8.f;
            cp.AddArc(cr.X, cr.Y, cr2, cr2, 180, 90);
            cp.AddArc(cr.X + cr.Width - cr2, cr.Y, cr2, cr2, 270, 90);
            cp.AddArc(cr.X + cr.Width - cr2, cr.Y + cr.Height - cr2, cr2, cr2, 0, 90);
            cp.AddArc(cr.X, cr.Y + cr.Height - cr2, cr2, cr2, 90, 90);
            cp.CloseFigure();
            g.FillPath(&f, &cp);
            g.DrawString(t, -1, &fBtn, cr, &sf, &btnText);
        };
        chip(accept_, acc_fill, L"归位");
        chip(dismiss_, dis_fill, L"忽略");
    }
    // 预乘 alpha
    for (int i = 0; i < kW * kH; ++i) {
        auto* px = static_cast<uint8_t*>(bits) + i * 4;
        uint8_t a = px[3];
        px[0] = uint8_t(px[0] * a / 255);
        px[1] = uint8_t(px[1] * a / 255);
        px[2] = uint8_t(px[2] * a / 255);
    }
    HDC hdcScreen = GetDC(nullptr);
    POINT src{0, 0};
    SIZE size{kW, kH};
    POINT dst{0, 0};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, hdcScreen, &dst, &size, hdcMem, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, hdcScreen);
    SelectObject(hdcMem, old);
    DeleteObject(dib);
    DeleteDC(hdcMem);
}

LRESULT CALLBACK HintToast::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<HintToast*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(
                              reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_MOUSEMOVE: {
        if (!self) break;
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        bool acc = PtInRect(&self->accept_, pt) != FALSE;
        bool dis = PtInRect(&self->dismiss_, pt) != FALSE;
        if (acc != self->accept_hot_ || dis != self->dismiss_hot_) {
            self->accept_hot_ = acc;
            self->dismiss_hot_ = dis;
            self->Draw();
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!self) break;
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (PtInRect(&self->accept_, pt) != FALSE) {
            auto cb = std::move(self->on_accept_);
            self->on_accept_ = nullptr;
            self->on_dismiss_ = nullptr;
            KillTimer(hwnd, 1);
            ShowWindow(hwnd, SW_HIDE);
            if (cb) cb(std::move(self->landings_));
        } else if (PtInRect(&self->dismiss_, pt) != FALSE) {
            self->Dismiss();
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK:   // 双击 = 快速归位
        if (self && self->on_accept_) {
            auto cb = std::move(self->on_accept_);
            self->on_accept_ = nullptr;
            self->on_dismiss_ = nullptr;
            KillTimer(hwnd, 1);
            ShowWindow(hwnd, SW_HIDE);
            if (cb) cb(std::move(self->landings_));
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && self) self->Dismiss();
        return 0;
    case WM_TIMER:
        if (wp == 1 && self) {
            KillTimer(hwnd, 1);
            self->Dismiss();
        }
        return 0;
    case WM_CLOSE:
        if (self) self->Dismiss();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace lb
