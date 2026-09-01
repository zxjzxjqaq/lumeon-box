#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include <shlobj.h>
#include <objidl.h>
#include "ui/drop_target.hpp"
#include "core/organizer.hpp"

using namespace lb;
namespace fs = std::filesystem;

// 最小 IDataObject 桩：只支持 CF_HDROP 的 GetData/QueryGetData
namespace {

struct StubDataObject : IDataObject {
    HANDLE hdrop = nullptr;
    std::atomic<ULONG> ref_{1};

    StubDataObject() = default;
    explicit StubDataObject(HANDLE h) : hdrop(h) {}

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
    ULONG STDMETHODCALLTYPE Release() override { return --ref_; }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* f, STGMEDIUM* med) override {
        if (!f || f->cfFormat != CF_HDROP || !med) return DV_E_FORMATETC;
        med->tymed = TYMED_HGLOBAL;
        med->hGlobal = static_cast<HGLOBAL>(hdrop);
        med->pUnkForRelease = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* f) override {
        return f && f->cfFormat == CF_HDROP && hdrop ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override {
        out->cfFormat = CF_HDROP;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_NOCONNECTION; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
};

// 构造 CF_HDROP 全局块（双 null 结尾宽字符列表）
HANDLE MakeHdrop(const std::vector<std::wstring>& paths) {
    size_t bytes = sizeof(DROPFILES) + 2;
    for (auto& p : paths) bytes += (p.size() + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    auto* df = static_cast<DROPFILES*>(GlobalLock(g));
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    wchar_t* w = reinterpret_cast<wchar_t*>(reinterpret_cast<uint8_t*>(df) + sizeof(DROPFILES));
    for (auto& p : paths) {
        wcscpy_s(w, p.size() + 1, p.c_str());
        w += p.size() + 1;
    }
    *w = L'\0';
    GlobalUnlock(g);
    return g;
}

struct Desk {
    fs::path dir;
    Desk() {
        dir = fs::temp_directory_path() / L"lumenbox_drop_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
    }
    ~Desk() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

}  // namespace

TEST_CASE("HDROP 提取文件列表", "[drop]") {
    HANDLE h = MakeHdrop({L"C:\\a.txt", L"C:\\bb.png"});
    auto files = DropTarget::FilesFromHdrop(static_cast<HGLOBAL>(h));
    GlobalFree(h);
    REQUIRE(files.size() == 2);
    REQUIRE(files[0] == L"C:\\a.txt");
    REQUIRE(files[1] == L"C:\\bb.png");
}

TEST_CASE("拖拽归位：命中格子才移动并回报类目", "[drop]") {
    Desk d;
    Journal jr((fs::temp_directory_path() / L"lumenbox_tests" / L"drop.jsonl").wstring());
    RuleSet rules;
    Organizer org(d.dir.wstring(), rules, jr);
    std::ofstream(d.dir / L"song.mp3") << 'x';
    std::ofstream(d.dir / L"pic.jpg") << 'x';

    std::vector<std::wstring> got;
    Category gotCat = Category::None;
    int lastDragBox = -99;
    DropTarget dt;
    dt.SetHooks(
        [](const POINTL& pt) { return (pt.x < 100 && pt.y < 100) ? 3 : -1; },
        [](int box) { return box == 3 ? Category::Audio : Category::None; },
        [&](const std::vector<std::wstring>& files, Category c) {
            gotCat = c;
            for (auto& f : files) {
                if (c == Category::None) {
                    if (org.MoveBackToDesktop(f)) got.push_back(f);
                } else if (org.MoveToCategory(f, c)) {
                    got.push_back(f);
                }
            }
        },
        [&](int box) { lastDragBox = box; });

    FORMATETC f{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};

    SECTION("命中格子") {
        HANDLE h = MakeHdrop({(d.dir / L"song.mp3").wstring()});
        StubDataObject obj(h);
        POINTL pt{50, 50};
        DWORD eff = DROPEFFECT_NONE;
        REQUIRE(dt.DragEnter(&obj, 0, pt, &eff) == S_OK);
        REQUIRE(eff == DROPEFFECT_MOVE);
        REQUIRE(lastDragBox == 3);
        eff = 0;
        REQUIRE(dt.DragOver(0, pt, &eff) == S_OK);
        REQUIRE(eff == DROPEFFECT_MOVE);
        REQUIRE(dt.Drop(&obj, 0, pt, &eff) == S_OK);
        // 块所有权随 Drop 消费，不再 GlobalFree
        REQUIRE(got.size() == 1);
        REQUIRE(gotCat == Category::Audio);
        REQUIRE(fs::exists(d.dir / L"LumenBox" / L"音频" / L"song.mp3"));
    }

    SECTION("空白处拖放 = 放回桌面") {
        // 先归位到盒子，再空白处松手 → 应放回桌面根
        REQUIRE(org.MoveToCategory((d.dir / L"pic.jpg").wstring(), Category::Doc));
        REQUIRE(fs::exists(d.dir / L"LumenBox" / L"文档" / L"pic.jpg"));
        HANDLE h = MakeHdrop({(d.dir / L"LumenBox" / L"文档" / L"pic.jpg").wstring()});
        StubDataObject obj(h);
        POINTL pt{500, 500};  // box_at 返回 -1（非卡片）
        DWORD eff = 0;
        REQUIRE(dt.DragEnter(&obj, 0, pt, &eff) == S_OK);
        REQUIRE(eff == DROPEFFECT_MOVE);
        REQUIRE(dt.Drop(&obj, 0, pt, &eff) == S_OK);
        REQUIRE(eff == DROPEFFECT_MOVE);          // 接受放置
        GlobalFree(h);
        REQUIRE(fs::exists(d.dir / L"pic.jpg"));   // 已放回桌面根
        REQUIRE_FALSE(fs::exists(d.dir / L"LumenBox" / L"文档" / L"pic.jpg"));
    }

    SECTION("非文件拖放 = 忽略") {
        StubDataObject obj;  // 无 HDROP
        POINTL pt{50, 50};
        DWORD eff = DROPEFFECT_COPY;
        REQUIRE(dt.DragEnter(&obj, 0, pt, &eff) == S_OK);
        REQUIRE(eff == DROPEFFECT_NONE);
    }

    SECTION("多文件一批归位") {
        HANDLE h = MakeHdrop({(d.dir / L"song.mp3").wstring(), (d.dir / L"pic.jpg").wstring()});
        StubDataObject obj(h);
        POINTL pt{10, 10};
        DWORD eff = 0;
        dt.DragEnter(&obj, 0, pt, &eff);
        dt.Drop(&obj, 0, pt, &eff);
        // 拖拽归位尊重用户手势：拖到哪个格就进哪类（不按扩展名重分类）
        REQUIRE(got.size() == 2);
        REQUIRE(fs::exists(d.dir / L"LumenBox" / L"音频" / L"song.mp3"));
        REQUIRE(fs::exists(d.dir / L"LumenBox" / L"音频" / L"pic.jpg"));
    }
}

TEST_CASE("DragLeave 清除高亮，引用计数自管", "[drop]") {
    DropTarget dt;
    int lastDragBox = -99;
    dt.SetHooks([](const POINTL&) { return 1; }, [](int) { return Category::Doc; },
                [](const std::vector<std::wstring>&, Category) {}, [&](int b) { lastDragBox = b; });
    HANDLE h = MakeHdrop({L"C:\\a.txt"});
    StubDataObject obj(h);
    POINTL pt{0, 0};
    DWORD eff = 0;
    dt.DragEnter(&obj, 0, pt, &eff);
    REQUIRE(lastDragBox == 1);
    dt.DragLeave();
    REQUIRE(lastDragBox == -1);
    dt.DragEnter(&obj, 0, pt, &eff);
    GlobalFree(h);
    dt.Release();  // 释放后不得崩溃（拖拽源未 Release 对象时同样安全）
}
