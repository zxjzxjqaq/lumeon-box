#pragma once
#include <windows.h>
#include <string>

namespace lb {

// 贴桌玻璃便签：就地编辑、自动保存（防抖）、位置记忆。托盘「便签」显隐切换。
class NoteWindow {
public:
    void Toggle();                      // 无则创建并显示；有则显隐切换
    bool Visible() const;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void Create();
    void Load();
    void Save();

    HWND hwnd_ = nullptr, edit_ = nullptr;
    std::wstring file_;  // %APPDATA%\LumenBox\notes.json
    bool created_ = false, dirty_ = false;
};

}  // namespace lb
