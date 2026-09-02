#include "ui/input_box.hpp"
#include <windowsx.h>
#include <algorithm>
#include <memory>
#include <utility>
#include <objidl.h>
#include <gdiplus.h>

#pragma comment(lib, "user32")
#pragma comment(lib, "gdiplus")

namespace lb {

namespace {
ULONG_PTR g_gdipToken = 0;

void EnsureGdiplus() {
    if (!g_gdipToken) {
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&g_gdipToken, &si, nullptr);
    }
}

// ---- 尺寸/布局（与容器卡面圆角 12、字体雅黑一致）----
constexpr int kW = 392, kH = 196;
constexpr int kPad = 24;
constexpr int kBtnW = 72, kBtnH = 34, kBtnR = 17;
constexpr int kBtnY = kH - kPad - kBtnH;                // 138
constexpr int kWellX = kPad, kWellY = 70;
constexpr int kWellW = kW - 2 * kPad, kWellH = 44;
constexpr int kTextX = kWellX + 12;
constexpr int kTextY = kWellY + 14;
constexpr int kCardR = 12, kWellR = 10;

int OkX() { return kW - kPad - kBtnW; }                 // 296
int CancelX() { return OkX() - 16 - kBtnW; }            // 208

void AddRoundRect(Gdiplus::GraphicsPath& p, const Gdiplus::RectF& r, float rad) {
    float x = r.X, y = r.Y, w = r.Width, h = r.Height;
    p.AddArc(x, y, rad * 2, rad * 2, 180, 90);
    p.AddArc(x + w - rad * 2, y, rad * 2, rad * 2, 270, 90);
    p.AddArc(x + w - rad * 2, y + h - rad * 2, rad * 2, rad * 2, 0, 90);
    p.AddArc(x, y + h - rad * 2, rad * 2, rad * 2, 90, 90);
    p.CloseFigure();
}

// 极光渐变（青→紫，同容器悬停高亮两端；中间蓝紫为辅）
void FillAurora(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float rad) {
    Gdiplus::LinearGradientBrush lg(r, Gdiplus::Color(255, 34, 211, 238),
                                    Gdiplus::Color(255, 192, 132, 252), 40.f);
    Gdiplus::GraphicsPath p;
    AddRoundRect(p, r, rad);
    g.FillPath(&lg, &p);
}

struct Ctx {
    HWND hwnd = nullptr;
    std::wstring text;
    int caret = 0;
    int selA = 0, selB = 0;  // 选中区间（selA<selB 时有选中）
    bool done = false, ok = false;
    std::wstring result;
    explicit Ctx(const wchar_t* init) {
        text = init ? init : L"";
        caret = (int)text.size();
        selA = 0;
        selB = (int)text.size();  // 打开即全选，方便直接输入替换
    }
    bool HasSel() const { return selA < selB; }
    void ClearSel() { selA = selB = caret; }
    float TextWidth(Gdiplus::Graphics& g, const std::wstring& s) const {
        if (s.empty()) return 0.f;
        Gdiplus::Font f(L"Microsoft YaHei UI", 11.f, Gdiplus::FontStyleRegular,
                        Gdiplus::UnitPixel);
        Gdiplus::RectF box(0, 0, 1000, 60), out;
        g.MeasureString(s.c_str(), (INT)s.size(), &f, box, &out);
        return out.Width;
    }
};

void Draw(Ctx* self, HDC hdc) {
    EnsureGdiplus();
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kW;
    bi.bmiHeader.biHeight = -kH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib) { DeleteDC(mem); return; }
    HGDIOBJ old = SelectObject(mem, dib);
    {
        Gdiplus::Graphics g(mem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));

        Gdiplus::Font fTitle(L"Microsoft YaHei UI", 12.f, Gdiplus::FontStyleRegular,
                             Gdiplus::UnitPixel);
        Gdiplus::Font fBody(L"Microsoft YaHei UI", 11.f, Gdiplus::FontStyleRegular,
                            Gdiplus::UnitPixel);
        Gdiplus::Font fHint(L"Microsoft YaHei UI", 10.f, Gdiplus::FontStyleRegular,
                            Gdiplus::UnitPixel);

        // 玻璃卡面：同容器 #0B0E14 @0.66、圆角12、白@0.14描边
        Gdiplus::RectF card(3.f, 3.f, (Gdiplus::REAL)(kW - 6), (Gdiplus::REAL)(kH - 6));
        {
            Gdiplus::GraphicsPath p;
            AddRoundRect(p, card, (float)kCardR);
            Gdiplus::SolidBrush fill(Gdiplus::Color(168, 11, 14, 20));
            g.FillPath(&fill, &p);
            Gdiplus::Pen border(Gdiplus::Color(36, 255, 255, 255), 1.f);
            g.DrawPath(&border, &p);
        }

