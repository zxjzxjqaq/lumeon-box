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
constexpr int kTimerSave = 1;   // EN_CHANGE 防抖保存
constexpr int kTitleH = 26;     // 拖拽条高
constexpr int kW = 264, kH = 324;

std::wstring NoteFile() {
    return ConfigStore::default_dir().wstring() + L"\\notes.json";
}

ULONG_PTR g_gdipToken = 0;

void EnsureGdiplus() {
    if (!g_gdipToken) {
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&g_gdipToken, &si, nullptr);
    }
}
}  // namespace

void NoteWindow::Toggle() {
    if (!created_) {
        if (!Create()) return;
        created_ = true;
        ShowWindow(hwnd_, SW_SHOW);
        ShowWindow(edit_, SW_SHOW);
        return;
    }
    if (IsWindowVisible(hwnd_)) {
        Save();
        ShowWindow(hwnd_, SW_HIDE);
        ShowWindow(edit_, SW_HIDE);
    } else {
        ShowWindow(hwnd_, SW_SHOW);
        ShowWindow(edit_, SW_SHOW);
    }
}

bool NoteWindow::Visible() const { return created_ && IsWindowVisible(hwnd_); }

bool NoteWindow::Create() {
    file_ = NoteFile();
    if (!hwnd_) {}  // placeholder
    EnsureGdiplus();
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LumenBoxNote";
    RegisterClassW(&wc);

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    std::ifstream f(file_, std::ios::binary);
    if (f) {
        try {
            json::json j = json::json::parse(f);
            x = j.value("x", CW_USEDEFAULT);
            y = j.value("y", CW_USEDEFAULT);
        } catch (...) {
        }
    }

    // 分层窗口：逐像素透明玻璃卡
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, wc.lpszClassName,
                            L"LumenBox 便签", WS_POPUP, x, y, kW, kH, nullptr, nullptr,
                            wc.hInstance, this);
    if (!hwnd_) return false;
    if (hwnd_ == INVALID_HANDLE_VALUE) return false;

    // 编辑弹窗：EDIT 不能作为分层窗口的子控件，独立成层
    edit_ = CreateWindowExW(WS_EX_TOOLWINDOW, L"EDIT", L"",
                            WS_POPUP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
                                ES_WANTRETURN,
                            0, 0, kW - 24, kH - kTitleH - 16, hwnd_, nullptr,
                            wc.hInstance, nullptr);
    SendMessageW(edit_, WM_SETFONT,
                 (WPARAM)CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI"),
                 TRUE);

    DrawGlass();
    SyncEdit();
    Load();
    return true;
}

void NoteWindow::DrawGlass() {
    EnsureGdiplus();
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kW;
    bi.bmiHeader.biHeight = -kH;  // top-down
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
        // 玻璃卡面：#0B0E14 0.62α（与格子非悬停态同参）
        Gdiplus::SolidBrush fill(Gdiplus::Color(158, 11, 14, 20));
        Gdiplus::Pen stroke(Gdiplus::Color(36, 255, 255, 255), 1.f);
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
        Gdiplus::SolidBrush title(Gdiplus::Color(235, 238, 245));
        Gdiplus::Font fnt(L"Microsoft YaHei UI", 9.5f, Gdiplus::FontStyleBold,
                          Gdiplus::UnitPixel);
        g.DrawString(L"便签", -1, &fnt, Gdiplus::PointF(12, 4), &title);
    }

    // 更新分层窗口（需要预乘 alpha）
    for (int i = 0; i < kW * kH; ++i) {
        auto* px = static_cast<uint8_t*>(bits) + i * 4;  // BGRA
        uint32_t a = px[3];
        px[0] = uint8_t(px[0] * a / 255);
        px[1] = uint8_t(px[1] * a / 255);
        px[2] = uint8_t(px[2] * a / 255);
    }

    HDC hdcScreen = GetDC(nullptr);
    POINT src{0, 0};
    SIZE size{kW, kH};
    POINT dst{0, 0};  // 位置由 SetWindowPos 单独控制
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, hdcScreen, &dst, &size, hdcMem, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, hdcScreen);
    SelectObject(hdcMem, old);
    DeleteObject(dib);
    DeleteDC(hdcMem);
    SyncEdit();
}

void NoteWindow::SyncEdit() {
    if (!edit_ || !hwnd_) return;
    RECT r{};
    GetWindowRect(hwnd_, &r);
    SetWindowPos(edit_, nullptr, r.left + 12, r.top + kTitleH + 8, r.right - r.left - 24,
                 r.bottom - r.top - kTitleH - 16, SWP_NOZORDER | SWP_NOACTIVATE);
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
    json::json j{{"text", ToUtf8(text)},
                 {"x", r.left},
                 {"y", r.top},
                 {"w", r.right - r.left},
                 {"h", r.bottom - r.top}};
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
    case WM_NCHITTEST:
        return HTCAPTION;  // 整卡拖动（输入在编辑弹窗层）
    case WM_WINDOWPOSCHANGED:
        if (self) self->SyncEdit();
        break;
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
        if (self->edit_) ShowWindow(self->edit_, SW_HIDE);
        return 0;
    case WM_DESTROY:
        if (self) self->Save();
        if (self->edit_) DestroyWindow(self->edit_);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace lb
