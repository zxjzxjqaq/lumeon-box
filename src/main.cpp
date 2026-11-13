#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <knownfolders.h>
#include <shlobj.h>
#include "anim/spring_demo.hpp"  // M0 演示保留编译；主流程不再使用
#include "core/appindex.hpp"
#include "core/autostart.hpp"
#include "core/clipboard_monitor.hpp"
#include "core/clipboard_store.hpp"
#include "core/config.hpp"
#include "core/organizer.hpp"
#include "core/rules.hpp"
#include "core/stage_log.hpp"
#include "core/strutil.hpp"
#include "core/watcher.hpp"
#include "resource.h"
#include "shell/desktop_icons.hpp"
#include "shell/desktop_layer.hpp"
#include "ui/clip_panel.hpp"
#include "ui/desktop_grid.hpp"
#include "ui/drop_target.hpp"
#include "ui/glass_window.hpp"
#include "ui/hint.hpp"
#include "ui/spotlight.hpp"
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

// 常驻宿主窗口：不属于桌面层、不会被 Explorer 重启销毁，专用于在容器窗口消失后
// 仍持续派发健康检查定时器与 TaskbarCreated 广播（容器被级联销毁时其窗口级定时器
// 与消息都会一起消失，恢复逻辑将永远没机会执行）。
struct HostCtx {
    std::function<bool()>* recover = nullptr;   // 容器重建
    std::function<void()>* resync = nullptr;    // 托盘/桌面层重挂
    UINT taskbarCreated = 0;
    HWND container = nullptr;
    bool* quitRequested = nullptr;
    bool independent = false;                   // 独立模式：不检查父宿主
};

static LRESULT CALLBACK HostProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = reinterpret_cast<HostCtx*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(h, msg, wp, lp);
    }
    if (!c) return DefWindowProcW(h, msg, wp, lp);
    if (msg == c->taskbarCreated && c->taskbarCreated) {
        if (c->resync && *c->resync) (*c->resync)();
        return 0;
    }
    if (msg == WM_TIMER && wp == 9) {
        HWND cur = c->container;
        if (!cur || !IsWindow(cur)) {
            StageTrace("h:container-gone");
            if (c->recover && *c->recover) (*c->recover)();
            return 0;
        }
        if (!c->independent) {  // 贴桌/置底：父宿主必须还在（Explorer 重启会换新）
            HWND par = GetParent(cur);
            if (!par || !IsWindow(par)) {
                StageTrace("h:host-gone");
                if (c->recover && *c->recover) (*c->recover)();
            }
        }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// 查找已运行实例的容器窗口：贴桌/置底模式下它是 WorkerW 的子窗口，FindWindowW 只找
// 顶层窗口必然失败——必须同时枚举顶层与子窗口（否则正常二次启动会被误判为僵尸实例）。
static BOOL CALLBACK FindByPidProc(HWND h, LPARAM lp) {
    auto* ctx = reinterpret_cast<std::pair<DWORD, HWND>*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == ctx->first) {
        wchar_t cls[64]{};
        GetClassNameW(h, cls, 64);
        if (wcscmp(cls, L"LumenBoxGlass") == 0) {
            ctx->second = h;
            return FALSE;
        }
    }
    return TRUE;
}

// 检测「已有实例」：宿主窗口（LumenBoxHost，常驻且顶层）存在即算在运行。
static bool ExistingInstanceAlive() {
    if (HWND h = FindWindowW(L"LumenBoxHost", nullptr)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid && pid != GetCurrentProcessId()) return true;
    }
    return false;
}

