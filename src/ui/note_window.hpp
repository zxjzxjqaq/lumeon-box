#pragma once
#include <windows.h>
#include <string>
#include <dwrite.h>
#include "ui/d2d_interop.hpp"

namespace lb {

// 液态玻璃便签：与格子同款 DComp 玻璃卡渲染（深色卡面+圆角+描边），
// 编辑层为透明原生 EDIT 覆于卡面。就地编辑、防抖自动保存、位置记忆。
class NoteWindow {
public:
    void Toggle();                      // 无则创建并显示；有则显隐切换
    bool Visible() const;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    bool Create();
    void DrawCard(int wPx, int hPx);
    void Load();
    void Save();

    HWND hwnd_ = nullptr, edit_ = nullptr;
    GraphicsContext gfx_;
    ComPtr<IDCompositionVisual> card_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<IDWriteTextFormat> titleFmt_;
    float dpi_ = 96.f;
    std::wstring file_;  // %APPDATA%\LumenBox\notes.json
    bool created_ = false, dirty_ = false;
};

}  // namespace lb
