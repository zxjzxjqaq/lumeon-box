#include "ui/note_window.hpp"
#include <windowsx.h>
#include <d2d1_2.h>
#include <dwrite.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "core/config.hpp"
#include "core/strutil.hpp"
#include "ui/glass_window.hpp"

#pragma comment(lib, "user32")
#pragma comment(lib, "dwrite")

namespace lb {
namespace json = nlohmann;

namespace {
constexpr int kTimerSave = 1;   // EN_CHANGE 防抖保存
constexpr int kTitleH = 26;     // 拖拽条高

std::wstring NoteFile() {
    return ConfigStore::default_dir().wstring() + L"\\notes.json";
}
}  // namespace

void NoteWindow::Toggle() {
    if (!created_) {
        if (!Create()) return;
        created_ = true;
        ShowWindow(hwnd_, SW_SHOW);
        return;
    }
    if (IsWindowVisible(hwnd_)) {
        Save();
        ShowWindow(hwnd_, SW_HIDE);
    } else {
        ShowWindow(hwnd_, SW_SHOW);
    }
}

bool NoteWindow::Visible() const { return created_ && IsWindowVisible(hwnd_); }

bool NoteWindow::Create() {
    file_ = NoteFile();
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

    // NOREDIRECTIONBITMAP：内容全由 DComp 呈现（与格子同栈）
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName,
                            L"LumenBox 便签", WS_POPUP | WS_THICKFRAME | WS_SYSMENU, x, y, 264,
                            324, nullptr, nullptr, wc.hInstance, this);
    if (!hwnd_) return false;
    if (!gfx_.InitForWindow(hwnd_)) return false;
    dpi_ = static_cast<float>(GetDpiForWindow(hwnd_));

    if (FAILED(gfx_.device->CreateVisual(card_.GetAddressOf()))) return false;
    gfx_.root->AddVisual(card_.Get(), TRUE, nullptr);

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
        return false;
    dwrite_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.f,
                              L"zh-cn", titleFmt_.GetAddressOf());

    // 透明编辑层覆于玻璃卡面（WM_CTLCOLOREDIT 返回空刷透出卡面）
    edit_ = CreateWindowExW(0, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                                ES_AUTOVSCROLL | ES_WANTRETURN,
                            12, kTitleH + 8, 240, 260, hwnd_, (HMENU)1, wc.hInstance, nullptr);
    SendMessageW(edit_, WM_SETFONT,
                 (WPARAM)CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI"),
                 TRUE);

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    DrawCard(rc.right, rc.bottom);

    Load();
    return true;
}

void NoteWindow::DrawCard(int wPx, int hPx) {
    if (!gfx_.device || !titleFmt_) return;  // 未初始化保护
    float w = wPx / dpi_, h = hPx / dpi_;
    auto surface = gfx_.DrawToSurface(w, h, dpi_, [&](::ID2D1DeviceContext* ctx, float) {
        ctx->Clear(nullptr);
        // 与格子同款液态玻璃卡面（Aurora Glass 规范）
        ComPtr<::ID2D1SolidColorBrush> fill, line, text;
        ctx->CreateSolidColorBrush(D2D1_COLOR_F{0.043f, 0.055f, 0.078f, 0.74f},
                                   fill.GetAddressOf());
        ctx->CreateSolidColorBrush(D2D1_COLOR_F{1.f, 1.f, 1.f, 0.14f}, line.GetAddressOf());
        ctx->CreateSolidColorBrush(D2D1_COLOR_F{1.f, 1.f, 1.f, 0.55f}, text.GetAddressOf());
        const D2D1_ROUNDED_RECT rr{{0.5f, 0.5f, w - 0.5f, h - 0.5f}, 12.f, 12.f};
        ctx->FillRoundedRectangle(rr, fill.Get());
        ctx->DrawRoundedRectangle(rr, line.Get(), 1.f);
        std::wstring title = L"便签";
        ctx->DrawText(title.c_str(), (UINT32)title.size(), titleFmt_.Get(),
                      D2D1_RECT_F{10.f, 4.f, 120.f, (float)kTitleH}, text.Get());
    });
    if (surface) {
        card_->SetContent(surface.Get());
        gfx_.device->Commit();
    }
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
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
        if (hit == HTCLIENT) {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            if (pt.y < kTitleH) hit = HTCAPTION;  // 顶部拖拽条
        }
        return hit;
    }
    case WM_SIZE:
        // M0 教训重演：CreateWindowExW/系统菜单「大小」可能早于图形上下文与
        // 编辑层就绪送达 WM_SIZE，未初始化直接绘制即 AV——全部就绪才绘制
        if (self && self->gfx_.device && self->edit_ && wp != SIZE_MINIMIZED) {
            self->DrawCard(LOWORD(lp), HIWORD(lp));
            SetWindowPos(self->edit_, nullptr, 12, kTitleH + 8, LOWORD(lp) - 24,
                         HIWORD(lp) - kTitleH - 16, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
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
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetBkMode(hdc, TRANSPARENT);  // 透出下层玻璃卡面
        SetTextColor(hdc, RGB(235, 238, 245));
        return (LRESULT)GetStockObject(HOLLOW_BRUSH);
    }
    case WM_CLOSE:  // 关闭 = 保存并隐藏（托盘再呼出）
        if (self) self->Save();
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        if (self) self->Save();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace lb
