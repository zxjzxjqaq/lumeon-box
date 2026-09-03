// deskmenu_probe：诊断「取桌面背景菜单」链条在哪一步失败。
// 只查询 shell，不改桌面、不碰文件。结果写 stdout 与 deskmenu_probe.log。
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <exdisp.h>
#include <objbase.h>
#include <wrl/client.h>
#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace {
FILE* g_log = nullptr;

void Log(const wchar_t* line) {
    wprintf(L"%ls\n", line);
    if (g_log) {
        fputws(line, g_log);
        fputws(L"\n", g_log);
        fflush(g_log);
    }
}

template <typename... A>
void LogF(const wchar_t* fmt, A... a) {
    wchar_t buf[512];
    swprintf_s(buf, fmt, a...);
    Log(buf);
}

void Blank() { Log(L""); }

// 报告一条菜单：QueryContextMenu 后枚举项名/是否子菜单
void ReportMenu(const wchar_t* tag, IContextMenu* cm) {
    HMENU hm = CreatePopupMenu();
    HRESULT hr = cm->QueryContextMenu(hm, 0, 1, 0x7FFF, CMF_NORMAL | CMF_EXPLORE);
    if (FAILED(hr)) {
        LogF(L"[%ls] QueryContextMenu 失败 hr=%08x", tag, (unsigned)hr);
        DestroyMenu(hm);
        return;
    }
    int n = GetMenuItemCount(hm);
    LogF(L"[%ls] QueryContextMenu 成功 -> 共 %d 项", tag, n);
    for (int i = 0; i < n; ++i) {
        wchar_t txt[128] = {};
        GetMenuStringW(hm, i, txt, 128, MF_BYPOSITION);
        HMENU sub = GetSubMenu(hm, i);
        if (sub) {
            int sc = GetMenuItemCount(sub);
            LogF(L"    %d. [子菜单 %d项] %ls", i + 1, sc, txt);
        } else {
            LogF(L"    %d. %ls", i + 1, txt);
        }
    }
    DestroyMenu(hm);
}
}  // namespace

int wmain() {
    if (FAILED(OleInitialize(nullptr))) wprintf(L"OleInitialize failed\n");
    g_log = _wfopen(L"D:\\DSH 桌面整理\\桌面整理\\deskmenu_probe.log", L"wb");
    Log(L"==== deskmenu_probe ====");
    Blank();

    // 链1：IShellWindows -> IShellBrowser -> IShellView -> SVGIO_BACKGROUND
    Log(L"[链1] IShellWindows->FindWindowSW->IShellBrowser->IShellView->SVGIO_BACKGROUND");
    ComPtr<IShellWindows> sw;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&sw));
    bool swOk = SUCCEEDED(hr) && sw;
    LogF(L"  1a CoCreateInstance(CLSID_ShellWindows)  %ls  hr=%08x",
         swOk ? L"OK" : L"FAIL", (unsigned)hr);
    ComPtr<IDispatch> disp;
    if (swOk) {
        VARIANT vEmpty;
        VariantInit(&vEmpty);
        long hd = 0;
        hr = sw->FindWindowSW(&vEmpty, &vEmpty, SWC_DESKTOP, &hd, SWFO_NEEDDISPATCH, &disp);
    }
    bool dispOk = SUCCEEDED(hr) && disp;
    LogF(L"  1b FindWindowSW(SWC_DESKTOP)             %ls  hr=%08x",
         dispOk ? L"OK" : L"FAIL", (unsigned)hr);
    ComPtr<IShellBrowser> browser;
    hr = dispOk ? disp->QueryInterface(IID_PPV_ARGS(&browser)) : E_FAIL;
    bool browserOk = SUCCEEDED(hr) && browser;
    LogF(L"  1c QueryInterface(IShellBrowser)          %ls  hr=%08x",
         browserOk ? L"OK" : L"FAIL", (unsigned)hr);
    ComPtr<IShellView> view;
    hr = browserOk ? browser->QueryActiveShellView(&view) : E_FAIL;
    bool viewOk = SUCCEEDED(hr) && view;
    LogF(L"  1d QueryActiveShellView                   %ls  hr=%08x",
         viewOk ? L"OK" : L"FAIL", (unsigned)hr);
    ComPtr<IContextMenu> cm1;
    hr = viewOk ? view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&cm1)) : E_FAIL;
    bool cm1Ok = SUCCEEDED(hr) && cm1;
    LogF(L"  1e GetItemObject(SVGIO_BACKGROUND)        %ls  hr=%08x",
         cm1Ok ? L"OK" : L"FAIL", (unsigned)hr);
    if (cm1Ok) ReportMenu(L"链1", cm1.Get());
    Blank();

    // 链2：SHGetDesktopFolder -> CreateViewObject(IShellView) -> GetItemObject(SVGIO_BACKGROUND)
    Log(L"[链2] SHGetDesktopFolder->CreateViewObject(IShellView)->SVGIO_BACKGROUND");
    ComPtr<IShellFolder> desktop;
    hr = SHGetDesktopFolder(&desktop);
    bool desktopOk = SUCCEEDED(hr) && desktop;
    LogF(L"  2a SHGetDesktopFolder                     %ls  hr=%08x",
         desktopOk ? L"OK" : L"FAIL", (unsigned)hr);
    ComPtr<IShellView> view2;
    hr = desktopOk ? desktop->CreateViewObject(nullptr, IID_PPV_ARGS(&view2)) : E_FAIL;
    bool view2Ok = SUCCEEDED(hr) && view2;
    LogF(L"  2b CreateViewObject(IShellView)           %ls  hr=%08x",
         view2Ok ? L"OK" : L"FAIL", (unsigned)hr);
    ComPtr<IContextMenu> cm2;
    hr = view2Ok ? view2->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&cm2)) : E_FAIL;
    bool cm2Ok = SUCCEEDED(hr) && cm2;
    LogF(L"  2c GetItemObject(SVGIO_BACKGROUND)        %ls  hr=%08x",
         cm2Ok ? L"OK" : L"FAIL", (unsigned)hr);
    if (cm2Ok) ReportMenu(L"链2", cm2.Get());
    Blank();

    // 链3（参考）：SHGetDesktopFolder -> GetUIObjectOf(0)
    Log(L"[链3参考] SHGetDesktopFolder->GetUIObjectOf(0)");
    ComPtr<IContextMenu> cm3;
    hr = desktopOk
             ? desktop->GetUIObjectOf(nullptr, 0, nullptr, IID_IContextMenu, nullptr,
                                      reinterpret_cast<void**>(cm3.GetAddressOf()))
             : E_FAIL;
    bool cm3Ok = SUCCEEDED(hr) && cm3;
    LogF(L"  3a GetUIObjectOf(0, IID_IContextMenu)     %ls  hr=%08x",
         cm3Ok ? L"OK" : L"FAIL", (unsigned)hr);
    if (cm3Ok) ReportMenu(L"链3", cm3.Get());
    Blank();

    Log(L"==== end ====");
    if (g_log) fclose(g_log);
    OleUninitialize();
    return 0;
}
