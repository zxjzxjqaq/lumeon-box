#pragma once
#include <windows.h>
#include <string>
#include <shlobj.h>
#include "ui/d2d_interop.hpp"

namespace lb {

// 资源管理器样式的上下文菜单：shlobj IContextMenu（含 IContextMenu2/3）。
// 用法：Build/BuildDesktop → Show（期间窗回调 OnMenu 会把 WM_INITMENUPOPUP/DRAWITEM/
// MEASUREITEM 转发到 HandleMsg）。继承 IContextMenu 的命令由 Show 内部 InvokeCommand；
// 文件名项（放回桌面/删除）返回给宿主处理：放回桌面由宿主移动，删除由宿主回收站删除。
class ShellCtxMenu {
public:
    static constexpr UINT kCustomBackToDesktop = 0x8000;  // 追加的自定义项 id
    static constexpr int kCustomDelete = 0x8001;  // 外壳 Delete → 宿主改走回收站
    static constexpr int kShellDone = 0x8002;     // 某外壳命令已执行 → 宿主刷新列表

    bool Build(HWND hwnd, const std::wstring& path);  // 文件上下文菜单
    bool BuildDesktop(HWND hwnd);                     // 桌面背景上下文菜单
    // 弹出并执行；返回 kCustomBackToDesktop/kCustomDelete/kShellDone 或 -1（未选择/失败）。
    int Show(HWND hwnd, int x, int y);
    LRESULT HandleMsg(UINT msg, WPARAM wp, LPARAM lp);
    void Reset();
    // 把文件移到回收站（FOF_ALLOWUNDO），保证可撤销
    static bool RecycleToBin(HWND hwnd, const std::wstring& path);

private:
    bool is_desktop_ = false;  // 桌面背景菜单不加自定义项、无文件 Delete 拦截
    ComPtr<IContextMenu> cm1_;
    ComPtr<IContextMenu2> cm2_;
    ComPtr<IContextMenu3> cm3_;
};

}  // namespace lb
