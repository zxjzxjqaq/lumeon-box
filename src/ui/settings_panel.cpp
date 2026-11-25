#include "ui/settings_panel.hpp"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include "core/hotkey.hpp"
#include "core/strutil.hpp"

#pragma comment(lib, "gdiplus.lib")

namespace lb {
namespace gd = Gdiplus;

namespace {
// 与剪贴板面板同一套烟熏玻璃色板
const gd::Color kPanelBg(205, 11, 14, 20);
const gd::Color kPanelStroke(255, 38, 44, 60);
const gd::Color kText(255, 235, 238, 245);
const gd::Color kTextMeta(255, 150, 158, 172);
const gd::Color kTextDim(255, 110, 118, 132);
const gd::Color kAccent(255, 34, 211, 238);
const gd::Color kLine(255, 38, 44, 60);
const gd::Color kHoverRow(26, 255, 255, 255);
const gd::Color kCtlBg(16, 255, 255, 255);

void PresentLayered(HWND hwnd, int pw, int ph, float scale,
                    const std::function<void(gd::Graphics&)>& paint) {
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = pw;
    bi.bmiHeader.biHeight = -ph;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp) {
        HGDIOBJ old = SelectObject(mem, bmp);
        {
            gd::Graphics g(mem);
            g.SetSmoothingMode(gd::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(gd::TextRenderingHintAntiAliasGridFit);
            g.ScaleTransform(scale, scale);
            paint(g);
        }
        auto* px = static_cast<unsigned*>(bits);
        for (int i = 0; i < pw * ph; ++i) {
            unsigned a = (px[i] >> 24) & 0xFF;
            px[i] = (a << 24) |
                    ((((px[i] >> 16) & 0xFF) * a / 255) << 16) |
                    ((((px[i] >> 8) & 0xFF) * a / 255) << 8) |
                    (((px[i] & 0xFF) * a / 255));
        }
        POINT src{0, 0};
        SIZE sz{pw, ph};
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd, screen, nullptr, &sz, mem, &src, 0, &bf, ULW_ALPHA);
        SelectObject(mem, old);
        DeleteObject(bmp);
    }
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

void PillPath(gd::GraphicsPath& cap, const gd::RectF& rc, float d) {
    cap.AddArc(rc.X, rc.Y, d, d, 90, 180);
    cap.AddArc(rc.X + rc.Width - d, rc.Y, d, d, 270, 180);
    cap.CloseFigure();
}

void Pill(gd::Graphics& g, const gd::RectF& rc, float d) {
    gd::GraphicsPath cap;
    PillPath(cap, rc, d);
    gd::SolidBrush fill(kCtlBg);
    gd::Pen border(kLine, 1.f);
    g.FillPath(&fill, &cap);
    g.DrawPath(&border, &cap);
}
}  // namespace

// ---------------------------------------------------------- 行模型

std::vector<SettingsPanel::Row> SettingsPanel::BuildRows() {
    std::vector<Row> r;
    SpikeConfig& c = *cfg_;
    auto save = [this] { Commit(); };
    r.push_back({L"剪贴板历史", L"关闭后不再记录新复制", KToggle,
                 [&] { return c.clipboardEnabled ? L"开" : L"关"; },
                 [&](int) { c.clipboardEnabled = !c.clipboardEnabled; save(); }});
    r.push_back({L"暂停记录", L"临时停记，已有历史保留", KToggle,
                 [&] { return c.clipboardPaused ? L"已暂停" : L"记录中"; },
                 [&](int) { c.clipboardPaused = !c.clipboardPaused; save(); }});
    r.push_back({L"历史条数上限", L"20–500，超出淘汰最旧", KStepper,
                 [&] { return std::to_wstring(c.clipboardMaxEntries) + L" 条"; },
                 [&](int d) {
                     c.clipboardMaxEntries = std::clamp(c.clipboardMaxEntries + d * 20, 20, 500);
                     save();
                 }});
    r.push_back({L"图片磁盘上限", L"8–512 MB，超出淘汰最旧", KStepper,
                 [&] { return std::to_wstring(c.clipboardBlobMB) + L" MB"; },
                 [&](int d) {
                     c.clipboardBlobMB = std::clamp(c.clipboardBlobMB + d * 8, 8, 512);
                     save();
                 }});
    r.push_back({L"记录来源应用", L"历史里显示从哪个程序复制", KToggle,
                 [&] { return c.clipboardRecordSource ? L"开" : L"关"; },
                 [&](int) { c.clipboardRecordSource = !c.clipboardRecordSource; save(); }});
    r.push_back({L"呼出热键", capturing_ ? L"按下新组合键（Esc 取消）" : L"点击后按键修改",
                 KHotkey, [&] { return ToWide(c.clipboardHotkey); },
                 [&](int) { capturing_ = true; Draw(); }});
    r.push_back({L"自动归类", L"新文件进容器：关/询问/自动", KCycle,
                 [&] {
                     return c.watcherMode == "auto" ? L"自动"
                          : c.watcherMode == "prompt" ? L"询问"
                                                      : L"关";
                 },
                 [&](int) {
                     c.watcherMode = c.watcherMode == "off" ? "prompt"
                                   : c.watcherMode == "prompt" ? "auto"
                                                               : "off";
                     save();
                 }});
    r.push_back({L"双击隐藏桌面图标", L"改动重启后生效", KCycle,
                 [&] { return c.hideIconsMode == "hook" ? L"双击钩子" : L"热键"; },
                 [&](int) {
                     c.hideIconsMode = c.hideIconsMode == "hook" ? "hotkey" : "hook";
                     save();
                 }});
    r.push_back({L"搜索桌面 / 文档", L"快速搜索纳入这两个文件夹", KToggle,
                 [&] { return c.searchFolders ? L"开" : L"关"; },
                 [&](int) { c.searchFolders = !c.searchFolders; save(); }});
    r.push_back({L"待办到期提醒", L"到期当天托盘气泡提醒一次", KToggle,
                 [&] { return c.todoReminder ? L"开" : L"关"; },
                 [&](int) { c.todoReminder = !c.todoReminder; save(); }});
    r.push_back({L"提醒时刻", L"当天到点或之后首次检查", KStepper,
                 [&] { return std::to_wstring(c.todoReminderHour) + L" 点"; },
                 [&](int d) {
                     c.todoReminderHour = std::clamp(c.todoReminderHour + d, 0, 23);
                     save();
                 }});
    r.push_back({L"开机自启", L"登录 Windows 后自动运行", KToggle,
                 [&] { return autoGet_ && autoGet_() ? L"开" : L"关"; },
                 [&](int) {
                     if (autoSet_ && autoGet_) autoSet_(!autoGet_());
                     Draw();
                 }});
    return r;
}

// ---------------------------------------------------------- 生命周期

void SettingsPanel::Toggle() {
    if (created_ && IsWindowVisible(hwnd_)) {
        OnClose();
        return;
    }
    OnOpen();
}

void SettingsPanel::OnOpen() {
    if (!cfg_) return;
    if (!created_) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = &SettingsPanel::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"LumenBoxSettings";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"LumenBoxSettings", L"",
                                WS_POPUP, 0, 0, kW, kHeaderH, nullptr, nullptr,
                                wc.hInstance, this);
        if (!hwnd_) return;
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, (LONG_PTR)this);
        created_ = true;
    }
    scale_ = GetDpiForSystem() / 96.f;
    capturing_ = false;
    hover_ = -1;
    rows_ = BuildRows();
    int h = kHeaderH + (int)rows_.size() * kRowH + kFooterH;
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int pw = (int)(kW * scale_), ph = (int)(h * scale_);
    int x = wa.left + ((wa.right - wa.left) - pw) / 2;
    int y = wa.top + ((wa.bottom - wa.top) - ph) / 2;
    if (y < wa.top) y = wa.top;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, pw, ph, SWP_NOACTIVATE);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    Draw();
}

