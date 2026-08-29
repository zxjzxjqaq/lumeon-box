#pragma once
#include <windows.h>
#include <string>

namespace lb {

// 液态玻璃便签：分层窗口（GDI+ 逐像素透明玻璃卡）+ 独立编辑弹窗。
// ——EDIT 控件无法渲染在 NOREDIRECTIONBITMAP/DComp 窗口上（无重定向表面，
// 整窗不可见），故玻璃卡走 UpdateLayeredWindow，编辑层为独立弹出层。
// 就地编辑、防抖自动保存、位置记忆。
class NoteWindow {
public:
    void Toggle();                      // 无则创建并显示；有则显隐切换
    bool Visible() const;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    bool Create();
    void DrawGlass();                   // GDI+ 玻璃卡 → UpdateLayeredWindow
    void SyncEdit();                    // 编辑弹窗贴合便签位置尺寸
    void Load();
    void Save();

    HWND hwnd_ = nullptr, edit_ = nullptr;
    std::wstring file_;  // %APPDATA%\LumenBox\notes.json
    bool created_ = false, dirty_ = false;
};

}  // namespace lb
