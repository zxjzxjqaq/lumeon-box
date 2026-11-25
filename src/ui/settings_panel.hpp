#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>
#include "core/config.hpp"

namespace lb {

// 设置面板（托盘「设置…」）：烟熏玻璃整板，与剪贴板面板同款工程
// （分层窗口 + GDI+ 整帧原子重绘）。所有开关直接写 config 并即时生效；
// 呼出热键支持点击后按键捕获。隐藏图标模式改动提示重启生效。
class SettingsPanel {
public:
    void SetApi(SpikeConfig* cfg, ConfigStore* store,
                std::function<bool()> autostartGet, std::function<void(bool)> autostartSet,
                std::function<void()> apply) {
        cfg_ = cfg;
        store_ = store;
        autoGet_ = std::move(autostartGet);
        autoSet_ = std::move(autostartSet);
        apply_ = std::move(apply);
    }
    void Toggle();
    bool Visible() const { return created_ && IsWindowVisible(hwnd_); }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void OnOpen();
    void OnClose();
    void Draw();
    void Commit();  // save + apply + 重绘

    enum Kind { KToggle, KStepper, KCycle, KHotkey };
    struct Row {
        std::wstring label;
        std::wstring hint;
        int kind = KToggle;
        std::function<std::wstring()> text;   // 当前值显示
        std::function<void(int)> act;         // dir: -1/+1（步进/切换），0=进入捕获
    };
    std::vector<Row> BuildRows();
    int HitRow(int x, int y) const;      // 行下标；-1 无
    int HitControl(int x, int y) const;  // 行内控件区：0=左减 1=右加/整控件 2=无

    static constexpr int kW = 480, kHeaderH = 48, kRowH = 40, kFooterH = 34;
    float scale_ = 1.f;
    HWND hwnd_ = nullptr;
    bool created_ = false;
    SpikeConfig* cfg_ = nullptr;
    ConfigStore* store_ = nullptr;
    std::function<bool()> autoGet_;
    std::function<void(bool)> autoSet_;
    std::function<void()> apply_;
    bool capturing_ = false;  // 热键捕获态
    int hover_ = -1;
    bool closeHot_ = false;
    std::vector<Row> rows_;
    std::vector<RECT> rowRects_;     // 逻辑坐标（Draw 时重建）
    std::vector<RECT> ctlRects_;     // 每行主控件矩形
    std::vector<RECT> minusRects_;   // 步进左减
};

}  // namespace lb