#include "ui/shell_ctx_menu.hpp"
#include <shellapi.h>
#include <shobjidl.h>
#include <cwchar>
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
    is_desktop_ = false;
    return true;
}

bool ShellCtxMenu::BuildDesktop(HWND hwnd) {
    ComPtr<IContextMenu> cm;
    // 桌面背景菜单：桌面 IShellFolder 的 IContextMenu（0 项 = 背景）
    ComPtr<IShellFolder> desktop;
    if (SUCCEEDED(SHGetDesktopFolder(&desktop)) && desktop)
        desktop->GetUIObjectOf(hwnd, 0, nullptr, IID_IContextMenu, nullptr,
                               reinterpret_cast<void**>(cm.GetAddressOf()));
    if (!cm) return false;
    cm.As(&cm2_);
    cm.As(&cm3_);
    cm1_ = cm;
    is_desktop_ = true;
    return true;
}

int ShellCtxMenu::Show(HWND hwnd, int x, int y) {
    if (!cm1_) return -1;
    HMENU menu = CreatePopupMenu();
    // idFirst = 1；0x7FFF 为上限，留 0x8000 给自定义项
    HRESULT hr = cm1_->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL | CMF_EXPLORE);
    if (FAILED(hr)) {
        DestroyMenu(menu);
        return -1;
    }
    // 文件菜单才追加自定义项「放回桌面」；桌面背景菜单不加
    if (!is_desktop_) {
        if (hr > 0) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kCustomBackToDesktop, L"\u653E\u56DE\u684C\u9762");  // 放回桌面
    }

    SetForegroundWindow(hwnd);  // TrackPopupMenu 需要前台窗口才能正常收起
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                             x, y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd <= 0) return -1;           // 未选择
    const UINT off = static_cast<UINT>(cmd - 1);  // 命令偏移 = cmd - idFirst(1)
    if (!is_desktop_ && cmd == (int)kCustomBackToDesktop) return (int)kCustomBackToDesktop;

    // 文件菜单的 Delete → 改走回收站（由宿主执行），不交给外壳 InvokeCommand
    if (!is_desktop_) {
        wchar_t verb[64] = {};
        if (cm1_ && SUCCEEDED(cm1_->GetCommandString(off, GCS_VERBW, nullptr,
                                                     reinterpret_cast<LPSTR>(verb), 62)) &&
            verb[0] && _wcsicmp(verb, L"delete") == 0)
            return kCustomDelete;
    }

    // 其它外壳项：InvokeCommand 执行；文件命令后宿主刷新列表
    CMINVOKECOMMANDINFOEX info{};
    info.cbSize = sizeof(info);
    info.fMask = CMIC_MASK_UNICODE;
    info.hwnd = hwnd;
    info.lpVerb = MAKEINTRESOURCEA(off);
    info.lpVerbW = MAKEINTRESOURCEW(off);
    info.nShow = SW_SHOWNORMAL;
    cm1_->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
    return is_desktop_ ? -1 : kShellDone;
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
    is_desktop_ = false;
}

bool ShellCtxMenu::RecycleToBin(HWND hwnd, const std::wstring& path) {
    std::wstring buf = path;
    buf.push_back(L'\0');
    buf.push_back(L'\0');  // 双空结尾路径列表
    SHFILEOPSTRUCTW sfo{};
    sfo.hwnd = hwnd;
    sfo.wFunc = FO_DELETE;
    sfo.pFrom = buf.c_str();
    sfo.fFlags = FOF_ALLOWUNDO;  // 走回收站，同步执行；完成后文件已不在容器中
    return SHFileOperationW(&sfo) == 0 && !sfo.fAnyOperationsAborted;
}

}  // namespace lb
