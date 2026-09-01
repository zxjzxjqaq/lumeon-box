#pragma once
#include <windows.h>
#include <string>

namespace lb {

// 弹出模态单行输入框（owner 为父窗口）。返回输入的文本；取消/空白返回空串。
std::wstring InputBox(HWND owner, const wchar_t* title, const wchar_t* initial);

}  // namespace lb
