// drag_probe: 以真实 OLE 拖拽源发起 DoDragDrop，验证落点窗口。
// 用法: drag_probe.exe <file> [x y] [logPath]
// 关键约束：SetCapture 只授予收到鼠标按下的线程——必须把鼠标按下点在探针自己的
// 可见窗口上，在 WM_LBUTTONDOWN 里发起 DoDragDrop，再由脚本驱动移动/释放。
#include <windows.h>
#include <shlobj.h>
#include <atomic>
#include <cstdio>
#include <string>

#pragma comment(lib, "ole32")
#pragma comment(lib, "shell32")

namespace {

wchar_t g_logPath[MAX_PATH] = L"drag_probe_out.txt";

void Log(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    FILE* f = nullptr;
    if (!_wfopen_s(&f, g_logPath, L"a") && f) {
        fwprintf(f, L"[%lu] ", GetTickCount());
        vfwprintf(f, fmt, args);
        fwprintf(f, L"\n");
        fclose(f);
    }
    va_end(args);
}

struct ProbeDropSource : IDropSource {
    std::atomic<ULONG> ref_{1};
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
    ULONG STDMETHODCALLTYPE Release() override { return --ref_; }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL esc, DWORD state) override {
        if (esc) return DRAGDROP_S_CANCEL;
        if (state & MK_LBUTTON) return S_OK;
        return DRAGDROP_S_DROP;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};

IDataObject* g_data = nullptr;
ProbeDropSource* g_source = nullptr;
bool g_dragStarted = false;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_LBUTTONDOWN: {
        if (g_dragStarted) break;
        g_dragStarted = true;
        Log(L"WM_LBUTTONDOWN -> DoDragDrop");
        DWORD effect = 0;
        HRESULT hr = DoDragDrop(g_data, g_source, DROPEFFECT_MOVE | DROPEFFECT_COPY, &effect);
        Log(L"DoDragDrop hr=0x%08X effect=%lu", (unsigned)hr, (unsigned)effect);
        PostQuitMessage(0);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        wprintf(L"usage: drag_probe <file> [x y] [logPath]\n");
        return 1;
    }
    std::wstring file = argv[1];
    int x = argc > 3 ? _wtoi(argv[2]) : 2280;
    int y = argc > 3 ? _wtoi(argv[3]) : 660;
    if (argc > 4) wcscpy_s(g_logPath, argv[4]);
    DeleteFileW(g_logPath);
    Log(L"probe start");

    if (FAILED(OleInitialize(nullptr))) {
        Log(L"OleInitialize FAILED");
        return 1;
    }
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(file.c_str(), nullptr, &pidl, 0, nullptr)) || !pidl) {
        Log(L"SHParseDisplayName FAILED");
        return 2;
    }
    // IShellItem::BindToHandler(BHID_DataObject) —— 与资源管理器真实拖拽同源
    IShellItem* item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(file.c_str(), nullptr, IID_PPV_ARGS(&item))) || !item) {
        Log(L"SHCreateItemFromParsingName FAILED");
        return 2;
    }
    if (FAILED(item->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&g_data))) || !g_data) {
        Log(L"BHID_DataObject FAILED");
        return 2;
    }
    item->Release();
    ILFree(pidl);
    g_source = new ProbeDropSource();

    // 诊断：枚举数据对象支持的格式
    IEnumFORMATETC* en = nullptr;
    HRESULT ehr = g_data->EnumFormatEtc(DATADIR_GET, &en);
    if (SUCCEEDED(ehr) && en) {
        FORMATETC fe[32];
        ULONG got = 0;
        HRESULT nhr = en->Next(32, fe, &got);
        Log(L"enum hr=0x%08X got=%lu", (unsigned)nhr, (unsigned)got);
        for (ULONG i = 0; i < got; ++i) {
            wchar_t name[64] = L"?";
            GetClipboardFormatNameW(fe[i].cfFormat, name, 64);
            Log(L"fmt cf=%u name=%s tymed=%lu", fe[i].cfFormat, name, (unsigned)fe[i].tymed);
        }
        en->Release();
    } else {
        Log(L"EnumFormatEtc FAILED hr=0x%08X", (unsigned)ehr);
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
    wc.lpszClassName = L"LumenBoxDragProbe";
    RegisterClassW(&wc);
    // 可见小窗（拖拽起点），WS_EX_TOPMOST 保证可点
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"DRAG", WS_POPUPWINDOW | WS_CAPTION,
                                x, y, 72, 60, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        Log(L"CreateWindow FAILED %lu", GetLastError());
        return 2;
    }
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    Log(L"window ready at %d,%d", x, y);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    Log(L"probe exit");
    OleUninitialize();
    return 0;
}
