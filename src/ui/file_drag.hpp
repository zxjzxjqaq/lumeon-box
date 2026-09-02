#pragma once
#include <windows.h>
#include <string>

namespace lb {

// 用 OLE 对单个文件发起拖动（可拖到桌面/资源管理器/本方框的其他格子）。
// 返回放置效果：DROPEFFECT_MOVE 表示已移动/放置成功。
DWORD StartFileDrag(const std::wstring& path);

}  // namespace lb
