#pragma once
#include <windows.h>
#include <string>
#include <shlobj.h>
#include "ui/d2d_interop.hpp"

namespace lb {

// 资源管理器样式的文件上下文菜单：shlobj IContextMenu（含 IContextMenu2/3）。
// 用法：Build → Show（期间窗回调 OnMenu 会把 WM_INITMENUPOPUP/DRAWITEM/MEASUREITEM
// 转发到 HandleMsg）。继承 IContextMenu 的命令直接 InvokeCommand；
// 自定义项（如「放回桌面」）由 Show 返回其 id，宿主自行处理。
class ShellCtxMenu {
public:
    static constexpr UINT kCustomBackToDesktop = 0x8000;  // 追加的自定义项 id

    bool Build(HWND hwnd, const std::wstring& path);
    // 弹出并执行；返回被选中的自定义项 id，或 -1（无自定义/未选择/失败）。
    int Show(HWND hwnd, int x, int y);
    LRESULT HandleMsg(UINT msg, WPARAM wp, LPARAM lp);
    void Reset();

private:
    ComPtr<IContextMenu> cm1_;
    ComPtr<IContextMenu2> cm2_;
    ComPtr<IContextMenu3> cm3_;
};

}  // namespace lb
