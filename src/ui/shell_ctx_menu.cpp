#include "ui/shell_ctx_menu.hpp"
#include <shellapi.h>
#pragma comment(lib, "shell32")
#pragma comment(lib, "ole32")

namespace lb {

bool ShellCtxMenu::Build(HWND hwnd, const std::wstring& path) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    SFGAOF sf = 0;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, &sf)) || !pidl)
        return false;
    ComPtr<IShellFolder> parent;
    PCUITEMID_CHILD child = nullptr;
    HRESULT hr = SHBindToParent(pidl, IID_PPV_ARGS(&parent), &child);
    CoTaskMemFree(pidl);
    if (FAILED(hr)) return false;

    ComPtr<IContextMenu> cm;
    hr = parent->GetUIObjectOf(hwnd, 1, &child, IID_IContextMenu, nullptr,
                               reinterpret_cast<void**>(cm.GetAddressOf()));
    if (FAILED(hr) || !cm) return false;
    cm.As(&cm2_);
    cm.As(&cm3_);
    cm1_ = cm;
    return true;
}

int ShellCtxMenu::Show(HWND hwnd, int x, int y) {
    if (!cm1_) return -1;
    HMENU menu = CreatePopupMenu();
    // idFirst = 1；0x7FFF 为上限，留 0x8000 给自定义项
    HRESULT hr = cm1_->QueryContextMenu(menu, 0, 1, 0x7FFF,
                                        CMF_NORMAL | CMF_EXPLORE);
    if (FAILED(hr)) {
        DestroyMenu(menu);
        return -1;
    }
    // 追加自定义项：放回桌面（格子文件 → 桌面根）
    if (hr > 0) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCustomBackToDesktop, L"\u653E\u56DE\u684C\u9762");  // 放回桌面

    SetForegroundWindow(hwnd);  // TrackPopupMenu 需要前台窗口才能正常收起
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                             x, y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd <= 0) return -1;           // 未选择
    if (cmd == (int)kCustomBackToDesktop) return (int)kCustomBackToDesktop;

    // 外壳项：cmd 落在 [1, 0x7FFF]，命令偏移 = cmd - 1
    CMINVOKECOMMANDINFOEX info{};
    info.cbSize = sizeof(info);
    info.fMask = CMIC_MASK_UNICODE;
    info.hwnd = hwnd;
    info.lpVerb = MAKEINTRESOURCEA(cmd - 1);
    info.lpVerbW = MAKEINTRESOURCEW(cmd - 1);
    info.nShow = SW_SHOWNORMAL;
    cm1_->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
    return -1;
}

LRESULT ShellCtxMenu::HandleMsg(UINT msg, WPARAM wp, LPARAM lp) {
    if (cm3_) {
        LRESULT r = 0;
        if (SUCCEEDED(cm3_->HandleMenuMsg2(msg, wp, lp, &r))) return r;
    }
    if (cm2_) return cm2_->HandleMenuMsg(msg, wp, lp);
    return 0;
}

void ShellCtxMenu::Reset() {
    cm1_.Reset();
    cm2_.Reset();
    cm3_.Reset();
}

}  // namespace lb
