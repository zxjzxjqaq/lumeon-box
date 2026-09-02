#include "ui/spotlight.hpp"
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <algorithm>
#include <vector>

#pragma comment(lib, "gdiplus")

namespace lb {

namespace {
constexpr int kPad = 16;

ULONG_PTR g_token = 0;
void EnsureGdiplus() {
    if (!g_token) {
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&g_token, &si, nullptr);
    }
}
}  // namespace

bool Spotlight::Create(HINSTANCE hInst) {
    EnsureGdiplus();
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LumenBoxSpotlight";
    RegisterClassW(&wc);
    kH = kHeaderH + kMaxRows * kRowH + 12;
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                            wc.lpszClassName, L"LumenBox 搜索", WS_POPUP, CW_USEDEFAULT,
                            CW_USEDEFAULT, kW, kH, nullptr, nullptr, wc.hInstance, this);
    return hwnd_ != nullptr;
}

void Spotlight::Toggle() {
    if (created_ && IsWindowVisible(hwnd_)) {
        OnClose();
    } else {
        if (!created_) {
            if (!Create(GetModuleHandleW(nullptr))) return;
            created_ = true;
        }
        OnOpen();
    }
}

void Spotlight::OnOpen() {
    files_ = filesFn_ ? filesFn_() : std::vector<std::wstring>{};
    query_.clear();
    sel_ = 0;
    Refresh();  // 填充 hits_ 并整帧绘制
    MoveToTop();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);  // SetActiveWindow 对分层窗并不必要且可能引发前台异常，去掉
}

void Spotlight::OnClose() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

void Spotlight::Refresh() {
    hits_.clear();
    if (apps_) {
        for (auto& a : apps_->Search(query_)) hits_.push_back(a);
    }
    for (auto& f : SearchFiles(files_, query_)) hits_.push_back(f);
    if (sel_ >= (int)hits_.size()) sel_ = (int)hits_.size() - 1;
    if (sel_ < 0) sel_ = 0;
    Draw();
}

