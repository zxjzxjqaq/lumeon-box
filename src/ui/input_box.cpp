#include "ui/input_box.hpp"
#include <windowsx.h>

namespace lb {

namespace {
constexpr int kIDOK = 1, kIDCANCEL = 2;
struct Ctx {
    std::wstring value;
    bool done = false;
};

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ctx = reinterpret_cast<Ctx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == kIDOK) {
            HWND edit = GetDlgItem(hwnd, 101);
            int len = GetWindowTextLengthW(edit);
            std::wstring t(len, L'\0');
            GetWindowTextW(edit, t.data(), len + 1);
            if (ctx) ctx->value = std::move(t);
            if (ctx) ctx->done = true;
            DestroyWindow(hwnd);
        } else if (LOWORD(wp) == kIDCANCEL) {
            if (ctx) ctx->done = true;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_CLOSE:
        if (ctx) ctx->done = true;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}  // namespace

std::wstring InputBox(HWND owner, const wchar_t* title, const wchar_t* initial) {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LumenBoxInput";
    RegisterClassW(&wc);

    const int KW = 300, KH = 130;
    Ctx ctx;
    ctx.value = initial ? initial : L"";

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW, wc.lpszClassName,
                                title, WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, 0, KW, KH,
                                owner, nullptr, hInst, &ctx);
    if (!hwnd) return L"";

    // EDIT 单行
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initial,
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                14, 40, KW - 28, 26, hwnd, (HMENU)(INT_PTR)101, hInst,
                                nullptr);
    // 按钮
    CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    KW - 160, KH - 44, 64, 28, hwnd, (HMENU)(INT_PTR)kIDOK, hInst,
                    nullptr);
    CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    KW - 86, KH - 44, 64, 28, hwnd, (HMENU)(INT_PTR)kIDCANCEL, hInst,
                    nullptr);
    // 提示文字
    CreateWindowExW(0, L"STATIC", L"此格名称（仅显示，不改动文件位置）:",
                    WS_CHILD | WS_VISIBLE, 14, 16, KW - 28, 20, hwnd, nullptr, hInst,
                    nullptr);

    SendMessageW(edit, WM_SETFONT,
                 (WPARAM)CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                     L"Microsoft YaHei UI"),
                 TRUE);

    // 居中到 owner
    RECT wr{}, or_ = {};
    if (owner) GetWindowRect(owner, &or_);
    int wx = or_.left + ((or_.right - or_.left) - KW) / 2;
    int wy = or_.top + ((or_.bottom - or_.top) - KH) / 2;
    SetWindowPos(hwnd, nullptr, wx, wy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    ShowWindow(edit, SW_SHOW);
    SetFocus(edit);

    // 模态循环
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (ctx.done) break;
    }
    DestroyWindow(hwnd);
    if (owner) EnableWindow(owner, TRUE);
    SetForegroundWindow(owner ? owner : hwnd);
    return std::move(ctx.value);
}

}  // namespace lb
