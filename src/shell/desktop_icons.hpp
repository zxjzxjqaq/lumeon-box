#pragma once
#include <windows.h>

namespace lb {

// 双击桌面空白隐藏/显示图标（D6：低级钩子 + 热键双模式，可切）。
// 显隐通过 ShowWindow(SHELLDLL_DefView) 实现；双击命中时用 ListView_HitTest
// 排除「点在图标上」，避免吞掉打开动作。

// 直接切换桌面图标显隐。返回 false 表示找不到桌面视图。
bool ToggleDesktopIcons();

// 低级鼠标钩子（WH_MOUSE_LL）。命中桌面空白双击时 PostMessage(notify, msg, 0, 0)。
// 在独立线程安装带消息泵；主线程勿阻塞。返回 false = 安装失败。
bool InstallHideHook(HWND notify, UINT msg);
void RemoveHideHook();
bool HideHookInstalled();

}  // namespace lb
