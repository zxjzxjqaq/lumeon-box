#pragma once
#include <windows.h>
#include <string>

namespace lb {

// 呼出热键字符串 ↔ 注册参数。格式："Ctrl+B" / "Ctrl+Alt+X" / "Shift+F5" / "Ctrl+Space"。
// 修饰键：Ctrl/Alt/Shift/Win（不区分大小写）；键：A-Z、0-9、F1-F12、Space、Tab。
// 解析失败返回 false（调用方回退默认热键）。mods 不含 MOD_NOREPEAT（注册时自行或上）。
bool ParseHotkey(const std::string& text, UINT& mods, UINT& vk);
// 注册参数 → 显示字符串（捕获热键后回写配置用）；vk 不支持时返回空串。
std::string FormatHotkey(UINT mods, UINT vk);

}  // namespace lb