        // 标题（白@0.92）与说明（白@0.55）
        Gdiplus::SolidBrush white(Gdiplus::Color(235, 255, 255, 255));
        Gdiplus::SolidBrush dim(Gdiplus::Color(140, 255, 255, 255));
        Gdiplus::SolidBrush mute(Gdiplus::Color(102, 255, 255, 255));
        g.DrawString(L"重命名此格", -1, &fTitle,
                     Gdiplus::RectF((Gdiplus::REAL)kPad, 20.f, 200, 24), nullptr, &white);
        g.DrawString(L"此格名称（仅显示，不改动文件位置）", -1, &fBody,
                     Gdiplus::RectF((Gdiplus::REAL)kPad, 46.f, 320, 20), nullptr, &dim);

        // 输入井：极光描边(外圈) + 内层暗色 #0F1620
        Gdiplus::RectF wellOuter((Gdiplus::REAL)kWellX, (Gdiplus::REAL)kWellY,
                                 (Gdiplus::REAL)kWellW, (Gdiplus::REAL)kWellH);
        FillAurora(g, wellOuter, (float)kWellR);
        Gdiplus::RectF wellIn((Gdiplus::REAL)(kWellX + 1), (Gdiplus::REAL)(kWellY + 1),
                              (Gdiplus::REAL)(kWellW - 2), (Gdiplus::REAL)(kWellH - 2));
        {
            Gdiplus::GraphicsPath p;
            AddRoundRect(p, wellIn, (float)(kWellR - 1));
            Gdiplus::SolidBrush wellfill(Gdiplus::Color(248, 13, 22, 32));
            g.FillPath(&wellfill, &p);
        }

        // 选中高亮（白@~0.27）
        if (self->HasSel()) {
            float sX = (Gdiplus::REAL)(kTextX + self->TextWidth(g, self->text.substr(0, self->selA)));
            float eX = (Gdiplus::REAL)(kTextX + self->TextWidth(g, self->text.substr(0, self->selB)));
            float maxX = (Gdiplus::REAL)(kWellX + kWellW - 6);
            if (eX > maxX) eX = maxX;
            Gdiplus::SolidBrush hl(Gdiplus::Color(70, 255, 255, 255));
            if (eX > sX) g.FillRectangle(&hl, Gdiplus::RectF(sX, (Gdiplus::REAL)kTextY, eX - sX, 17.f));
        }

        // 输入文本（白@0.92）
        if (!self->text.empty())
            g.DrawString(self->text.c_str(), (INT)self->text.size(), &fBody,
                         Gdiplus::RectF((Gdiplus::REAL)kTextX, (Gdiplus::REAL)kTextY,
                                        (Gdiplus::REAL)kWellW, 24.f),
                         nullptr, &white);

        // 白色光标
        float caretX = (Gdiplus::REAL)(kTextX + self->TextWidth(g, self->text.substr(0, self->caret)));
        if (caretX > kWellX + kWellW - 6) caretX = (Gdiplus::REAL)(kWellX + kWellW - 6);
        g.FillRectangle(&white, Gdiplus::RectF(caretX, (Gdiplus::REAL)(kTextY + 1.f), 2.f, 16.f));

        // 提示（白@0.4）
        g.DrawString(L"例如：工作 / 素材 / 待办", -1, &fHint,
                     Gdiplus::RectF((Gdiplus::REAL)kPad, 132.f, 240, 18), nullptr, &mute);

        // 按钮：取消(幽灵) + 确定(极光底/白字)
        Gdiplus::RectF rc((Gdiplus::REAL)CancelX(), (Gdiplus::REAL)kBtnY,
                          (Gdiplus::REAL)kBtnW, (Gdiplus::REAL)kBtnH);
        {
            Gdiplus::GraphicsPath p;
            AddRoundRect(p, rc, (float)kBtnR);
            Gdiplus::SolidBrush b(Gdiplus::Color(26, 255, 255, 255));
            g.FillPath(&b, &p);
            Gdiplus::Pen brd(Gdiplus::Color(56, 255, 255, 255), 1.f);
            g.DrawPath(&brd, &p);
        }
        g.DrawString(L"取消", -1, &fBody,
                     Gdiplus::RectF((Gdiplus::REAL)CancelX() + 23, (Gdiplus::REAL)kBtnY + 9, 40, 18),
                     nullptr, &dim);
        FillAurora(g, Gdiplus::RectF((Gdiplus::REAL)OkX(), (Gdiplus::REAL)kBtnY,
                                     (Gdiplus::REAL)kBtnW, (Gdiplus::REAL)kBtnH),
                   (float)kBtnR);
        g.DrawString(L"确定", -1, &fBody,
                     Gdiplus::RectF((Gdiplus::REAL)OkX() + 23, (Gdiplus::REAL)kBtnY + 9, 40, 18),
                     nullptr, &white);
    }

    // ULW_ALPHA 需要预乘 alpha
    auto* px = static_cast<unsigned*>(bits);
    for (int i = 0; i < kW * kH; ++i) {
        unsigned a = (px[i] >> 24) & 0xFF;
        px[i] = (a << 24) |
                ((((px[i] >> 16) & 0xFF) * a / 255) << 16) |
                ((((px[i] >> 8) & 0xFF) * a / 255) << 8) |
                (((px[i] & 0xFF) * a / 255));
    }

    POINT ptSrc{0, 0};
    SIZE sz{kW, kH};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(self->hwnd, nullptr, nullptr, &sz, mem, &ptSrc, 0, &bf, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
}

