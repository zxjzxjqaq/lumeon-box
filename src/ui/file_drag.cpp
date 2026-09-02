#include "ui/file_drag.hpp"
#include <objbase.h>
#include <ole2.h>
#include <oleidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstring>

namespace lb {

namespace {

// 拖动源：返回「移动」效果，Esc 取消，松键即放置
class DropSource : public IDropSource {
public:
    DropSource() : ref_(1) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *out = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        if (--ref_ == 0) {
            delete this;
            return 0;
        }
        return ref_;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL fEscape, DWORD grfKeyState) override {
        if (fEscape) return DRAGDROP_S_CANCEL;
        if (!(grfKeyState & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    ULONG ref_;
};

// 枚举 CF_HDROP：多数 shell 放置目标会先 EnumFormatEtc 确认可用格式，
// 原实现返回 E_NOTIMPL 可能被判定为空数据对象而拒绝承接。
class HdropFormatEnum : public IEnumFORMATETC {
public:
    HdropFormatEnum() = default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) {
            *out = static_cast<IEnumFORMATETC*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        if (--ref_ == 0) {
            delete this;
            return 0;
        }
        return ref_;
    }
    HRESULT STDMETHODCALLTYPE Next(ULONG celt, FORMATETC* out, ULONG* fetched) override {
        if (!out || celt < 1) return S_FALSE;
        FORMATETC fe{};
        fe.cfFormat = CF_HDROP;
        fe.ptd = nullptr;
        fe.dwAspect = DVASPECT_CONTENT;
        fe.lindex = -1;
        fe.tymed = TYMED_HGLOBAL;
        out[0] = fe;
        if (fetched) *fetched = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG) override { return S_FALSE; }
    HRESULT STDMETHODCALLTYPE Reset() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** out) override {
        if (!out) return E_POINTER;
        *out = new HdropFormatEnum();
        return S_OK;
    }

private:
    ULONG ref_ = 1;
};

// 数据对象：提供 CF_HDROP（单个文件路径 + 结尾双空字符）
class FileData : public IDataObject {
public:
    explicit FileData(std::wstring path) : path_(std::move(path)), ref_(1) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *out = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        if (--ref_ == 0) {
            delete this;
            return 0;
        }
        return ref_;
    }

    static bool IsHdrop(const FORMATETC* fe) {
        return fe && fe->cfFormat == CF_HDROP && (fe->tymed & TYMED_HGLOBAL);
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* fe, STGMEDIUM* med) override {
        if (!IsHdrop(fe)) return DV_E_FORMATETC;
        if (!med) return E_INVALIDARG;
        med->pUnkForRelease = nullptr;
        med->tymed = TYMED_HGLOBAL;
        med->hGlobal = BuildHdrop();
        return med->hGlobal ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC* fe, STGMEDIUM* med) override {
        if (!IsHdrop(fe)) return DV_E_FORMATETC;
        if (!med || !med->hGlobal) return E_INVALIDARG;
        HGLOBAL h = BuildHdrop();
        if (!h) return E_OUTOFMEMORY;
        SIZE_T bytes = GlobalSize(h);
        void* src = GlobalLock(h);
        void* dst = GlobalLock(med->hGlobal);
        SIZE_T cap = med->hGlobal ? GlobalSize(med->hGlobal) : 0;
        if (src && dst) memcpy(dst, src, bytes < cap ? bytes : cap);
        if (src) GlobalUnlock(h);
        if (dst) GlobalUnlock(med->hGlobal);
        GlobalFree(h);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* fe) override {
        return IsHdrop(fe) ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* cetc) override {
        if (!cetc) return E_INVALIDARG;
        cetc->cfFormat = CF_HDROP;
        cetc->ptd = nullptr;
        cetc->dwAspect = DVASPECT_CONTENT;
        cetc->lindex = -1;
        cetc->tymed = TYMED_HGLOBAL;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dir, IEnumFORMATETC** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (dir != DATADIR_GET) return E_NOTIMPL;
        *out = new HdropFormatEnum();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD* conn) override {
        if (conn) *conn = 0;
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** out) override {
        if (out) *out = nullptr;
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    HGLOBAL BuildHdrop() const {
        // CF_HDROP 要求文件列表为“双空字符结尾”（路径\0\0），否则目标可能不承接
        SIZE_T bytes = sizeof(DROPFILES) + (path_.size() + 2) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
        if (!h) return nullptr;
        void* p = GlobalLock(h);
        if (!p) {
            GlobalFree(h);
            return nullptr;
        }
        DROPFILES df{};
        df.pFiles = (DWORD)sizeof(DROPFILES);
        df.fWide = TRUE;
        std::memcpy(p, &df, sizeof(df));
        std::memcpy(reinterpret_cast<char*>(p) + sizeof(df), path_.c_str(),
                     (path_.size() + 1) * sizeof(wchar_t));  // 路径 + 单个空；第二个空由 ZEROINIT 提供
        GlobalUnlock(h);
        return h;
    }

    std::wstring path_;
    ULONG ref_;
};

}  // namespace

DWORD StartFileDrag(const std::wstring& path) {
    if (path.empty()) return DROPEFFECT_NONE;
    auto* data = new FileData(path);
    auto* src = new DropSource();
    DWORD effect = DROPEFFECT_NONE;
    DoDragDrop(data, src, DROPEFFECT_MOVE | DROPEFFECT_COPY, &effect);
    data->Release();  // 释放我方引用（DoDragDrop 内部已平衡引用）
    src->Release();
    return effect;
}

}  // namespace lb