void Spotlight::Draw() {
    if (!hwnd_) return;

    // 内存 DIB：kW×kH、32bpp，供 GDI+ 绘制后以预乘 alpha 交给 UpdateLayeredWindow
    int w = kW, h = kH;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // 顶朝下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) {
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return;
    }
    HGDIOBJ old = SelectObject(mem, bmp);
    {
        Gdiplus::Graphics g(mem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

        // 先整体涂成透明
        Gdiplus::SolidBrush clear(Gdiplus::Color(0, 0, 0, 0));
        g.FillRectangle(&clear, Gdiplus::RectF(0, 0, (Gdiplus::REAL)w, (Gdiplus::REAL)h));

        // 圆角玻璃卡片（alpha ~0.72）
        Gdiplus::GraphicsPath path;
        const float r = 16.f;
        path.AddArc(0.5f, 0.5f, r, r, 180, 90);
        path.AddArc(w - r - 0.5f, 0.5f, r, r, 270, 90);
        path.AddArc(w - r - 0.5f, h - r - 0.5f, r, r, 0, 90);
        path.AddArc(0.5f, h - r - 0.5f, r, r, 90, 90);
        path.CloseFigure();
        Gdiplus::SolidBrush bg(Gdiplus::Color(184, 11, 14, 20));
        g.FillPath(&bg, &path);
        Gdiplus::Pen stroke(Gdiplus::Color(70, 34, 211, 238), 1.f);
        g.DrawPath(&stroke, &path);

        // 查询行
        Gdiplus::Font fQ(L"Microsoft YaHei UI", 13.f, Gdiplus::FontStyleRegular,
                         Gdiplus::UnitPixel);
        std::wstring disp = query_.empty() ? L"搜索应用与桌面文件…" : query_;
        Gdiplus::SolidBrush qtext(query_.empty() ? Gdiplus::Color(120, 160, 170, 180)
                                                 : Gdiplus::Color(235, 238, 245));
        Gdiplus::RectF qbox((Gdiplus::REAL)kPad, 14.f, (Gdiplus::REAL)(w - 2 * kPad), 30.f);
        g.DrawString(disp.c_str(), (INT)disp.size(), &fQ, qbox, nullptr, &qtext);

        // 静态光标：紧跟已输入文本
        if (!query_.empty()) {
            Gdiplus::RectF mf;
            g.MeasureString(query_.c_str(), (INT)query_.size(), &fQ, qbox, &mf);
            float cx = kPad + mf.Width + 2.f;
            Gdiplus::SolidBrush caret(Gdiplus::Color(220, 34, 211, 238));
            g.FillRectangle(&caret,
                            Gdiplus::RectF(cx, 17.f, 2.f, 18.f));
        }

        // 分隔线
        Gdiplus::SolidBrush sep(Gdiplus::Color(40, 255, 255, 255));
        g.FillRectangle(&sep, Gdiplus::RectF((Gdiplus::REAL)kPad, 45.f,
                                             (Gdiplus::REAL)(w - 2 * kPad), 1.f));

        // 结果行
        Gdiplus::Font fRow(L"Microsoft YaHei UI", 12.f, Gdiplus::FontStyleRegular,
                           Gdiplus::UnitPixel);
        int n = (int)std::min<size_t>(hits_.size(), kMaxRows);
        for (int i = 0; i < n; ++i) {
            float y = kHeaderH + i * (Gdiplus::REAL)kRowH;
            if (i == sel_) {
                Gdiplus::SolidBrush hl(Gdiplus::Color(64, 34, 211, 238));
                g.FillRectangle(
                    &hl, Gdiplus::RectF(8.f, y, (Gdiplus::REAL)(w - 16), (Gdiplus::REAL)kRowH - 4));
            }
            std::wstring name = hits_[i].name;
            if ((int)name.size() > 34) name = name.substr(0, 34) + L"…";
            Gdiplus::SolidBrush rowcol(i == sel_ ? Gdiplus::Color(255, 255, 255)
                                                 : Gdiplus::Color(210, 214, 222));
            Gdiplus::RectF box((Gdiplus::REAL)(kPad + 8), y + 6.f,
                               (Gdiplus::REAL)(w - 2 * (kPad + 8)), (Gdiplus::REAL)kRowH);
            g.DrawString(name.c_str(), (INT)name.size(), &fRow, box, nullptr, &rowcol);
        }
    }

    // 预乘 alpha
    auto* px = static_cast<unsigned*>(bits);
    for (int i = 0; i < w * h; ++i) {
        unsigned a = (px[i] >> 24) & 0xFF;
        px[i] = (a << 24) |
                ((((px[i] >> 16) & 0xFF) * a / 255) << 16) |
                ((((px[i] >> 8) & 0xFF) * a / 255) << 8) |
                (((px[i] & 0xFF) * a / 255));
    }

    POINT ptSrc{0, 0};
    SIZE sz{w, h};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    // 保持窗口现有位置（由 SetWindowPos 决定），位置参数传 NULL
    UpdateLayeredWindow(hwnd_, nullptr, nullptr, &sz, mem, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

void Spotlight::Validate() {
    PAINTSTRUCT ps{};
    BeginPaint(hwnd_, &ps);
    EndPaint(hwnd_, &ps);
}

void Spotlight::MoveToTop() {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = work.right - work.left, h = work.bottom - work.top;
    int x = work.left + (w - kW) / 2;
    int y = work.top + h / 3 - kH / 2;
    if (y < work.top) y = work.top;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, kW, kH, SWP_NOACTIVATE);
}

void Spotlight::Launch(int i) {
    if (i < 0 || i >= (int)hits_.size()) return;
    ShellExecuteW(nullptr, L"open", hits_[i].path.c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
    OnClose();
}

LRESULT CALLBACK Spotlight::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Spotlight*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<Spotlight*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_PAINT:
        self->Validate();  // 分层窗不在 WM_PAINT 绘制，但需清除更新区
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CHAR:
        if (wp == VK_ESCAPE) {
            self->OnClose();
        } else if (wp >= 0x20) {
            self->query_.push_back((wchar_t)wp);
            self->Refresh();
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_RETURN) {
            self->Launch(self->sel_);
        } else if (wp == VK_UP) {
            if (self->sel_ > 0) {
                --self->sel_;
                self->Refresh();
            }
        } else if (wp == VK_DOWN) {
            if (self->sel_ < (int)self->hits_.size() - 1) {
                ++self->sel_;
                self->Refresh();
            }
        } else if (wp == VK_BACK) {
            if (!self->query_.empty()) {
                self->query_.pop_back();
                self->Refresh();
            }
        }
        return 0;
    case WM_CLOSE:
        self->OnClose();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace lb
