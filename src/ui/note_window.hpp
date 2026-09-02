#pragma once
#include <windows.h>
#include <string>

namespace lb {

// 液体玻璃便签：不透明深色圆角卡（GDI+ 卡面 + 圆角区域）+ 深色子 EDIT（白字），
// 与容器同色系（#0B0E14、极光渐变描边、圆角）。卡画在 WM_PAINT，编辑区为子窗口。
// 防抖自动保存、位置/尺寸记忆、标题区拖卡。
class NoteWindow {
public:
    void Toggle();                      // 无则创建并显示；有则显隐切换
    bool Visible() const;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    bool Create();
    void Load();
    void Save();

    HWND hwnd_ = nullptr, edit_ = nullptr;
    std::wstring file_;  // %APPDATA%\LumenBox\notes.json
    bool created_ = false, dirty_ = false;
};

}  // namespace lb
