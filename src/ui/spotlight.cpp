#include "ui/spotlight.hpp"
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <algorithm>
#include <filesystem>

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
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName,
                            L"LumenBox 搜索", WS_POPUP,
                            CW_USEDEFAULT, CW_USEDEFAULT, kW, kH, nullptr, nullptr,
                            wc.hInstance, this);
    if (!hwnd_) return false;
    SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, kW, kH, 16, 16), TRUE);
    return true;
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
    Refresh();
    MoveToTop();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
    SetTimer(hwnd_, 1, 500, nullptr);
}

void Spotlight::OnClose() {
    if (!hwnd_) return;
    KillTimer(hwnd_, 1);
    ShowWindow(hwnd_, SW_HIDE);
}

void Spotlight::Refresh() {
    hits_.clear();
    if (apps_) {
        for (auto& a : apps_->Search(query_)) hits_.push_back(a);
    }
    for (auto& f : SearchFiles(files_, query_)) hits_.push_back(f);
    if (sel_ >= (int)hits_.size()) sel_ = (int)hits_.size() - 1;
    if (sel_ < 0) sel_ = 0;
    InvalidateRect(hwnd_, nullptr, FALSE);
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

void Spotlight::Paint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        // 深色玻璃底色（normal 窗口：不透明，保证键盘/文字清晰）
        Gdiplus::SolidBrush bg(Gdiplus::Color(244, 11, 14, 20));
        Gdiplus::RectF card(0.5f, 0.5f, kW - 1.f, kH - 1.f);
        g.FillRectangle(&bg, card);
        Gdiplus::Pen stroke(Gdiplus::Color(60, 34, 211, 238), 1.f);
        g.DrawRectangle(&stroke, card);

        // 查询行
        Gdiplus::SolidBrush qtext(Gdiplus::Color(235, 238, 245));
        Gdiplus::Font fQ(L"Microsoft YaHei UI", 13.f, Gdiplus::FontStyleRegular,
                         Gdiplus::UnitPixel);
        std::wstring disp = query_.empty() ? L"搜索应用与桌面文件…" : query_;
        Gdiplus::SolidBrush ph(Gdiplus::Color(120, 150, 160));
        g.DrawString(disp.c_str(), -1, &fQ, Gdiplus::RectF(kPad, 14.f, kW - 2.f * kPad, 30),
                     nullptr, query_.empty() ? &ph : &qtext);
        if (caret_on_) {
            Gdiplus::SolidBrush caret(Gdiplus::Color(34, 211, 238));
            g.FillRectangle(&caret, Gdiplus::RectF(kPad + 4.f + query_.size() * 13.f,
                                                   18.f, 2.f, 18.f));
        }

        // 分隔线
        Gdiplus::Pen sep(Gdiplus::Color(40, 255, 255, 255), 1.f);
        Gdiplus::REAL sy = (Gdiplus::REAL)kHeaderH + 2.f;
        g.DrawLine(&sep, (Gdiplus::REAL)kPad, sy, (Gdiplus::REAL)(kW - kPad), sy);

        // 结果行
        Gdiplus::Font fName(L"Microsoft YaHei UI", 9.5f, Gdiplus::FontStyleRegular,
                            Gdiplus::UnitPixel);
        Gdiplus::Font fPath(L"Microsoft YaHei UI", 8.5f, Gdiplus::FontStyleRegular,
                            Gdiplus::UnitPixel);
        Gdiplus::SolidBrush nameBrush(Gdiplus::Color(228, 232, 240));
        Gdiplus::SolidBrush pathBrush(Gdiplus::Color(130, 140, 150));
        if (hits_.empty()) {
            Gdiplus::SolidBrush empty(Gdiplus::Color(150, 150, 150));
            g.DrawString(L"未找到匹配", -1, &fPath,
                         Gdiplus::RectF(kPad, (float)kHeaderH + 12.f, kW - 2.f * kPad, 20),
                         nullptr, &empty);
        } else {
            int visible = std::min<int>(kMaxRows, (int)hits_.size());
            for (int i = 0; i < visible; ++i) {
                float y = (float)kHeaderH + i * kRowH;
                if (i == sel_) {
                    Gdiplus::SolidBrush sel(Gdiplus::Color(48, 34, 211, 238));
                    Gdiplus::RectF rr(kPad - 8.f, y + 2.f, kW - 2.f * (kPad - 8.f),
                                      kRowH - 4.f);
                    g.FillRectangle(&sel, rr);
                }
                std::wstring name = hits_[i].name;
                g.DrawString(name.c_str(), -1, &fName,
                             Gdiplus::RectF(kPad, y + 3.f, kW - 2.f * kPad, 18), nullptr,
                             &nameBrush);
                std::wstring path = hits_[i].path;
                g.DrawString(path.c_str(), -1, &fPath,
                             Gdiplus::RectF(kPad, y + 19.f, kW - 2.f * kPad, 14), nullptr,
                             &pathBrush);
            }
            if ((int)hits_.size() > visible) {
                Gdiplus::SolidBrush more(Gdiplus::Color(120, 150, 160));
                std::wstring s = L"… 还有 " + std::to_wstring((int)hits_.size() - visible) +
                                 L" 项";
                g.DrawString(s.c_str(), -1, &fPath,
                             Gdiplus::RectF(kPad, kH - 22.f, kW - 2.f * kPad, 16), nullptr,
                             &more);
            }
        }
    }
    EndPaint(hwnd_, &ps);
}

LRESULT CALLBACK Spotlight::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Spotlight*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(
                              reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_PAINT:
        if (self) self->Paint();
        return 0;
    case WM_CHAR:
        if (self && wp >= 32 && wp != 127) {
            if (self->query_.size() < 120) self->query_.push_back((wchar_t)wp);
            self->Refresh();
        }
        return 0;
    case WM_KEYDOWN:
        if (!self) break;
        if (wp == VK_ESCAPE) {
            self->OnClose();
        } else if (wp == VK_RETURN) {
            self->Launch(self->sel_);
        } else if (wp == VK_UP) {
            if (self->sel_ > 0) --self->sel_;
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wp == VK_DOWN) {
            if (self->sel_ < (int)self->hits_.size() - 1) ++self->sel_;
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wp == VK_BACK) {
            if (!self->query_.empty()) {
                self->query_.pop_back();
                self->Refresh();
            }
        }
        return 0;
    case WM_TIMER:
        if (wp == 1 && self) {
            self->caret_on_ = !self->caret_on_;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_ACTIVATE:
        if (self && (LOWORD(wp) == WA_INACTIVE)) self->OnClose();
        return 0;
    case WM_CLOSE:
        if (self) self->OnClose();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace lb
