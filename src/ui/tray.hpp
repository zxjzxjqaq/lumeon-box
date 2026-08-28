#pragma once
#include <windows.h>
#include <shellapi.h>

namespace lb {

// 托盘图标（回调消息由宿主窗口处理）
class TrayIcon {
public:
    bool Add(HWND hwnd, UINT callbackMsg, HICON icon, const wchar_t* tip);
    void Remove();

private:
    HWND hwnd_ = nullptr;
    UINT msg_ = 0;
    bool added_ = false;
};

}  // namespace lb
