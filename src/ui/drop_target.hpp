#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <windows.h>
#include <oleidl.h>
#include "core/rules.hpp"

namespace lb {

// OLE 拖放目标：资源管理器拖文件到格子 → 归位。
// 与 UI/核心层解耦：三个钩子注入，可脱离 DComp 单测（假 IDataObject 直接调方法）。
class DropTarget : public IDropTarget {
public:
    // boxAtScreen: 屏幕坐标 → 格子索引（-1 非卡片）；catOf: 格子索引 → 类目；
    // onFiles: 归位执行（卡片→MoveToCategory；Category::None→MoveBackToDesktop 放回桌面 + 刷新）；
    // onDragBox: 悬停格子变化高亮（-1 离开）
    void SetHooks(std::function<int(const POINTL&)> boxAtScreen,
                  std::function<Category(int)> catOf,
                  std::function<void(const std::vector<std::wstring>&, Category)> onFiles,
                  std::function<void(int)> onDragBox);
    void SetWindow(HWND hwnd) { hwnd_ = hwnd; }

    // 测试辅助：从 HDROP 全局块提取文件列表
    static std::vector<std::wstring> FilesFromHdrop(HGLOBAL h);

    // IUnknown（聚合无关，引用计数自管）
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    // IDropTarget
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* obj, DWORD grfKeyState, POINTL pt,
                                        DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD grfKeyState, POINTL pt, DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* obj, DWORD grfKeyState, POINTL pt,
                                   DWORD* effect) override;

private:
    bool HasFiles(IDataObject* obj);
    void UpdateDragBox(int box);

    std::atomic<ULONG> ref_{1};
    IDataObject* dragging_ = nullptr;
    int drag_box_ = -1;
    HWND hwnd_ = nullptr;
    std::function<int(const POINTL&)> box_at_;
    std::function<Category(int)> cat_of_;
    std::function<void(const std::vector<std::wstring>&, Category)> on_files_;
    std::function<void(int)> on_drag_box_;
};

}  // namespace lb