void SettingsPanel::OnClose() {
    capturing_ = false;
    ShowWindow(hwnd_, SW_HIDE);
}

void SettingsPanel::Commit() {
    if (store_ && cfg_) store_->save(*cfg_);
    if (apply_) apply_();
    rows_ = BuildRows();
    Draw();
}

// ---------------------------------------------------------- 绘制

void SettingsPanel::Draw() {
    if (!created_) return;
    rows_ = BuildRows();
    const int h = kHeaderH + (int)rows_.size() * kRowH + kFooterH;
    rowRects_.clear();
    ctlRects_.clear();
    minusRects_.clear();
    PresentLayered(hwnd_, (int)(kW * scale_), (int)(h * scale_), scale_,
                   [&](gd::Graphics& g) {
        const float w = (float)kW, hh = (float)h;
        gd::SolidBrush clear(gd::Color(0, 0, 0, 0));
        g.FillRectangle(&clear, gd::RectF(0, 0, w, hh));
        gd::GraphicsPath path;
        const float r = 16.f;
        path.AddArc(0.5f, 0.5f, r, r, 180, 90);
        path.AddArc(w - r - 0.5f, 0.5f, r, r, 270, 90);
        path.AddArc(w - r - 0.5f, hh - r - 0.5f, r, r, 0, 90);
        path.AddArc(0.5f, hh - r - 0.5f, r, r, 90, 90);
        path.CloseFigure();
        gd::SolidBrush bg(kPanelBg);
        g.FillPath(&bg, &path);
        gd::LinearGradientBrush aurora(gd::PointF(0, 0), gd::PointF(w, hh),
                                       gd::Color(0, 0, 0, 0), gd::Color(0, 0, 0, 0));
        gd::Color acols[3] = {gd::Color(120, 34, 211, 238), gd::Color(120, 129, 140, 248),
                              gd::Color(120, 192, 132, 252)};
        gd::REAL apos[3] = {0.f, 0.5f, 1.f};
        aurora.SetInterpolationColors(acols, apos, 3);
        gd::Pen stroke(&aurora, 1.2f);
        g.DrawPath(&stroke, &path);

        gd::Font fTitle(L"Microsoft YaHei UI", 15.f, gd::FontStyleBold, gd::UnitPixel);
        gd::Font fRow(L"Microsoft YaHei UI", 13.f, gd::FontStyleRegular, gd::UnitPixel);
        gd::Font fCtl(L"Microsoft YaHei UI", 12.f, gd::FontStyleRegular, gd::UnitPixel);
        gd::Font fHint(L"Microsoft YaHei UI", 11.f, gd::FontStyleRegular, gd::UnitPixel);
        gd::SolidBrush tc(kText), mc(kTextMeta), dc(kTextDim);
        gd::SolidBrush hoverBr(kHoverRow);
        gd::SolidBrush lineBr(kLine);
        gd::SolidBrush sepBr(gd::Color(26, 255, 255, 255));
        gd::StringFormat lf;
        lf.SetFormatFlags(gd::StringFormatFlagsNoWrap);
        gd::StringFormat cf;
        cf.SetAlignment(gd::StringAlignmentCenter);
        cf.SetLineAlignment(gd::StringAlignmentCenter);
        cf.SetFormatFlags(gd::StringFormatFlagsNoWrap);
        g.DrawString(L"设置", -1, &fTitle, gd::RectF(16.f, 0.f, 200.f, (float)kHeaderH),
                     &lf, &tc);
        {  // 关闭 X
            gd::RectF rc(w - 36.f, ((float)kHeaderH - 24.f) / 2.f, 24.f, 24.f);
            if (closeHot_) g.FillRectangle(&hoverBr, rc);
            gd::Pen cp(&mc, 1.6f);
            float cx = rc.X + 6.f, cy = rc.Y + 6.f;
            g.DrawLine(&cp, cx, cy, cx + 12.f, cy + 12.f);
            g.DrawLine(&cp, cx, cy + 12.f, cx + 12.f, cy);
        }
        g.FillRectangle(&lineBr, gd::RectF(8.f, (float)kHeaderH, w - 16.f, 1.f));

        for (int i = 0; i < (int)rows_.size(); ++i) {
            const Row& row = rows_[i];
            float y = (float)(kHeaderH + i * kRowH);
            RECT rr{8, (int)y + 2, (int)w - 8, (int)y + kRowH - 2};
            rowRects_.push_back(rr);
            if (i == hover_)
                g.FillRectangle(&hoverBr,
                                gd::RectF(8.f, y + 2.f, w - 16.f, (float)kRowH - 4.f));
            g.DrawString(row.label.c_str(), -1, &fRow, gd::RectF(16.f, y, 220.f, (float)kRowH),
                         &lf, &tc);
            if (!row.hint.empty())
                g.DrawString(row.hint.c_str(), -1, &fHint,
                             gd::RectF(16.f, y + 21.f, 260.f, 16.f), &lf, &dc);
            // 控件区（右侧）
            float cw = 132.f, cx = w - 16.f - cw, cy = y + (kRowH - 24.f) / 2.f;
            RECT cr{(int)cx, (int)cy, (int)(cx + cw), (int)(cy + 24.f)};
            ctlRects_.push_back(cr);
            minusRects_.push_back(RECT{0, 0, 0, 0});
            std::wstring val = row.text ? row.text() : L"";
            if (row.kind == KToggle) {
                bool on = val == L"开" || val == L"记录中";
                gd::RectF rc(cx + cw - 46.f, cy, 46.f, 24.f);
                gd::GraphicsPath cap;
                PillPath(cap, rc, 24.f);
                if (on) {
                    gd::SolidBrush onBr(kAccent);
                    g.FillPath(&onBr, &cap);
                } else {
                    gd::SolidBrush offBr(kCtlBg);
                    gd::Pen offPen(kLine, 1.f);
                    g.FillPath(&offBr, &cap);
                    g.DrawPath(&offPen, &cap);
                }
                float kx = on ? rc.X + rc.Width - 20.f : rc.X + 4.f;
                gd::SolidBrush knob(on ? gd::Color(255, 11, 14, 20) : kTextMeta);
                g.FillEllipse(&knob, kx, rc.Y + 4.f, 16.f, 16.f);
            } else if (row.kind == KStepper) {
                gd::RectF minus(cx, cy, 24.f, 24.f), plus(cx + cw - 24.f, cy, 24.f, 24.f);
                minusRects_.back() = RECT{(int)minus.X, (int)minus.Y,
                                          (int)(minus.X + minus.Width), (int)(minus.Y + minus.Height)};
                Pill(g, minus, 24.f);
                Pill(g, plus, 24.f);
                g.DrawString(L"−", -1, &fCtl, minus, &cf, &mc);
                g.DrawString(L"+", -1, &fCtl, plus, &cf, &mc);
                g.DrawString(val.c_str(), -1, &fCtl,
                             gd::RectF(cx + 26.f, cy, cw - 52.f, 24.f), &cf, &tc);
            } else {  // KCycle / KHotkey
                gd::RectF rc(cx, cy, cw, 24.f);
                Pill(g, rc, 24.f);
                if (row.kind == KHotkey && capturing_) {
                    gd::GraphicsPath hp;
                    PillPath(hp, rc, 24.f);
                    gd::Pen capPen(kAccent, 1.4f);
                    g.DrawPath(&capPen, &hp);
                }
                gd::SolidBrush vc(row.kind == KHotkey && capturing_ ? kAccent : kText);
                g.DrawString(val.c_str(), -1, &fCtl, rc, &cf, &vc);
            }
            if (i + 1 < (int)rows_.size())
                g.FillRectangle(&sepBr,
                                gd::RectF(16.f, y + kRowH - 1.f, w - 32.f, 1.f));
        }
        g.DrawString(L"改动立即保存并生效 · 双击隐藏模式重启后生效", -1, &fHint,
                     gd::RectF(16.f, hh - (float)kFooterH, w - 32.f, (float)kFooterH),
                     &lf, &dc);
    });
}

