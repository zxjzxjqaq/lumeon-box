#include "anim/spring_demo.hpp"
#include "core/config.hpp"
#include "core/stage_log.hpp"
#include "shell/desktop_layer.hpp"
#include "ui/card_visual.hpp"
#include "ui/d2d_interop.hpp"
#include "ui/glass_window.hpp"

using namespace lb;

static int RunSpike(HINSTANCE hInst) {
    StageTrace("0:enter-runspike");

    ConfigStore store(ConfigStore::default_dir() / L"spike.json");
    SpikeConfig cfg = store.load();

    GlassWindow win;
    SpringDemo demo;
    DesktopMode mode = cfg.mode == "desktop" ? DesktopMode::DesktopLayer
                     : cfg.mode == "bottom"  ? DesktopMode::Bottom
                                             : DesktopMode::Independent;

    auto on_key = [&](UINT id) {
        if (id == 1) {  // F9：独立 → 贴桌(失败转置底) → 独立
            StageTrace("k:f9-press");
            if (mode == DesktopMode::Independent) {
                if (AttachToDesktopLayer(win.hwnd())) {
                    StageTrace("k:attach-ok");
                    mode = DesktopMode::DesktopLayer;
                } else {
                    StageTrace("k:attach-fail -> bottom");
                    UseBottomMode(win.hwnd());
                    mode = DesktopMode::Bottom;
                }
            } else if (RestoreIndependent(win.hwnd())) {
                StageTrace("k:restore-ok");
                mode = DesktopMode::Independent;
            }
            cfg.mode = mode == DesktopMode::DesktopLayer ? "desktop"
                     : mode == DesktopMode::Bottom       ? "bottom"
                                                         : "independent";
        } else if (id == 2) {  // F10：弹簧动画
            demo.Trigger(win.hwnd());
        }
    };

    if (!win.Create(hInst, L"LumenBox Spike", cfg, on_key)) return 1;
    StageTrace("1:window-created");
    // spike 用 Ctrl+Alt+1/2：F 键常被输入法/工具占用（F9 在本机注册即失败）
    if (!RegisterHotKey(win.hwnd(), 1, MOD_CONTROL | MOD_ALT, '1'))
        StageTrace("hotkey-CA1-register-FAILED");
    if (!RegisterHotKey(win.hwnd(), 2, MOD_CONTROL | MOD_ALT, '2'))
        StageTrace("hotkey-CA2-register-FAILED");
    win.OnTimer([&](HWND h, UINT id) { demo.OnTimer(h, id); });

    ComPtr<IDCompositionVisual> card;
    if (!CreateCardVisual(win.gfx(), card, static_cast<float>(cfg.w),
                          static_cast<float>(cfg.h), win.DpiScale()))
        return 1;
    win.Root()->AddVisual(card.Get(), TRUE, nullptr);
    StageTrace("3:card-visual");

    if (!demo.Init(win.gfx(), static_cast<float>(cfg.w), static_cast<float>(cfg.h)))
        return 1;
    StageTrace("4:spring-init");

    // 恢复上次模式
    if (mode == DesktopMode::DesktopLayer && !AttachToDesktopLayer(win.hwnd())) {
        UseBottomMode(win.hwnd());
        mode = DesktopMode::Bottom;
        cfg.mode = "bottom";
    } else if (mode == DesktopMode::Bottom) {
        UseBottomMode(win.hwnd());
    }
    StageTrace("5:mode-restored");

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    StageTrace("6:loop-exited");
    store.save(cfg);  // 位置/尺寸/模式持久化
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

    int exit_code = 0;
    std::string err;
    try {
        exit_code = RunSpike(hInst);
    } catch (std::exception const& e) {
        exit_code = 2;
        err = e.what();
    }
    if (!err.empty()) MessageBoxA(nullptr, err.c_str(), "LumenBox Spike 异常", MB_ICONERROR);
    return exit_code;
}