static int Run(HINSTANCE hInst, const wchar_t* cmd) {
    StageTrace("0:enter-run");
    // OLE STA：RegisterDragDrop 的前置（拖拽归位 S4）
    if (FAILED(OleInitialize(nullptr))) StageTrace("ole-init-FAILED");
    const std::wstring appData = ConfigStore::default_dir().wstring();
    const std::wstring cfgPath = appData + L"\\config.json";

    ConfigStore store(cfgPath);
    SpikeConfig cfg = store.load();

    const std::wstring desktop = KnownPath(FOLDERID_Desktop);
    // 容器根：%APPDATA%\LumenBox（=appData）。格子=其下 <类目>\ 子文件夹；不再放在桌面。
    const std::wstring boxRoot = appData;
    RuleSet rules = RuleSet::Load(appData + L"\\rules.json");
    Journal jr(appData + L"\\journal.jsonl");
    Organizer org(desktop, boxRoot, rules, jr);

    GlassWindow win;
    DesktopGrid grid;
    TrayIcon tray;
    Autostart autostart;
    HintToast hint;
    Watcher watcher;
    Watcher boxWatcher;                        // 监视容器根（递归）→ 外部改动/还原时刷新容器
    int boxWatcherMsg = 0;                     // 0=未启动
    std::vector<std::wstring> pendingBatch;   // watcher 批次的累积（UI 线程防抖）
    bool watcherStarted = false;              // watcher 线程是否已运行
    bool visible = true;
    std::function<bool()> recoverFromShellRestart;  // Explorer 重启恢复（定义于下方）
    std::function<void()> resyncShell;              // 托盘/桌面层重挂（定义于下方）

    // M2：应用索引启动时读一次并缓存（快速搜索用）；不建全盘索引
    AppIndex appIndex;
    appIndex.Build();
    Spotlight spot;
    spot.SetApps(&appIndex);
    spot.SetFilesFn([&]() -> std::vector<std::wstring> {
        // 只搜「容器内」的内容：各数据容器文件夹（跳过待办盒与根目录 config/tasks）。
        // 文件与文件夹都作为候选命中项（不向下递归目录）。
        std::vector<std::wstring> out;
        std::error_code ec;
        int n = grid.BoxCount();
        for (int i = 0; i < n; ++i) {
            if (grid.IsTasksAt(i)) continue;
            std::wstring folder = grid.FolderAt(i);
            if (folder.empty()) continue;
            std::wstring dir = org.FolderDir(folder);
            for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec))
                out.push_back(it->path().wstring());
        }
        return out;
    });

    // 用户要求：容器作为桌面层挂载（盖在图标上），并随桌面常驻——「显示桌面/Win+D」也不消失。
    // 顶层浮窗(independent/bottom)会被显示桌面最小化，故一律默认挂到桌面层(DesktopLayer)。
    DesktopMode mode = DesktopMode::DesktopLayer;

    // M5：剪贴板历史（设计文档 D7–D9）。store 先 Load，面板/监听共享同一份。
    ClipStore clipStore(appData + L"\\clipboard");
    clipStore.Load();
    ClipboardMonitor clipMon;
    ClipPanel clipPanel;
    clipPanel.SetStore(&clipStore);
    clipPanel.SetCopyFn([&](const ClipEntry& e) {
        return CopyClipToClipboard(
            win.hwnd(), e,
            [&](const std::string& fmt, std::string& bytes) {
                return clipStore.LoadPayload(e, fmt, bytes);
            },
            [&](std::vector<std::wstring>& out) {  // 多图条目：全部 png blob 路径
                for (auto& p : e.payloads)
                    if (p.fmt == "png" && !p.file.empty())
                        out.push_back(clipStore.BlobPath(e, p));
            });
    });
    clipPanel.SetPausedApi(
        [&] { return cfg.clipboardPaused; },
        [&](bool p) {
            cfg.clipboardPaused = p;
            clipMon.SetPaused(p);
            store.save(cfg);
        });

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
        } else if (id == 3) {  // 双击隐藏的热键回退路径（hideIconsMode=hotkey）
            StageTrace("k:ca3-hideicons");
            ToggleDesktopIcons();
        } else if (id == 4) {  // Ctrl+Space：快速搜索
            StageTrace("k:ca4-spotlight");
            spot.Toggle();
        } else if (id == 5) {  // Ctrl+B：剪贴板历史面板
            StageTrace("k:cb-clip");
            clipPanel.Toggle();
        }
    };

    if (!win.Create(hInst, L"LumenBox", cfg, on_key)) return 1;
    StageTrace("1:window-created");
    if (!RegisterHotKey(win.hwnd(), 1, MOD_CONTROL | MOD_ALT, '1'))
        StageTrace("hotkey-CA1-register-FAILED");
    bool ctrlSpaceFailed = false;
    if (!RegisterHotKey(win.hwnd(), 4, MOD_CONTROL | MOD_NOREPEAT, VK_SPACE)) {
        StageTrace("hotkey-ctrl-space-FAILED");
        ctrlSpaceFailed = true;
    }
    // M5：剪贴板历史监听（须在主线程 Start：message 窗口挂主循环）+ Ctrl+B 热键（D7）
    bool clipHotkeyFailed = false;
    if (cfg.clipboardEnabled) {
        ClipboardOpts clipOpts;
        clipOpts.maxEntries = cfg.clipboardMaxEntries;
        clipOpts.blobMaxMB = static_cast<uint64_t>(cfg.clipboardBlobMB);
        clipOpts.recordSource = cfg.clipboardRecordSource;
        if (clipMon.Start(&clipStore, win.hwnd(), WM_APP + 6, clipOpts))
            clipMon.SetPaused(cfg.clipboardPaused);
        else
            StageTrace("clipmon-start-FAILED");
        if (!RegisterHotKey(win.hwnd(), 5, MOD_CONTROL | MOD_NOREPEAT, 'B')) {
            if (RegisterHotKey(win.hwnd(), 5, MOD_CONTROL | MOD_ALT, 'B'))
                StageTrace("hotkey-ctrl-b-FAILED-fallback-ctrl-alt-b");
            else {
                StageTrace("hotkey-clip-register-FAILED");
                clipHotkeyFailed = true;
            }
        }
    }
    // 双击隐藏图标：默认钩子（D6），失败或热键模式走 Ctrl+Alt+H
    if (cfg.hideIconsMode == "hook") {
        if (InstallHideHook(win.hwnd(), WM_APP + 3)) {
            StageTrace("hide-hook-OK");
        } else {
            StageTrace("hide-hook-FAILED-fallback-hotkey");
            if (!RegisterHotKey(win.hwnd(), 3, MOD_CONTROL | MOD_ALT, 'H'))
                StageTrace("hotkey-H-register-FAILED");
        }
    } else {  // hotkey
        if (!RegisterHotKey(win.hwnd(), 3, MOD_CONTROL | MOD_ALT, 'H'))
            StageTrace("hotkey-H-register-FAILED");
    }

    // M2：watcher 批次在 UI 线程做 500ms 防抖合并 → 过滤 → auto 归位 / prompt 提示
    auto flushPending = [&] {
        KillTimer(win.hwnd(), 3);
        if (pendingBatch.empty()) return;
        std::vector<std::wstring> batch;
        batch.swap(pendingBatch);
        std::sort(batch.begin(), batch.end());
        batch.erase(std::unique(batch.begin(), batch.end()), batch.end());
        auto landings = SuggestLandings(batch, rules, boxRoot);
        if (landings.empty()) return;
        if (cfg.watcherMode == "auto") {
            for (auto& l : landings) org.MoveToCategory(l.path, l.cat);
            grid.Refresh();
        } else {
            hint.Show(std::move(landings),
                      [&](std::vector<FileLanding> ls) {
                          for (auto& l : ls) org.MoveToCategory(l.path, l.cat);
                          grid.Refresh();
                      },
                      [] {});
        }
    };
    win.OnTimer([&](HWND h, UINT id) {
        if (id == 3) { flushPending(); return; }
        if (id == 9) {  // 桌面层健康检查：宿主 WorkerW 消失/窗口脱离则重挂或重建
            static HWND lastHost = nullptr;
            HWND cur = win.hwnd();
            bool needRepair = false;
            if (!cur || !IsWindow(cur)) {
                needRepair = true;  // 窗口已销毁（未走 WM_DESTROY 的非常规路径）
            } else if (mode != DesktopMode::Independent) {
                // 贴桌/置底模式：确认父宿主仍在。宿主被 Explorer 销毁后本窗口会变成
                // 孤儿（IsWindow 为真但不再显示、拖放/点击失效）——必须重建。
                HWND par = GetParent(cur);
                if (!par || !IsWindow(par)) {
                    needRepair = true;
                } else {
                    lastHost = par;
                    RECT r{};
                    if (GetWindowRect(cur, &r) && !IsWindowVisible(cur) && visible) {
                        ShowWindow(cur, SW_SHOW);  // 仅隐藏：直接显示即可
                    }
                }
            }
            if (needRepair && !recoverFromShellRestart()) {
                win.SetQuitOnDestroy(true);
                if (win.hwnd()) DestroyWindow(win.hwnd());
            }
            return;
        }
        grid.OnTimer(h, id);
    });
    win.OnMouse([&](UINT msg, float x, float y) { grid.OnMouse(win.hwnd(), msg, x, y); });
    win.OnWheel([&](int delta, float x, float y) { grid.OnWheel(delta, x, y); });
    win.OnKeyboard([&](UINT msg, UINT wp) { return grid.OnKeyboard(msg, wp); });
    win.OnMenu([&](UINT msg, WPARAM wp, LPARAM lp) { return grid.OnShellMenuMsg(msg, wp, lp); });
    win.OnHitTest([&](float x, float y) {
        // 全窗 HTCLIENT：OLE 拖放周期性用 WM_NCHITTEST 复验目标，HTTRANSPARENT
        // 会让复验转投图标层导致拖放丢失（实测）。窗口紧贴格子内容，空白仅
        // 14px 格间距，放弃穿透换取拖放可靠；图标层交互不受影响（窗口外）。
        (void)x; (void)y;
        return HTCLIENT;
    });
    win.OnDpiChanged([&](float scale) { grid.SetDpi(scale); });  // 跨显示器 DPI 变更 → 网格重缩放

    grid.SetOrg(&org);
    grid.SetTasksFile(appData + L"\\tasks.json");  // 日程格数据（待办盒）— 须在 Init 前，Init 内部 LoadTasks
    grid.Init(win.gfx(), win.Root(), win.DpiScale());
    grid.SetBoxes(cfg.boxes);
    grid.SetWindowHandle(win.hwnd());
    grid.SetOnExtent([&](float w, float h) {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int margin = 24;
        int wd = static_cast<int>(w * win.DpiScale()) + 2;
        int ht = static_cast<int>(h * win.DpiScale()) + 2;
        int x = work.right - wd - margin;
        int y = work.bottom - ht - margin;
        if (!cfg.anchored) {  // 用户手动摆放过：保持左上角，尺寸变化时夹回工作区
            RECT cur{};
            GetWindowRect(win.hwnd(), &cur);
            const int lo_x = static_cast<int>(work.left);
            const int hi_x = work.right - wd - margin;
            const int lo_y = static_cast<int>(work.top);
            const int hi_y = work.bottom - ht - margin;
            x = std::min(std::max(static_cast<int>(cur.left), lo_x), hi_x);
            y = std::min(std::max(static_cast<int>(cur.top), lo_y), hi_y);
        }
        SetWindowPos(win.hwnd(), nullptr, x, y, wd, ht, SWP_NOZORDER | SWP_NOACTIVATE);
        StageTrace("l:extent-applied");
    });
    // 自定义容器：盒子清单变化（新增/重排/重命名/折叠）→ 持久化 cfg.boxes
    grid.SetOnBoxesChanged([&] { cfg.boxes = grid.BoxesOut(); store.save(cfg); });
    grid.SetOnClearBox([&](int boxIdx) {
        std::wstring folder = grid.FolderAt(boxIdx);
        if (!folder.empty())
            for (auto& p : org.FilesInFolder(folder)) org.MoveBackToDesktop(p);
        grid.Refresh();
    });
    grid.SetOnMoved([&](bool window_moved) {  // 拖动结束：立即持久化
        cfg.boxes = grid.BoxesOut();
        RECT r{};
        GetWindowRect(win.hwnd(), &r);
        cfg.x = r.left;
        cfg.y = r.top;
        cfg.w = r.right - r.left;
        cfg.h = r.bottom - r.top;
        if (window_moved) cfg.anchored = false;  // 仅整窗拖动改变锚定态
        store.save(cfg);
    });
    grid.Refresh();
    StageTrace("2:grid-ready");

    // 容器目录实时监视：外部改动 / 回收站还原时刷新对应容器（还原把文件放回容器目录）
    boxWatcherMsg = WM_APP + 5;
    if (boxWatcher.Start(appData, win.hwnd(), boxWatcherMsg, /*recursive*/ true)) {
        StageTrace("2:box-watch-on");
    }

    // 拖拽归位：资源管理器拖文件到盒子 → 进该盒文件夹；非卡片区 → 放回桌面
    DropTarget dropTarget;
    dropTarget.SetHooks(
        [&](const POINTL& pt) {
            POINT c{pt.x, pt.y};
            ScreenToClient(win.hwnd(), &c);
            // 客户端像素 → DIP（与 OnMouse 的 x/dpi*96 同空间）；勿再乘 96
            return grid.BoxAt(c.x / win.DpiScale(), c.y / win.DpiScale());
        },
        [&](const std::vector<std::wstring>& files, int box, DWORD effect) {
            if (box >= 0 && grid.IsTasksAt(box)) {  // 待办盒：不接受文件拖入
                grid.Refresh();
                return;
            }
            const bool copy = (effect & DROPEFFECT_COPY) != 0;  // Ctrl 拖拽=复制（保留源）
            for (auto& f : files) {
                if (box < 0)  // 落在非卡片区 = 放回桌面
                    org.MoveBackToDesktop(f);
                else if (copy)
                    org.CopyToFolder(f, grid.FolderAt(box));
                else
                    org.MoveToFolder(f, grid.FolderAt(box));
            }
            grid.Refresh();
        },
        [&](int box) { grid.SetDragBox(box); });
    dropTarget.SetWindow(win.hwnd());
    if (FAILED(RegisterDragDrop(win.hwnd(), &dropTarget))) StageTrace("drag-reg-FAILED");

    if (!tray.Add(win.hwnd(), WM_APP + 1, LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP)),
                  L"LumenBox 桌面整理"))
        StageTrace("tray-add-FAILED");

    // 常驻宿主窗口（见 HostProc 说明）：容器被 Explorer 级联销毁后，靠它继续派发
    // 健康检查与 TaskbarCreated 广播，实现自动恢复（不再变僵尸进程）。
    // 须在 resyncShell/recoverFromShellRestart 之前创建（二者引用 hostCtx）。
    WNDCLASSEXW hwc{};
    hwc.cbSize = sizeof(hwc);
    hwc.lpfnWndProc = &HostProc;
    hwc.hInstance = hInst;
    hwc.lpszClassName = L"LumenBoxHost";
    RegisterClassExW(&hwc);
    UINT wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    bool quitRequested = false;
    HostCtx hostCtx;
    hostCtx.recover = &recoverFromShellRestart;
    hostCtx.resync = &resyncShell;
    hostCtx.taskbarCreated = wmTaskbarCreated;
    hostCtx.container = win.hwnd();
    hostCtx.quitRequested = &quitRequested;
    HWND hostWnd = CreateWindowExW(0, L"LumenBoxHost", L"LumenBoxHost", 0, 0, 0, 0, 0,
                                   HWND_MESSAGE, nullptr, hInst, &hostCtx);
    if (!hostWnd) {
        StageTrace("hostwnd-create-FAILED");
    } else {
        StageTrace("hostwnd-ready");
        SetTimer(hostWnd, 9, 1500, nullptr);  // 健康检查（挂常驻窗口，容器死也照跑）
    }

    // Explorer 重启恢复（见 HostProc 说明）：重建主窗口 + 重挂全部窗口级资源
    resyncShell = [&] {
        StageTrace("r:shell-resync");
        tray.Add(win.hwnd(), WM_APP + 1, LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP)),
                 L"LumenBox 桌面整理");
        if (mode == DesktopMode::DesktopLayer) {
            if (!AttachToDesktopLayer(win.hwnd())) UseBottomMode(win.hwnd());
        } else if (mode == DesktopMode::Bottom) {
            UseBottomMode(win.hwnd());
        }
        grid.Refresh();
    };
    recoverFromShellRestart = [&]() -> bool {
        StageTrace("r:shell-restart-rebuild");
        Sleep(150);  // 等 Explorer 把新 WorkerW 建出来
        if (!win.Create(hInst, L"LumenBox", cfg, on_key)) {
            StageTrace("r:recreate-FAILED");
            return false;  // 重建失败 → 正常退出，不留僵尸
        }
        HWND h = win.hwnd();
        hostCtx.container = h;
        RemoveHideHook();
        if (cfg.hideIconsMode == "hook" && !InstallHideHook(h, WM_APP + 3))
            StageTrace("r:hook-reinstall-FAILED");
        RegisterHotKey(h, 1, MOD_CONTROL | MOD_ALT, '1');
        RegisterHotKey(h, 4, MOD_CONTROL | MOD_NOREPEAT, VK_SPACE);
        if (cfg.clipboardEnabled &&
            !RegisterHotKey(h, 5, MOD_CONTROL | MOD_NOREPEAT, 'B'))
            RegisterHotKey(h, 5, MOD_CONTROL | MOD_ALT, 'B');
        if (cfg.hideIconsMode != "hook") RegisterHotKey(h, 3, MOD_CONTROL | MOD_ALT, 'H');
        clipMon.SetNotifyWindow(h);
        // boxWatcher 原指向已销毁的旧窗口：重启监听挂到新 HWND，
        // 否则 Explorer 重启后「容器外部改动/回收站还原实时刷新」静默失效。
        boxWatcher.Stop();
        boxWatcher.Start(appData, h, boxWatcherMsg, /*recursive*/ true);
        dropTarget.SetWindow(h);
        RegisterDragDrop(h, &dropTarget);
        grid.SetWindowHandle(h);
        grid.Init(win.gfx(), win.Root(), win.DpiScale());
        grid.Refresh();
        if (mode == DesktopMode::DesktopLayer && !AttachToDesktopLayer(h))
            UseBottomMode(h);
        else if (mode == DesktopMode::Bottom)
            UseBottomMode(h);
        StageTrace("r:recreate-OK");
        return true;
    };
    win.OnDestroyed([&]() -> bool { return recoverFromShellRestart(); });
    // §5.1 P0：热键冲突一次性提示（托盘气泡，不阻塞主流程）
    if (ctrlSpaceFailed)
        tray.Balloon(L"热键冲突", L"Ctrl+Space 被占用，快速搜索暂不可用（可在设置后重试）。");
    if (clipHotkeyFailed)
        tray.Balloon(L"热键冲突", L"Ctrl+B 被占用，剪贴板历史热键暂不可用（托盘菜单仍可打开）。");

    win.OnApp([&](UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
        if (msg == WM_APP + 6) {  // 剪贴板新条目落库 → 面板打开时实时刷新
            clipPanel.OnStoreChanged();
            return 0;
        }
        if (msg == WM_APP + 7) {  // 图片超限被拒（一次性气泡，不阻塞）
            static bool warned = false;
            if (!warned) {
                warned = true;
                tray.Balloon(L"剪贴板历史", L"有图片过大未被记录（超过 8K 像素或 32MB 限制）。");
            }
            return 0;
        }
        if (msg == WM_APP + 3) {  // 双击桌面空白 → 切换图标显隐（钩子路径）
            ToggleDesktopIcons();
            return 0;
        }
        if (msg == WM_APP + 5) {  // 容器目录内容变化（外部增删/回收站还原）→ 刷新受影响容器
            auto* batch = reinterpret_cast<std::vector<std::wstring>*>(lp);
            if (batch) {
                bool hit = false;
                for (auto& p : *batch) {
                    for (int i = 0; i < grid.BoxCount(); ++i) {
                        if (grid.IsTasksAt(i)) continue;
                        std::wstring pref = org.FolderDir(grid.FolderAt(i)) + L"\\";
                        if (_wcsnicmp(p.c_str(), pref.c_str(), pref.size()) == 0) {
                            hit = true;
                            break;
                        }
                    }
                    if (hit) break;
                }
                delete batch;
                if (hit) grid.Refresh();
            }
            return 0;
        }
        if (msg == WM_APP + 2) {  // watcher 批次：UI 线程累积，防抖后处理
            auto* batch = reinterpret_cast<std::vector<std::wstring>*>(lp);
            if (batch) {
                pendingBatch.insert(pendingBatch.end(), batch->begin(), batch->end());
                delete batch;
                if (cfg.watcherMode != "off")
                    SetTimer(win.hwnd(), 3, 500, nullptr);  // 500ms 静默后合并处理
            }
            return 0;
        }
        if (msg == WM_APP + 1) {
            if (LOWORD(lp) == WM_RBUTTONUP) {
                SetForegroundWindow(win.hwnd());
                HMENU m = CreatePopupMenu();
                AppendMenuW(m, MF_STRING, 7, L"快速搜索  (Ctrl+Space)");
                AppendMenuW(m, MF_STRING, 10, L"剪贴板历史  (Ctrl+B)");
                AppendMenuW(m, MF_STRING | (cfg.clipboardPaused ? MF_CHECKED : 0u), 11,
                            L"暂停记录剪贴板");
                AppendMenuW(m, MF_STRING, 12, L"清空剪贴板历史");
                AppendMenuW(m, MF_STRING, 8, L"新建容器…");
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
                case 3: toggleVisible(); break;
                case 4:
                    autostart.IsEnabled() ? autostart.Disable() : autostart.Enable();
                    break;
                case 7: spot.Toggle(); break;
                case 10: clipPanel.Toggle(); break;
                case 11: {  // 暂停/恢复剪贴板记录（同步持久化）
                    bool p = !cfg.clipboardPaused;
                    cfg.clipboardPaused = p;
                    clipMon.SetPaused(p);
                    store.save(cfg);
                    break;
                }
                case 12:  // 一键清空（置顶保留），面板打开则同步刷新
                    clipStore.Clear();
                    clipPanel.OnStoreChanged();
                    break;
                case 8: {  // 新建容器：加盒并立即就地重命名
                    int bi = grid.AddBox(L"\u65B0\u5BB9\u5668");  // 新容器
                    if (bi >= 0) grid.BeginRename(bi);
                    break;
                }
                case 9:
                    win.SetQuitOnDestroy(true);  // 用户主动退出：WM_DESTROY 才真正结束循环
                    DestroyWindow(win.hwnd());
                    break;
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

    // 按格式自动归类已随自定义容器模型移除：不再监听桌面新文件做分类归位。
    // （watcher/flushPending/hint 保留为空档，但不注册 watcher 故不触发。）

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        try {
            DispatchMessageW(&m);
        } catch (std::exception const& e) {  // 任何 UI 异常只记录，不再弹模态框卡死
            std::string s = std::string("loop-exc:") + e.what();
            StageTrace(s.c_str());
        }
    }
    StageTrace("6:loop-exited");
    if (hostWnd) DestroyWindow(hostWnd);
    clipMon.Stop();  // 剪贴板监听：先停线程再销毁监听窗（须主线程）
    watcher.Stop();
    boxWatcher.Stop();
    RemoveHideHook();
    RevokeDragDrop(win.hwnd());
    store.save(cfg);
    tray.Remove();
    OleUninitialize();
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int) {
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

    // 单实例：命名互斥体。第二次启动会唤醒已开窗口并退出本次。
    // 僵尸实例处理：若互斥体存在但宿主/容器窗口都不在了（Explorer 重启级联销毁后
    // 的旧版遗留），不能静默退出——否则用户既进不去也退不出（只能任务管理器杀进程）。
    // 判定必须用 ExistingInstanceAlive()：贴桌模式下容器是 WorkerW 子窗口，
    // FindWindowW 找不到，直接用它会误判为僵尸并开出第二个实例。
    HANDLE inst = CreateMutexW(nullptr, TRUE, L"LumenBox_SingleInstanceMutex");
    if (inst && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (ExistingInstanceAlive()) {
            HWND w = FindWindowW(L"LumenBoxGlass", nullptr);
            if (w && IsWindow(w)) {
                ShowWindow(w, SW_RESTORE);
                SetForegroundWindow(w);
            }
            CloseHandle(inst);
            return 0;  // 已有实例在运行（正常），本次直接退出，避免多开
        }
        StageTrace("single-instance-zombie-takeover");  // 旧实例已成僵尸，本次接管
    }

    int exit_code = 0;
    std::string err;
    try {
        exit_code = Run(hInst, lpCmdLine);
        if (exit_code == 1) {  // 窗口创建失败：多为同名窗口类被僵尸实例占用
            if (FindWindowW(nullptr, L"LumenBox") == nullptr) {
                MessageBoxW(nullptr,
                            L"启动失败：可能仍有 LumenBox 旧进程残留（占用窗口类或单实例锁）。\n"
                            L"请打开任务管理器结束所有 lumenbox.exe 后重试。",
                            L"LumenBox", MB_ICONWARNING);
            }
        }
    } catch (std::exception const& e) {
        exit_code = 2;
        err = e.what();
    }
    if (!err.empty()) MessageBoxA(nullptr, err.c_str(), "LumenBox 异常", MB_ICONERROR);
    if (inst) CloseHandle(inst);  // 释放互斥体，允许下个实例启动
    return exit_code;
}
