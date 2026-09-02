#include "ui/note_window.hpp"
#include <windowsx.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "core/config.hpp"
#include "core/strutil.hpp"

#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")

#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus")

namespace lb {
namespace json = nlohmann;

namespace {
constexpr int kTimerSave = 1;  // EN_CHANGE 防抖保存
constexpr int kTitleH = 30;    // 拖拽条高
constexpr int kW = 280, kH = 340;
constexpr int kPad = 14;       // 卡内边距（编辑区左右下）
constexpr int kRadius = 12;    // 圆角（与容器格子一致）

std::wstring NoteFile() {
    return ConfigStore::default_dir().wstring() + L"\\notes.json";
}

ULONG_PTR g_gdipToken = 0;
HBRUSH g_editBrush = nullptr;  // 编辑区深色背景刷（WM_CTLCOLOREDIT）

void EnsureGdiplus() {
    if (!g_gdipToken) {
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&g_gdipToken, &si, nullptr);
    }
    if (!g_editBrush) g_editBrush = CreateSolidBrush(RGB(0x0F, 0x16, 0x20));
}

// 圆角路径（与容器同 12px 圆角）
void BuildCardPath(Gdiplus::GraphicsPath& path, float W, float H, float r) {
    Gdiplus::RectF rr(0.5f, 0.5f, W - 1.f, H - 1.f);
    path.AddArc(rr.X, rr.Y, r, r, 180, 90);
    path.AddArc(rr.X + rr.Width - r, rr.Y, r, r, 270, 90);
    path.AddArc(rr.X + rr.Width - r, rr.Y + rr.Height - r, r, r, 0, 90);
    path.AddArc(rr.X, rr.Y + rr.Height - r, r, r, 90, 90);
    path.CloseFigure();
}
}  // namespace

void NoteWindow::Toggle() {
    if (!created_) {
        if (!Create()) return;
        created_ = true;
        ShowWindow(hwnd_, SW_SHOW);
        SetFocus(edit_);  // 打开即聚焦编辑（注：不抢其它窗口焦点，仅点击/打开后）
        return;
    }
    if (IsWindowVisible(hwnd_)) {
        Save();
        ShowWindow(hwnd_, SW_HIDE);
    } else {
        ShowWindow(hwnd_, SW_SHOW);
        SetFocus(edit_);
    }
}

bool NoteWindow::Visible() const { return created_ && IsWindowVisible(hwnd_); }

bool NoteWindow::Create() {
    file_ = NoteFile();
    EnsureGdiplus();
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LumenBoxNote";
    wc.hbrBackground = nullptr;  // 自绘（WM_PAINT），避免 WM_ERASEBKGND 白闪
    RegisterClassW(&wc);

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int w = kW, h = kH;
    std::ifstream f(file_, std::ios::binary);
    if (f) {
        try {
            json::json j = json::json::parse(f);
            x = j.value("x", CW_USEDEFAULT);
            y = j.value("y", CW_USEDEFAULT);
            w = j.value("w", kW);
            h = j.value("h", kH);
        } catch (...) {
        }
    }
    if (w < 200) w = kW;
    if (h < 220) h = kH;

    // 普通窗口（不透明卡面）+ 圆角区域；编辑区为子窗口，可靠地做成深色
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"LumenBox 便签",
                            WS_POPUP, x, y, w, h, nullptr, nullptr, wc.hInstance, this);
    if (!hwnd_ || hwnd_ == INVALID_HANDLE_VALUE) return false;

    HRGN rgn = CreateRoundRectRgn(0, 0, w, h, kRadius * 2, kRadius * 2);
    SetWindowRgn(hwnd_, rgn, TRUE);  // 区域所有权交给系统

    edit_ = CreateWindowExW(0, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                                ES_AUTOVSCROLL | ES_WANTRETURN | ES_LEFT,
                            kPad, kTitleH + 10, w - kPad * 2, h - kTitleH - 26, hwnd_,
                            (HMENU)1, wc.hInstance, nullptr);
    SendMessageW(edit_, WM_SETFONT,
                 (WPARAM)CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI"),
                 TRUE);
    SendMessageW(edit_, EM_SETLIMITTEXT, 100000, 0);

    Load();
    return true;
}

