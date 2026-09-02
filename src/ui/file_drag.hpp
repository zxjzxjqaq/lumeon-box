#pragma once
#include <windows.h>
#include <string>

namespace lb {

// 用 OLE/SHDoDragDrop 对单个文件发起拖动（可拖到桌面/资源管理器/本方框的其他格子）。
// 返回放置效果：DROPEFFECT_MOVE 表示已移动/放置成功。hwnd 为发起拖动的源窗口。
DWORD StartFileDrag(HWND hwnd, const std::wstring& path);

}  // namespace lb
