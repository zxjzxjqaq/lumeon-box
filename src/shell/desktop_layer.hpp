#pragma once
#include <windows.h>

namespace lb {

enum class DesktopMode { Independent, DesktopLayer, Bottom };

bool AttachToDesktopLayer(HWND hwnd);  // 挂到壁纸之上、图标之下；失败 false
void UseBottomMode(HWND hwnd);         // 降级：置底
bool RestoreIndependent(HWND hwnd);    // 回普通窗口

}  // namespace lb