void RedrawCtx(Ctx* self) {
    HDC hdc = GetDC(self->hwnd);
    Draw(self, hdc);
    ReleaseDC(self->hwnd, hdc);
}

void Commit(Ctx* self, HWND hwnd, bool ok) {
    self->result = self->text;
    self->ok = ok;
    self->done = true;
    DestroyWindow(hwnd);
}

void SetCaretFromX(Ctx* self, int x, HDC hdc) {
    Gdiplus::Graphics g(hdc);
    int best = (int)self->text.size();
    for (size_t i = 0; i <= self->text.size(); ++i) {
        if (self->TextWidth(g, self->text.substr(0, i)) <= (float)(x - kTextX)) best = (int)i;
        else break;
    }
    self->caret = best;
    self->ClearSel();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Ctx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (y >= 3 && y < 64 && x > 3 && x < kW - 3) return HTCAPTION;  // 顶部标题可拖拽
        return HTCLIENT;
    }
    case WM_KEYDOWN:
        switch ((int)wp) {
        case VK_LEFT:
            if (self->caret > 0) { --self->caret; self->ClearSel(); }
            break;
        case VK_RIGHT:
            if (self->caret < (int)self->text.size()) { ++self->caret; self->ClearSel(); }
            break;
        case VK_HOME:
            self->caret = 0; self->ClearSel();
            break;
        case VK_END:
            self->caret = (int)self->text.size(); self->ClearSel();
            break;
        case VK_BACK:
            if (self->HasSel()) {
                self->text.erase(self->selA, self->selB - self->selA);
                self->caret = self->selA; self->ClearSel();
            } else if (self->caret > 0) {
                self->text.erase(self->caret - 1, 1);
                --self->caret;
            }
            break;
        case VK_DELETE:
            if (self->HasSel()) {
                self->text.erase(self->selA, self->selB - self->selA);
                self->caret = self->selA; self->ClearSel();
            } else if (self->caret < (int)self->text.size()) {
                self->text.erase(self->caret, 1);
            }
            break;
        case VK_RETURN:
            Commit(self, hwnd, true);
            return 0;
        case VK_ESCAPE:
            Commit(self, hwnd, false);
            return 0;
        default:
            break;
        }
        RedrawCtx(self);
        return 0;
    case WM_CHAR:
        if (wp >= 0x20 && wp <= 0xFFFF) {
            if (self->HasSel()) {
                self->text.erase(self->selA, self->selB - self->selA);
                self->caret = self->selA; self->ClearSel();
            }
            self->text.insert(self->caret, 1, (wchar_t)wp);
            ++self->caret;
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (x >= CancelX() && x < CancelX() + kBtnW && y >= kBtnY && y < kBtnY + kBtnH) {
            Commit(self, hwnd, false);
            return 0;
        }
        if (x >= OkX() && x < OkX() + kBtnW && y >= kBtnY && y < kBtnY + kBtnH) {
            Commit(self, hwnd, true);
            return 0;
        }
        if (x >= kWellX && x < kWellX + kWellW && y >= kWellY && y < kWellY + kWellH) {
            HDC hdc = GetDC(hwnd);
            SetCaretFromX(self, x, hdc);
            ReleaseDC(hwnd, hdc);
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        }
        return 0;
    }
    case WM_CLOSE:
        Commit(self, hwnd, false);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}  // namespace

std::wstring InputBox(HWND owner, const wchar_t* title, const wchar_t* initial) {
    (void)title;  // 固定为“重命名此格”，外部标题仅作兼容
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LumenBoxRename";
    RegisterClassW(&wc);

    EnsureGdiplus();
    Ctx ctx(initial);

    HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                                wc.lpszClassName, L"", WS_POPUP, 0, 0, kW, kH, owner, nullptr,
                                hInst, &ctx);
    if (!hwnd) return L"";
    ctx.hwnd = hwnd;

    RECT r{};
    if (owner) GetWindowRect(owner, &r);
    int x = (r.left + r.right) / 2 - kW / 2;
    int y = (r.top + r.bottom) / 2 - kH / 2;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    RedrawCtx(&ctx);  // 先铺内容，再显示，避免空白闪一下
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    MSG m;
    while (!ctx.done) {
        BOOL r_ = GetMessageW(&m, nullptr, 0, 0);
        if (r_ <= 0) break;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    return ctx.ok ? std::move(ctx.result) : L"";
}

}  // namespace lb
