#include "ui/note_window.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "core/config.hpp"
#include "core/strutil.hpp"
#include "ui/glass_window.hpp"

#pragma comment(lib, "user32")

namespace lb {
namespace json = nlohmann;

namespace {
constexpr int kTimerSave = 1;  // EN_CHANGE 防抖保存

std::wstring NoteFile() {
    return ConfigStore::default_dir().wstring() + L"\\notes.json";
}
}  // namespace

void NoteWindow::Toggle() {
    if (!created_) {
        Create();
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

void NoteWindow::Create() {
    file_ = NoteFile();
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wc.lpszClassName = L"LumenBoxNote";
    RegisterClassW(&wc);

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    // 位置与内容记忆
    std::ifstream f(file_, std::ios::binary);
    if (f) {
        try {
            json::json j = json::json::parse(f);
            x = j.value("x", CW_USEDEFAULT);
            y = j.value("y", CW_USEDEFAULT);
        } catch (...) {
        }
    }

    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"LumenBox 便签",
                            WS_POPUP | WS_THICKFRAME | WS_SYSMENU, x, y, 264, 324, nullptr,
                            nullptr, wc.hInstance, this);
    ApplyGlassBackdrop(hwnd_);

    // 顶部拖拽条 + 编辑区
    HWND drag = CreateWindowExW(0, L"STATIC", L"  便签", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0,
                                264, 22, hwnd_, nullptr, wc.hInstance, nullptr);
    SendMessageW(drag, WM_SETFONT,
                 (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    edit_ = CreateWindowExW(0, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                                ES_AUTOVSCROLL | ES_WANTRETURN,
                            0, 22, 264, 302, hwnd_, (HMENU)1, wc.hInstance, nullptr);
    SendMessageW(edit_, WM_SETFONT,
                 (WPARAM)CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI"),
                 TRUE);

    // 载入文本
    std::ifstream f2(file_, std::ios::binary);
    if (f2) {
        try {
            json::json j = json::json::parse(f2);
            std::string text = j.value("text", std::string());
            SetWindowTextW(edit_, ToWide(text).c_str());
        } catch (...) {
        }
    }
}

void NoteWindow::Save() {
    if (file_.empty()) return;
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
    std::filesystem::create_directories(
        std::filesystem::path(file_).parent_path(), ec);
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
        // 顶部 22px 拖拽条 + 边缘 resize，其余给编辑框
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
        if (hit == HTCLIENT) {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            if (pt.y < 22) hit = HTCAPTION;
        }
        return hit;
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
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(235, 238, 245));
        SetBkColor(hdc, RGB(14, 17, 24));
        static HBRUSH bg = CreateSolidBrush(RGB(14, 17, 24));
        return (LRESULT)bg;
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