void NoteWindow::Load() {
    std::ifstream f(file_, std::ios::binary);
    if (!f) return;
    try {
        json::json j = json::json::parse(f);
        std::string text = j.value("text", std::string());
        SetWindowTextW(edit_, ToWide(text).c_str());
    } catch (...) {
    }
}

void NoteWindow::Save() {
    if (file_.empty() || !edit_) return;
    int len = GetWindowTextLengthW(edit_);
    std::wstring text(len, L'\0');
    GetWindowTextW(edit_, text.data(), len + 1);
    RECT r{};
    GetWindowRect(hwnd_, &r);
    // 若无尺寸则回退默认
    int w = (r.right - r.left) > 0 ? (r.right - r.left) : kW;
    int h = (r.bottom - r.top) > 0 ? (r.bottom - r.top) : kH;
    json::json j{{"text", ToUtf8(text)},
                 {"x", r.left},
                 {"y", r.top},
                 {"w", w},
                 {"h", h}};
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(file_).parent_path(), ec);
    std::ofstream f(file_, std::ios::binary);
    f << j.dump(2);
    dirty_ = false;
}

LRESULT CALLBACK NoteWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<NoteWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(
                              reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_NCHITTEST: {  // 标题区拖卡，编辑区交还文本操作
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        if (pt.y >= 0 && pt.y < kTitleH) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_ERASEBKGND:  // 自绘，避免白闪
        return 1;
    case WM_PAINT: {
        EnsureGdiplus();
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int W = rc.right, H = rc.bottom;
        {
            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
            Gdiplus::GraphicsPath path;
            BuildCardPath(path, (float)W, (float)H, (float)kRadius);
            // 卡面：与容器同色 #0B0E14（不透明实卡）
            Gdiplus::SolidBrush fill(Gdiplus::Color(0x0B, 0x0E, 0x14));
            g.FillPath(&fill, &path);
            // 极光渐变描边（青→紫，横向）
            Gdiplus::LinearGradientBrush stroke(
                Gdiplus::RectF(0, 0, (float)W, (float)H),
                Gdiplus::Color(255, 0x22, 0xD3, 0xEE), Gdiplus::Color(255, 0xC0, 0x84, 0xFC),
                Gdiplus::LinearGradientModeHorizontal);
            Gdiplus::Pen pen(&stroke, 1.f);
            g.DrawPath(&pen, &path);
            // 标题：便签
            Gdiplus::Font fnt(L"Microsoft YaHei UI", 10.f, Gdiplus::FontStyleBold,
                              Gdiplus::UnitPixel);
            Gdiplus::SolidBrush title(Gdiplus::Color(235, 238, 245));
            Gdiplus::RectF tb((float)kPad, 5.f, (float)W - kPad * 2, (float)kTitleH - 8);
            Gdiplus::StringFormat sf;
            sf.SetTrimming(Gdiplus::StringTrimmingEllipsisWord);
            sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
            g.DrawString(L"便签", -1, &fnt, tb, &sf, &title);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT: {  // 深色编辑区 + 白字
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(236, 239, 241));
        SetBkColor(hdc, RGB(0x0F, 0x16, 0x20));
        return (LRESULT)g_editBrush;
    }
    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE && self) {  // 防抖自动保存
            self->dirty_ = true;
            SetTimer(hwnd, kTimerSave, 800, nullptr);
        }
        return 0;
    case WM_TIMER:
        if (wp == kTimerSave && self && self->dirty_) {
            KillTimer(hwnd, kTimerSave);
            self->Save();
        }
        return 0;
    case WM_CLOSE:  // 关闭 = 保存并隐藏（托盘再呼出）
        if (self) self->Save();
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        if (self) self->Save();
        if (self->edit_) DestroyWindow(self->edit_);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace lb