// ---------------------------------------------------------- 命中 / 消息

int SettingsPanel::HitRow(int x, int y) const {
    for (int i = 0; i < (int)rowRects_.size(); ++i) {
        const RECT& r = rowRects_[i];
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return i;
    }
    return -1;
}

int SettingsPanel::HitControl(int x, int y) const {
    for (int i = 0; i < (int)ctlRects_.size(); ++i) {
        const RECT& r = ctlRects_[i];
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) {
            const RECT& m = minusRects_[i];
            if (m.right > m.left && x >= m.left && x < m.right && y >= m.top && y < m.bottom)
                return 0;
            return 1;
        }
    }
    return 2;
}

LRESULT CALLBACK SettingsPanel::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (SettingsPanel*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_LBUTTONDOWN: {
        int x = (int)((int)(short)LOWORD(lp) / self->scale_);
        int y = (int)((int)(short)HIWORD(lp) / self->scale_);
        if (y < kHeaderH) {
            if (x > kW - 40) self->OnClose();
            return 0;
        }
        int row = self->HitRow(x, y);
        if (row < 0 || row >= (int)self->rows_.size()) return 0;
        int ctl = self->HitControl(x, y);
        if (ctl == 2) return 0;
        const auto& r = self->rows_[row];
        if (r.kind == KStepper) r.act(ctl == 0 ? -1 : 1);
        else r.act(1);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int x = (int)((int)(short)LOWORD(lp) / self->scale_);
        int y = (int)((int)(short)HIWORD(lp) / self->scale_);
        int row = self->HitRow(x, y);
        bool ch = y < kHeaderH && x > kW - 40;
        if (row != self->hover_ || ch != self->closeHot_) {
            self->hover_ = row;
            self->closeHot_ = ch;
            self->Draw();
        }
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (self->hover_ != -1 || self->closeHot_) {
            self->hover_ = -1;
            self->closeHot_ = false;
            self->Draw();
        }
        return 0;
    case WM_KEYDOWN: {
        if (self->capturing_) {
            if (wp == VK_ESCAPE) {
                self->capturing_ = false;
                self->Draw();
                return 0;
            }
            if (wp == VK_CONTROL || wp == VK_MENU || wp == VK_SHIFT ||
                wp == VK_LWIN || wp == VK_RWIN) return 0;
            UINT mods = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
            if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
            if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
            std::string s = FormatHotkey(mods, (UINT)wp);
            if (!s.empty() && self->cfg_) {
                self->cfg_->clipboardHotkey = s;
                self->capturing_ = false;
                self->Commit();
            }
            return 0;
        }
        if (wp == VK_ESCAPE) self->OnClose();
        return 0;
    }
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace lb