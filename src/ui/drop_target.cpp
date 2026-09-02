#include "ui/drop_target.hpp"
#include <shellapi.h>
#include "core/rules.hpp"
#include "core/stage_log.hpp"

namespace lb {

void DropTarget::SetHooks(std::function<int(const POINTL&)> boxAtScreen,
                          std::function<void(const std::vector<std::wstring>&, int)> onFiles,
                          std::function<void(int)> onDragBox) {
    box_at_ = std::move(boxAtScreen);
    on_files_ = std::move(onFiles);
    on_drag_box_ = std::move(onDragBox);
}

std::vector<std::wstring> DropTarget::FilesFromHdrop(HGLOBAL h) {
    std::vector<std::wstring> out;
    if (!h) return out;
    UINT n = DragQueryFileW(static_cast<HDROP>(h), 0xFFFFFFFF, nullptr, 0);
    out.reserve(n);
    for (UINT i = 0; i < n; ++i) {
        UINT len = DragQueryFileW(static_cast<HDROP>(h), i, nullptr, 0);
        std::wstring s(len, L'\0');
        DragQueryFileW(static_cast<HDROP>(h), i, s.data(), len + 1);
        out.push_back(std::move(s));
    }
    // 不 DragFinish：内存块归调用方/STGMEDIUM 所有（Drop 按 pUnkForRelease 释放）
    return out;
}

bool DropTarget::HasFiles(IDataObject* obj) {
    if (!obj) return false;
    FORMATETC f{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return obj->QueryGetData(&f) == S_OK;
}

void DropTarget::UpdateDragBox(int box) {
    if (box == drag_box_) return;
    drag_box_ = box;
    if (on_drag_box_) on_drag_box_(box);
}

HRESULT STDMETHODCALLTYPE DropTarget::QueryInterface(REFIID riid, void** out) {
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *out = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE DropTarget::AddRef() { return ++ref_; }
ULONG STDMETHODCALLTYPE DropTarget::Release() {
    // 栈上/所有者管理生命周期：归零不自删，宿主保证先 RevokeDragDrop
    return --ref_;
}

HRESULT STDMETHODCALLTYPE DropTarget::DragEnter(IDataObject* obj, DWORD, POINTL pt,
                                                DWORD* effect) {
    if (!effect) return E_POINTER;
    dragging_ = HasFiles(obj) ? obj : nullptr;
    if (!dragging_) {
        *effect = DROPEFFECT_NONE;
        UpdateDragBox(-1);
        return S_OK;
    }
    obj->AddRef();  // 拖拽期间保活
    UpdateDragBox(box_at_ ? box_at_(pt) : -1);
    *effect = DROPEFFECT_MOVE;  // 整窗可放置：卡片=进格，非卡片=放回桌面
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DropTarget::DragOver(DWORD, POINTL pt, DWORD* effect) {
    if (!effect) return E_POINTER;
    if (!dragging_) {
        *effect = DROPEFFECT_NONE;
        return S_OK;
    }
    UpdateDragBox(box_at_ ? box_at_(pt) : -1);
    *effect = DROPEFFECT_MOVE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DropTarget::DragLeave() {
    UpdateDragBox(-1);
    if (dragging_) {
        dragging_->Release();
        dragging_ = nullptr;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DropTarget::Drop(IDataObject* obj, DWORD, POINTL pt, DWORD* effect) {
    if (!effect) return E_POINTER;
    if (!dragging_ || !HasFiles(obj)) {
        *effect = DROPEFFECT_NONE;
        UpdateDragBox(-1);
        return S_OK;
    }
    int box = box_at_ ? box_at_(pt) : -1;
    UpdateDragBox(-1);
    if (on_files_) {
        FORMATETC f{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM med{};
        if (obj->GetData(&f, &med) == S_OK) {
            auto files = FilesFromHdrop(med.hGlobal);
            // box>=0 进该盒；-1 非卡片区 → 放回桌面（宿主决定）
            on_files_(files, box);
            if (med.pUnkForRelease)
                med.pUnkForRelease->Release();
            else
                GlobalFree(med.hGlobal);
        }
    }
    dragging_->Release();
    dragging_ = nullptr;
    *effect = DROPEFFECT_MOVE;
    return S_OK;
}

}  // namespace lb
