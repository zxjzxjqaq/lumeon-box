#include "ui/calendar.hpp"
#include <windowsx.h>
#include <string>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus")

namespace lb {

namespace {
constexpr int kPad = 14;
constexpr int kCellW = 32, kCellH = 30;
constexpr int kHeaderH = 38, kWeekH = 30, kFootH = 34;
constexpr int kW = kPad * 2 + 7 * kCellW;        // 252
constexpr int kH = kPad + kHeaderH + kWeekH + 6 * kCellH + kFootH + kPad;
constexpr int kGridTop = kPad + kHeaderH + kWeekH;
constexpr int kFootTop = kGridTop + 6 * kCellH;

ULONG_PTR token_ = 0;
void EnsureGdip() {
    if (!token_) {
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&token_, &si, nullptr);
    }
}

bool LeapY(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
int DimOf(int y, int m) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2) return LeapY(y) ? 29 : 28;
    return d[m - 1];
}
long long Jdn(int y, int m, int d) {
    long long a = (14 - m) / 12, yy = y + 4800 - a, mm = m + 12 * a - 3;
    return d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
}
int Weekday(int y, int m, int d) { return (int)(Jdn(y, m, d) % 7); }  // 0=周一

void BuildCard(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float r) {
    Gdiplus::RectF rr(x, y, w, h);
    p.AddArc(rr.X, rr.Y, r, r, 180, 90);
    p.AddArc(rr.X + rr.Width - r, rr.Y, r, r, 270, 90);
    p.AddArc(rr.X + rr.Width - r, rr.Y + rr.Height - r, r, r, 0, 90);
    p.AddArc(rr.X, rr.Y + rr.Height - r, r, r, 90, 90);
    p.CloseFigure();
}
}  // namespace

