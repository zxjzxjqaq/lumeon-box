// 探测 LumenBox 容器窗口状态（含子窗口）：用于 Explorer 重启恢复的验证。
// 用法：lbprobe.exe  → 输出容器窗口 hwnd / 可见性 / 父窗口类名 / 宿主机进程
#include <windows.h>
#include <cstdio>
#include <vector>
#include <string>

struct Hit {
    HWND hwnd;
    HWND parent;
    std::wstring parentClass;
    bool visible;
    DWORD pid;
};

static std::vector<Hit> g_hits;
static DWORD g_target = 0;

static BOOL CALLBACK ChildProc(HWND h, LPARAM) {
    wchar_t cls[256]{};
    GetClassNameW(h, cls, 256);
    if (wcscmp(cls, L"LumenBoxGlass") != 0) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (g_target && pid != g_target) return TRUE;
    Hit hit{};
    hit.hwnd = h;
    hit.parent = GetParent(h);
    hit.visible = IsWindowVisible(h) != FALSE;
    hit.pid = pid;
    if (hit.parent) GetClassNameW(hit.parent, cls, 256);
    hit.parentClass = cls;
    g_hits.push_back(hit);
    return TRUE;
}

static BOOL CALLBACK TopProc(HWND h, LPARAM) {
    EnumChildWindows(h, ChildProc, 0);
    return TRUE;
}

int wmain(int argc, wchar_t** argv) {
    if (argc > 1) g_target = wcstoul(argv[1], nullptr, 10);
    // 顶层窗口也查一遍（独立模式/置底模式下容器是顶层窗口）
    EnumWindows(TopProc, 0);

    if (g_hits.empty()) {
        wprintf(L"CONTAINER=ABSENT\n");
        return 1;
    }
    for (auto& h : g_hits) {
        wprintf(L"CONTAINER=PRESENT hwnd=%p visible=%d parent=%p parentClass=%s pid=%lu\n",
                h.hwnd, h.visible ? 1 : 0, h.parent, h.parentClass.c_str(), h.pid);
    }
    return 0;
}
