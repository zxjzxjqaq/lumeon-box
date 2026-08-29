#include <windows.h>
#include <filesystem>
#include <knownfolders.h>
#include <shlobj.h>
#include "anim/spring_demo.hpp"  // M0 演示保留编译；主流程不再使用
#include "core/autostart.hpp"
#include "core/config.hpp"
#include "core/organizer.hpp"
#include "core/rules.hpp"
#include "core/stage_log.hpp"
#include "shell/desktop_layer.hpp"
#include "ui/desktop_grid.hpp"
#include "ui/drop_target.hpp"
#include "ui/glass_window.hpp"
#include "ui/tray.hpp"

using namespace lb;
namespace fs = std::filesystem;

static std::wstring KnownPath(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &raw))) return L".";
    std::wstring p(raw);
    CoTaskMemFree(raw);
    return p;
}

static int Run(HINSTANCE hInst) {
    StageTrace("0:enter-run");
    // OLE STA：RegisterDragDrop 的前置（拖拽归位 S4）
    if (FAILED(OleInitialize(nullptr))) StageTrace("ole-init-FAILED");

    const std::wstring appData = ConfigStore::default_dir().wstring();
    const std::wstring cfgPath = appData + L"\\config.json";
    const bool fresh = !fs::exists(cfgPath);

    ConfigStore store(cfgPath);
    SpikeConfig cfg = store.load();

    RuleSet rules = RuleSet::Load(appData + L"\\rules.json");
    Journal jr(appData + L"\\journal.jsonl");
    Organizer org(KnownPath(FOLDERID_Desktop), rules, jr);

    GlassWindow win;
    DesktopGrid grid;
    TrayIcon tray;
    Autostart autostart;
    bool visible = true;

    DesktopMode mode = cfg.mode == "bottom"    ? DesktopMode::Bottom
                     : cfg.mode == "independent" && !fresh ? DesktopMode::Independent
                                                           : DesktopMode::DesktopLayer;

    auto doOrganize = [&] {
        org.OrganizeAll();
        grid.Refresh();
        grid.AnimateIn(win.hwnd());
    };
    auto doUndo = [&] {
        org.UndoLast();
        grid.Refresh();
    };
    auto toggleVisible = [&] {
        visible = !visible;
        ShowWindow(win.hwnd(), visible ? SW_SHOW : SW_HIDE);
    };

    auto on_key = [&](UINT id) {
        if (id == 1) {  // Ctrl+Alt+1：贴桌 ↔ 独立（失败转置底）
            StageTrace("k:ca1");
            if (mode == DesktopMode::Independent) {
                if (AttachToDesktopLayer(win.hwnd())) {
                    mode = DesktopMode::DesktopLayer;
                } else {
                    UseBottomMode(win.hwnd());
                    mode = DesktopMode::Bottom;
                }
            } else if (RestoreIndependent(win.hwnd())) {
                mode = DesktopMode::Independent;
            }
            cfg.mode = mode == DesktopMode::DesktopLayer ? "desktop"
                     : mode == DesktopMode::Bottom       ? "bottom"
                                                         : "independent";
        } else if (id == 2) {  // Ctrl+Alt+2：一键整理
            StageTrace("k:ca2-organize");
            doOrganize();
        }
    };

    if (!win.Create(hInst, L"LumenBox", cfg, on_key)) return 1;
    StageTrace("1:window-created");
    if (!RegisterHotKey(win.hwnd(), 1, MOD_CONTROL | MOD_ALT, '1'))
        StageTrace("hotkey-CA1-register-FAILED");
    if (!RegisterHotKey(win.hwnd(), 2, MOD_CONTROL | MOD_ALT, '2'))
        StageTrace("hotkey-CA2-register-FAILED");
    win.OnTimer([&](HWND h, UINT id) { grid.OnTimer(h, id); });
    win.OnMouse([&](UINT msg, float x, float y) { grid.OnMouse(win.hwnd(), msg, x, y); });
    win.OnHitTest([&](float x, float y) {
        // 全窗 HTCLIENT：OLE 拖放周期性用 WM_NCHITTEST 复验目标，HTTRANSPARENT
        // 会让复验转投图标层导致拖放丢失（实测）。窗口紧贴格子内容，空白仅
        // 14px 格间距，放弃穿透换取拖放可靠；图标层交互不受影响（窗口外）。
        (void)x; (void)y;
        return HTCLIENT;
    });

    grid.SetOrg(&org);
    grid.Init(win.gfx(), win.Root(), win.DpiScale());
    grid.SetOnExtent([&](float w, float h) {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int margin = 24;
        int wd = static_cast<int>(w * win.DpiScale()) + 2;
        int ht = static_cast<int>(h * win.DpiScale()) + 2;
        SetWindowPos(win.hwnd(), nullptr, work.right - wd - margin,
                     work.bottom - ht - margin, wd, ht,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        StageTrace("l:extent-applied");
    });
    grid.Refresh();
    StageTrace("2:grid-ready");

    // 拖拽归位：资源管理器拖文件到格子 → MoveToCategory
    DropTarget dropTarget;
    dropTarget.SetHooks(
        [&](const POINTL& pt) {
            POINT c{pt.x, pt.y};
            ScreenToClient(win.hwnd(), &c);
            return grid.BoxAt(c.x / win.DpiScale() * 96.f, c.y / win.DpiScale() * 96.f);
        },
        [&](int box) { return grid.CategoryAt(box); },
        [&](const std::vector<std::wstring>& files, Category cat) {
            for (auto& f : files) org.MoveToCategory(f, cat);
            grid.Refresh();
        },
        [&](int box) { grid.SetDragBox(box); });
    dropTarget.SetWindow(win.hwnd());
    if (FAILED(RegisterDragDrop(win.hwnd(), &dropTarget))) StageTrace("drag-reg-FAILED");

    if (!tray.Add(win.hwnd(), WM_APP + 1, LoadIconW(nullptr, IDI_APPLICATION),
                  L"LumenBox 桌面整理"))
        StageTrace("tray-add-FAILED");

    win.OnApp([&](UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
        if (msg == WM_APP + 1) {
            if (LOWORD(lp) == WM_RBUTTONUP) {
                SetForegroundWindow(win.hwnd());
                HMENU m = CreatePopupMenu();
                AppendMenuW(m, MF_STRING, 1, L"一键整理");
                AppendMenuW(m, MF_STRING, 2, L"撤销上次整理");
                AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(m, MF_STRING | (autostart.IsEnabled() ? MF_CHECKED : 0u), 4,
                            L"开机自启");
                AppendMenuW(m, MF_STRING, 3, L"显示 / 隐藏");
                AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(m, MF_STRING, 9, L"退出");
                POINT pt;
                GetCursorPos(&pt);
                int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                         pt.x, pt.y, 0, win.hwnd(), nullptr);
                DestroyMenu(m);
                switch (cmd) {
                case 1: doOrganize(); break;
                case 2: doUndo(); break;
                case 3: toggleVisible(); break;
                case 4:
                    autostart.IsEnabled() ? autostart.Disable() : autostart.Enable();
                    break;
                case 9: DestroyWindow(win.hwnd()); break;
                }
            } else if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
                toggleVisible();
            }
        }
        return 0;
    });

    // 启动挂靠：默认贴桌（v0.4 D2 决策），失败转置底
    if (mode == DesktopMode::DesktopLayer && !AttachToDesktopLayer(win.hwnd())) {
        UseBottomMode(win.hwnd());
        mode = DesktopMode::Bottom;
    } else if (mode == DesktopMode::Bottom) {
        UseBottomMode(win.hwnd());
    }
    // 同步实际启动模式，退出时持久化的是真实状态（热键切换路径自行更新）
    cfg.mode = mode == DesktopMode::DesktopLayer ? "desktop"
             : mode == DesktopMode::Bottom       ? "bottom"
                                                 : "independent";
    StageTrace("3:mode-restored");

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    StageTrace("6:loop-exited");
    RevokeDragDrop(win.hwnd());
    store.save(cfg);
    tray.Remove();
    OleUninitialize();
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

    int exit_code = 0;
    std::string err;
    try {
        exit_code = Run(hInst);
    } catch (std::exception const& e) {
        exit_code = 2;
        err = e.what();
    }
    if (!err.empty()) MessageBoxA(nullptr, err.c_str(), "LumenBox 异常", MB_ICONERROR);
    return exit_code;
}