void Calendar::Open(HWND owner, int y, int m, int d, int px, int py) {
    EnsureGdip();
    scale_ = (owner ? GetDpiForWindow(owner) : GetDpiForSystem()) / 96.f;  // 按所在窗口 DPI（Per-Monitor）
    if (!hwnd_) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = &WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"LumenBoxCalendar";
        wc.hbrBackground = nullptr;
        RegisterClassW(&wc);
        int w = (int)(kW * scale_), h = (int)(kH * scale_);
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"选择日期",
                                WS_POPUP, px, py, w, h, owner, nullptr, wc.hInstance, this);
        HRGN rgn = CreateRoundRectRgn(0, 0, w, h, (int)(24 * scale_), (int)(24 * scale_));
        SetWindowRgn(hwnd_, rgn, TRUE);
    }
    // 夹回工作区（用缩放后的物理尺寸，避免高 DPI 下越界）
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int sw = (int)(kW * scale_), sh = (int)(kH * scale_);
    if (px + sw > work.right) px = work.right - sw - 4;
    if (py + sh > work.bottom) py = work.bottom - sh - 4;
    if (px < work.left) px = work.left + 4;
    if (py < work.top) py = work.top + 4;
    SetWindowPos(hwnd_, HWND_TOPMOST, px, py, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    viewY_ = y;
    viewM_ = m;
    selY_ = y;
    selM_ = m;
    selD_ = d;
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void Calendar::Close() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

LRESULT CALLBACK Calendar::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Calendar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(
                              reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_CANCEL) {
            if (self) self->Close();
            return 0;
        }
        break;
    case WM_LBUTTONDOWN: {
        if (!self) return 0;
        float s = self->scale_ ? self->scale_ : 1.f;
        int x = (int)(GET_X_LPARAM(lp) / s), y = (int)(GET_Y_LPARAM(lp) / s);  // 逻辑坐标
        // 头栏 ◀ / ▶
        if (y >= kPad && y < kPad + kHeaderH) {
            if (x >= kPad && x <= kPad + 34) {  // 上个月
                if (--self->viewM_ == 0) {
                    self->viewM_ = 12;
                    --self->viewY_;
                }
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            if (x >= kW - kPad - 34 && x <= kW - kPad) {  // 下个月
                if (++self->viewM_ == 13) {
                    self->viewM_ = 1;
                    ++self->viewY_;
                }
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
        }
        // 底部：今天 / 取消
        if (y >= kFootTop && y < kFootTop + kFootH) {
            if (x < kW / 2) {  // 今天
                SYSTEMTIME st;
                GetLocalTime(&st);
                if (self->on_pick_) self->on_pick_(st.wYear, st.wMonth, st.wDay);
            }
            self->Close();
            return 0;
        }
        // 日期格
        int y0 = y - kGridTop;
        if (y0 >= 0 && y0 < 6 * kCellH) {
            int row = y0 / kCellH, col = (x - kPad) / kCellW;
            if (x >= kPad && col >= 0 && col < 7) {
                int firstCol = Weekday(self->viewY_, self->viewM_, 1);
                int day = row * 7 + col - firstCol + 1;
                if (day >= 1 && day <= DimOf(self->viewY_, self->viewM_)) {
                    if (self->on_pick_) self->on_pick_(self->viewY_, self->viewM_, day);
                    self->Close();
                    return 0;
                }
            }
        }
        return 0;
    }
    case WM_PAINT: {
        EnsureGdip();
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        {
            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
            if (self) g.ScaleTransform(self->scale_, self->scale_);  // 逻辑 → 物理（高 DPI 清晰）
            Gdiplus::GraphicsPath path;
            BuildCard(path, 0.5f, 0.5f, (float)kW - 1.f, (float)kH - 1.f, 12.f);
            Gdiplus::SolidBrush fill(Gdiplus::Color(0x0B, 0x0E, 0x14));
            g.FillPath(&fill, &path);
            Gdiplus::Pen border(Gdiplus::Color(36, 255, 255, 255), 1.f);
            g.DrawPath(&border, &path);

            Gdiplus::Font f(L"Microsoft YaHei UI", 11.f, Gdiplus::FontStyleRegular,
                            Gdiplus::UnitPixel);
            Gdiplus::SolidBrush white(Gdiplus::Color(235, 238, 245));
            Gdiplus::SolidBrush dim(Gdiplus::Color(140, 255, 255, 255));
            Gdiplus::SolidBrush accent(Gdiplus::Color(0x22, 0xD3, 0xEE));  // 选中/今天
            Gdiplus::SolidBrush dark(Gdiplus::Color(0x0B, 0x0E, 0x14));    // 选中上的深字

            // 头栏：◀  YYYY年M月  ▶
            g.DrawString(L"\u25C0", -1, &f, Gdiplus::PointF((float)kPad, (float)kPad + 10),
                         &dim);
            std::wstring mm = std::to_wstring(self->viewY_) + L"年" +
                              std::to_wstring(self->viewM_) + L"月";
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::RectF hb((float)40, (float)kPad, (float)(kW - 80), (float)kHeaderH);
            g.DrawString(mm.c_str(), (int)mm.size(), &f, hb, &sf, &white);
            g.DrawString(L"\u25B6", -1, &f, Gdiplus::PointF((float)(kW - kPad - 14),
                                                           (float)kPad + 10), &dim);

            // 周名
            static const wchar_t* WD[] = {L"一", L"二", L"三", L"四", L"五", L"六", L"日"};
            for (int c = 0; c < 7; ++c) {
                Gdiplus::RectF cb((float)(kPad + c * kCellW), (float)(kPad + kHeaderH),
                                  (float)kCellW, (float)kWeekH);
                Gdiplus::StringFormat csf;
                csf.SetAlignment(Gdiplus::StringAlignmentCenter);
                csf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                g.DrawString(WD[c], 1, &f, cb, &csf, &dim);
            }

            // 日期格
            int firstCol = Weekday(self->viewY_, self->viewM_, 1);
            int days = DimOf(self->viewY_, self->viewM_);
            SYSTEMTIME today;
            GetLocalTime(&today);
            for (int d = 1; d <= days; ++d) {
                int idx = firstCol + (d - 1);
                int row = idx / 7;
                int col = idx % 7;
                if (row >= 6) break;
                float cx = (float)(kPad + col * kCellW);
                float cy = (float)(kGridTop + row * kCellH);
                bool sel = (d == self->selD_ && self->viewM_ == self->selM_ &&
                            self->viewY_ == self->selY_);
                bool istoday = (d == today.wDay && self->viewM_ == today.wMonth &&
                                self->viewY_ == today.wYear);
                Gdiplus::RectF cell(cx, cy, (float)kCellW, (float)kCellH);
                if (sel) {
                    Gdiplus::GraphicsPath cp;
                    BuildCard(cp, cx, cy, (float)kCellW, (float)kCellH, 8.f);
                    g.FillPath(&accent, &cp);
                    g.DrawString(std::to_wstring(d).c_str(), -1, &f, cell, &sf, &dark);
                } else if (istoday) {
                    Gdiplus::GraphicsPath cp;
                    BuildCard(cp, cx, cy, (float)kCellW, (float)kCellH, 8.f);
                    Gdiplus::Pen tpen(&accent, 1.2f);
                    g.DrawPath(&tpen, &cp);
                    g.DrawString(std::to_wstring(d).c_str(), -1, &f, cell, &sf, &white);
                } else {
                    g.DrawString(std::to_wstring(d).c_str(), -1, &f, cell, &sf, &dim);
                }
            }

            // 底部：今天 / 取消
            Gdiplus::RectF tk((float)kPad, (float)kFootTop, (float)(kW / 2 - kPad),
                              (float)kFootH);
            g.DrawString(L"\u4ECA\u5929", -1, &f, tk, &sf, &accent);  // 今天
            Gdiplus::RectF cancel((float)(kW / 2), (float)kFootTop, (float)(kW / 2 - kPad),
                                  (float)kFootH);
            g.DrawString(L"\u53D6\u6D88", -1, &f, cancel, &sf, &dim);  // 取消
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        if (self) self->Close();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace lb
