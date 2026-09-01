#include "ui/desktop_grid.hpp"
#include "ui/input_box.hpp"
#include <algorithm>
#include "core/strutil.hpp"
#include "core/stage_log.hpp"
#include <filesystem>
#include <shellapi.h>
#include <dwrite.h>
#pragma comment(lib, "shell32")
#pragma comment(lib, "dwrite")

namespace lb {
namespace fs = std::filesystem;

static std::wstring Ellipsize(const std::wstring& s, size_t maxChars) {
    if (s.size() <= maxChars) return s;
    return s.substr(0, maxChars - 1) + L"…";
}

bool DesktopGrid::Init(GraphicsContext& gfx, ComPtr<IDCompositionVisual> root, float dpi) {
    gfx_ = &gfx;
    root_ = root;
    dpi_ = dpi;
    hwnd_ = nullptr;

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
        return false;
    dwrite_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.f,
                              L"zh-cn", headerFmt_.GetAddressOf());
    dwrite_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f,
                              L"zh-cn", rowFmt_.GetAddressOf());

    const Category cats[kBoxCount] = {Category::Doc, Category::Image, Category::Video,
                                      Category::Audio, Category::Archive,
                                      Category::Installer, Category::Other};
    for (int i = 0; i < kBoxCount; ++i) {
        Box b;
        b.cat = cats[i];
        if (FAILED(gfx.device->CreateVisual(b.visual.GetAddressOf()))) return false;
        b.visual->SetOffsetX(0.f);
        b.visual->SetOffsetY(0.f);
        root_->AddVisual(b.visual.Get(), TRUE, nullptr);
        boxes_.push_back(std::move(b));
    }
    gfx.device->Commit();
    return true;
}

float DesktopGrid::HeightOf(const Box& b) const {
    if (b.collapsed) return kHeaderH;
    size_t rows = std::min<size_t>(b.files.size(), kMaxRows);
    return kHeaderH + kPad + static_cast<float>(rows) * kRowH + 4.f;
}

void DesktopGrid::LayoutGridPositions() {
    float rowMaxH[2] = {0, 0};
    for (auto& b : boxes_) {
        int row = b.slot / kPerRow;
        rowMaxH[row] = std::max(rowMaxH[row], HeightOf(b));
    }
    float rowY[2] = {0, rowMaxH[0] + kGap};
    for (auto& b : boxes_) {
        int row = b.slot / kPerRow, col = b.slot % kPerRow;
        float x = col * (kBoxW + kGap), y = rowY[row];
        b.rect = {x, y, x + kBoxW, y + HeightOf(b)};
    }
}

void DesktopGrid::Relayout() {
    LayoutGridPositions();
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (auto& b : boxes_) {
        minx = std::min(minx, b.rect.left);
        miny = std::min(miny, b.rect.top);
        maxx = std::max(maxx, b.rect.right);
        maxy = std::max(maxy, b.rect.bottom);
    }
    bbox_ = {minx, miny, maxx, maxy};
    for (auto& b : boxes_) {
        b.visual->SetOffsetX((b.rect.left - minx) * dpi_);
        b.visual->SetOffsetY((b.rect.top - miny) * dpi_);
    }
    gfx_->device->Commit();
    if (on_extent_) on_extent_(maxx - minx, maxy - miny);
}

int DesktopGrid::BoxAtSlot(int slot) const {
    for (int i = 0; i < kBoxCount; ++i)
        if (boxes_[i].slot == slot) return i;
    return -1;
}

int DesktopGrid::NearestSlot(float x, float y) const {
    float rowMaxH[2] = {0, 0};
    for (auto& b : boxes_)
        rowMaxH[b.slot / kPerRow] = std::max(rowMaxH[b.slot / kPerRow], HeightOf(b));
    float rowY[2] = {0, rowMaxH[0] + kGap};
    int best = -1;
    float bestD = (kBoxW * 1.5f) * (kBoxW * 1.5f);  // 距离超过 1.5 格宽 = 拖出网格，归位
    for (int s = 0; s < kSlotCount; ++s) {
        float cx = (s % kPerRow) * (kBoxW + kGap) + kBoxW / 2;
        float cy = rowY[s / kPerRow] + std::max(rowMaxH[s / kPerRow], kHeaderH + 8.f) / 2;
        float d = (cx - x) * (cx - x) + (cy - y) * (cy - y);
        if (d < bestD) { bestD = d; best = s; }
    }
    return best;
}

void DesktopGrid::SetBoxNames(const std::map<std::string, std::string>& names) {
    boxNames_.clear();
    for (auto& [k, v] : names) boxNames_[ToWide(k)] = ToWide(v);
}

std::wstring DesktopGrid::DisplayName(Category c) const {
    auto it = boxNames_.find(CategoryKey(c));
    return it != boxNames_.end() && !it->second.empty() ? it->second : CategoryName(c);
}

void DesktopGrid::SetSlots(const std::vector<std::string>& order,
                           const std::vector<std::string>& collapsed) {
    for (auto& b : boxes_) b.slot = -1;
    int next = 0;
    for (auto& key : order)
        for (auto& b : boxes_)
            if (b.slot < 0 && ToUtf8(CategoryKey(b.cat)) == key) { b.slot = next++; break; }
    for (auto& b : boxes_)
        if (b.slot < 0 && next < kSlotCount) b.slot = next++;  // 未列出的补尾
    for (auto& key : collapsed)
        for (auto& b : boxes_)
            if (ToUtf8(CategoryKey(b.cat)) == key) b.collapsed = true;
}

std::vector<std::string> DesktopGrid::SlotOrder() const {
    std::vector<std::string> order(kSlotCount, "");
    for (auto& b : boxes_)
        if (b.slot >= 0 && b.slot < kSlotCount) order[b.slot] = ToUtf8(CategoryKey(b.cat));
    return order;
}

std::vector<std::string> DesktopGrid::CollapsedKeys() const {
    std::vector<std::string> out;
    for (auto& b : boxes_)
        if (b.collapsed) out.push_back(ToUtf8(CategoryKey(b.cat)));
    return out;
}



void DesktopGrid::RedrawBox(int i) {
    Box& b = boxes_[i];
    bool active = b.hovered || static_cast<int>(i) == drag_box_;
    float w = kBoxW, h = HeightOf(b);
    auto surface = gfx_->DrawToSurface(w, h, dpi_, [&](::ID2D1DeviceContext* ctx, float) {
        ctx->Clear(nullptr);

        // 贴桌模式下窗口是 WorkerW 子窗口，DWM 模糊不可用（仅顶层窗口生效），
        // 玻璃感由自绘深色卡面承担：#0B0E14 系，悬停/拖入微亮（Aurora Glass 规范）
        auto base = D2D1_COLOR_F{0.043f, 0.055f, 0.078f, active ? 0.74f : 0.62f};
        ComPtr<::ID2D1SolidColorBrush> fill;
        ctx->CreateSolidColorBrush(base, fill.GetAddressOf());
        ComPtr<::ID2D1SolidColorBrush> line;
        ctx->CreateSolidColorBrush(
            D2D1_COLOR_F{1.f, 1.f, 1.f, active ? 0.22f : 0.14f}, line.GetAddressOf());
        ComPtr<::ID2D1SolidColorBrush> text;
        ctx->CreateSolidColorBrush(D2D1_COLOR_F{1.f, 1.f, 1.f, 0.92f}, text.GetAddressOf());
        ComPtr<::ID2D1SolidColorBrush> dim;
        ctx->CreateSolidColorBrush(D2D1_COLOR_F{1.f, 1.f, 1.f, 0.55f}, dim.GetAddressOf());

        const D2D1_ROUNDED_RECT rr{{0.5f, 0.5f, w - 0.5f, h - 0.5f}, 12.f, 12.f};
        ctx->FillRoundedRectangle(rr, fill.Get());

        // 头栏：类名 + 计数
        std::wstring title = DisplayName(b.cat);
        ctx->DrawText(title.c_str(), (UINT32)title.size(), headerFmt_.Get(),
                      {12.f, 6.f, w - 40.f, kHeaderH}, text.Get());
        std::wstring count = std::to_wstring(b.files.size());
        ctx->DrawText(count.c_str(), (UINT32)count.size(), headerFmt_.Get(),
                      {w - 34.f, 6.f, w - 10.f, kHeaderH}, dim.Get());
        ctx->DrawLine({10.f, kHeaderH}, {w - 10.f, kHeaderH}, line.Get(), 0.8f);

        // 行：图标 + 文件名（悬停/拖入走极光渐变描边整卡）
        if (active) {
            D2D1_GRADIENT_STOP gs[3] = {
                {0.0f, D2D1_COLOR_F{0.133f, 0.827f, 0.933f, 0.9f}},
                {0.5f, D2D1_COLOR_F{0.506f, 0.549f, 0.973f, 0.9f}},
                {1.0f, D2D1_COLOR_F{0.753f, 0.518f, 0.988f, 0.9f}},
            };
            ComPtr<::ID2D1GradientStopCollection> stops;
            ctx->CreateGradientStopCollection(gs, 3, D2D1_GAMMA_2_2,
                                              D2D1_EXTEND_MODE_CLAMP, stops.GetAddressOf());
            ComPtr<::ID2D1LinearGradientBrush> border;
            ctx->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0),
                                                    D2D1::Point2F(w, h)),
                stops.Get(), border.GetAddressOf());
            ctx->DrawRoundedRectangle(rr, border.Get(), 1.2f);
        } else {
            ctx->DrawRoundedRectangle(rr, line.Get(), 1.f);
        }

        if (!b.collapsed) {
            size_t rows = std::min<size_t>(b.files.size(), kMaxRows);
            for (size_t r = 0; r < rows; ++r) {
                std::wstring name = Ellipsize(fs::path(b.files[r]).filename().wstring(), 19);
                float y = kHeaderH + kPad * 0.4f + static_cast<float>(r) * kRowH;
                if (auto* icon = icons_.Get(ctx, b.files[r])) {
                    ctx->DrawBitmap(icon, D2D1::RectF(14.f, y + 3.f, 30.f, y + 19.f), 1.f,
                                    D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
                }
                ctx->DrawText(name.c_str(), (UINT32)name.size(), rowFmt_.Get(),
                              {36.f, y, w - 12.f, y + kRowH + 3.f}, text.Get());
            }
            if (b.files.size() > kMaxRows) {
                std::wstring more = L"+ " + std::to_wstring(b.files.size() - kMaxRows) + L" 更多";
                float y = kHeaderH + kPad * 0.4f + static_cast<float>(kMaxRows) * kRowH;
                ctx->DrawText(more.c_str(), (UINT32)more.size(), rowFmt_.Get(),
                              {14.f, y, w - 12.f, y + kRowH + 3.f}, dim.Get());
            }
        }
    });
    if (surface) {
        b.visual->SetContent(surface.Get());
        gfx_->device->Commit();
    }
}

void DesktopGrid::Refresh() {
    if (!org_) return;
    for (auto& b : boxes_) {
        b.files = org_->FilesIn(b.cat);
        std::sort(b.files.begin(), b.files.end(), [](const std::wstring& a, const std::wstring& c) {
            return _wcsicmp(fs::path(a).filename().c_str(), fs::path(c).filename().c_str()) < 0;
        });
    }
    Relayout();
    for (int i = 0; i < kBoxCount; ++i) RedrawBox(i);
}

int DesktopGrid::BoxAt(float x, float y) const {
    // 客户区原点是窗口左上，grid 坐标原点是包围盒左上——自由布局下二者有偏移
    return BoxAtGrid(x + bbox_.left, y + bbox_.top);
}

int DesktopGrid::BoxAtGrid(float x, float y) const {
    for (int i = kBoxCount - 1; i >= 0; --i) {  // 倒序：重叠时上层（后添加）优先
        const auto& r = boxes_[i].rect;
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return i;
    }
    return -1;
}

void DesktopGrid::SetDragBox(int i) {
    if (i == drag_box_) return;
    int old = drag_box_;
    drag_box_ = i;
    if (old >= 0) RedrawBox(old);
    if (i >= 0) RedrawBox(i);
}

int DesktopGrid::RowIndexAt(const Box& b, float x, float y) const {
    float relY = y - b.rect.top - kHeaderH;
    if (relY < 0) return -2;  // 头栏
    int idx = static_cast<int>(relY / kRowH);
    size_t shown = std::min<size_t>(b.files.size(), kMaxRows);
    if (idx < 0 || idx >= static_cast<int>(shown)) return -1;
    return idx;
}

void DesktopGrid::OpenFile(const std::wstring& path) const {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void DesktopGrid::RowMenu(HWND hwnd, int boxIdx, const std::wstring& path) {
    (void)boxIdx;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"打开");
    AppendMenuW(m, MF_STRING, 2, L"打开位置");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 3, L"放回桌面");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);  // TrackPopupMenu 需要前台窗口才能正常收起
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                             pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(m);
    switch (cmd) {
    case 1: OpenFile(path); break;
    case 2:
        ShellExecuteW(nullptr, L"open", L"explorer.exe",
                      (L"/select,\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
        break;
    case 3:
        if (org_ && org_->MoveBackToDesktop(path)) Refresh();
        break;
    }
}

void DesktopGrid::HeaderMenu(HWND hwnd, int boxIdx) {
    if (boxIdx < 0 || boxIdx >= kBoxCount) return;
    std::wstring curName = DisplayName(boxes_[boxIdx].cat);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"重命名此格…");
    AppendMenuW(m, MF_STRING, 2, L"清空此格（放回桌面）");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                             pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(m);
    switch (cmd) {
    case 1: {
        std::wstring newName = InputBox(hwnd, L"重命名此格", curName.c_str());
        if (!newName.empty()) {
            std::wstring key = CategoryKey(boxes_[boxIdx].cat);
            boxNames_[key] = newName;
            if (on_box_renamed_) on_box_renamed_(key, newName);
            RedrawBox(boxIdx);
        }
        break;
    }
    case 2:
        if (on_clear_box_) on_clear_box_(boxIdx);
        break;
    }
}

void DesktopGrid::OnMouse(HWND hwnd, UINT msg, float x, float y) {
    hwnd_ = hwnd;
    x += bbox_.left;  // 客户区 → grid 坐标（矩形/命中都在 grid 空间）
    y += bbox_.top;
    int b = BoxAtGrid(x, y);
    switch (msg) {
    case WM_MOUSEMOVE: {
        if (dragging_) {
            POINT now{};
            GetCursorPos(&now);
            int dx = now.x - drag_cursor0_.x, dy = now.y - drag_cursor0_.y;
            if (!drag_moved_ && (abs(dx) > 3 || abs(dy) > 3)) drag_moved_ = true;
            if (!drag_moved_) return;
            if (box_drag_ >= 0) {
                char t[64];
                snprintf(t, 64, "d:boxmove %d,%d", dx, dy);
                StageTrace(t);
            }
            if (box_drag_ >= 0) {
                // 只动视觉偏移：矩形/命中保持槽位语义，Relayout 不会覆盖拖动
                drag_delta_ = {dx, dy};
                float ddx = dx / dpi_, ddy = dy / dpi_;
                auto& b = boxes_[box_drag_];
                b.visual->SetOffsetX((b.rect.left - bbox_.left + ddx) * dpi_);
                b.visual->SetOffsetY((b.rect.top - bbox_.top + ddy) * dpi_);
                gfx_->device->Commit();
                // 实时高亮交换目标（拖动盒中心下的槽位）
                float cx = box_rect0_.left + ddx + kBoxW / 2;
                float cy = box_rect0_.top + ddy + HeightOf(b) / 2;
                SetDragBox(BoxAtSlot(NearestSlot(cx, cy)));
            } else {  // 整窗
                SetWindowPos(hwnd_, nullptr, drag_rect0_.left + dx, drag_rect0_.top + dy, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return;
        }
        int row = b >= 0 ? RowIndexAt(boxes_[b], x, y) : -1;
        SetCursor(LoadCursorW(nullptr, row >= 0 ? IDC_HAND : IDC_ARROW));
        if (b != hovered_) {
            int old = hovered_;
            hovered_ = b;
            if (old >= 0) RedrawBox(old);
            if (b >= 0) RedrawBox(b);
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        // 卡片任意处按下 = 拖动该单格（文件行除外：行是打开/右键目标）；
        // 空隙按下 = 整窗拖动。头栏窄条曾导致抓边缘误落空隙，故放宽到卡片主体
        int row = b >= 0 ? RowIndexAt(boxes_[b], x, y) : -9;
        {
            char t[96];
            snprintf(t, 96, "d:down b=%d row=%d x=%.0f y=%.0f", b, row, x, y);
            StageTrace(t);
        }
        bool header = b >= 0 && row != -1;  // -1=行区外（仅头栏+内边距），>=0=文件行
        if (header) {
            SetCapture(hwnd);
            dragging_ = true;
            box_drag_ = b;
            drag_slot0_ = boxes_[b].slot;
            drag_on_header_ = (row == -2);
            drag_moved_ = false;
            GetCursorPos(&drag_cursor0_);
            box_rect0_ = boxes_[b].rect;
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        } else if (b < 0) {  // 空隙按下 = 整窗拖动
            SetCapture(hwnd);
            dragging_ = true;
            box_drag_ = -1;
            drag_moved_ = false;
            GetCursorPos(&drag_cursor0_);
            GetWindowRect(hwnd, &drag_rect0_);
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        }
        break;
    }
    case WM_LBUTTONUP: {
        if (dragging_) {
            int bd = box_drag_;
            bool moved = drag_moved_, onhdr = drag_on_header_;
            ReleaseCapture();  // 可能同步派发 WM_CAPTURECHANGED，先取状态
            dragging_ = false;
            SetCursor(LoadCursorW(nullptr, b >= 0 ? IDC_HAND : IDC_ARROW));
            if (bd < 0) {  // 整窗拖动
                StageTrace(moved ? "d:up win-moved" : "d:up win-click");
                if (moved && on_moved_) on_moved_(true);
                break;
            }
            if (!moved) {  // 未位移的头栏单击 → 折叠并持久化
                StageTrace("d:up click-header");
                if (drag_on_header_) {
                    boxes_[bd].collapsed = !boxes_[bd].collapsed;
                    Relayout();
                    RedrawBox(bd);
                    if (on_moved_) on_moved_(false);
                }
                break;
            }
            // 换槽位：松手光标位置（拖动盒中心）最近槽位；拖出网格则归位
            SetDragBox(-1);  // 清除拖动中的目标高亮
            float cx = box_rect0_.left + drag_delta_.x / dpi_ + kBoxW / 2;
            float cy = box_rect0_.top + drag_delta_.y / dpi_ + HeightOf(boxes_[bd]) / 2;
            int target = NearestSlot(cx, cy);
            {
                char t[80];
                snprintf(t, 80, "d:up bd=%d moved=%d target=%d slot=%d", bd,
                         (int)drag_moved_, target, boxes_[bd].slot);
                StageTrace(t);
            }
            if (bd >= kBoxCount || bd < 0) break;  // 越界保护
            if (target >= 0 && target != boxes_[bd].slot) {
                int other = BoxAtSlot(target);
                if (other >= 0)
                    std::swap(boxes_[other].slot, boxes_[bd].slot);
                else
                    boxes_[bd].slot = target;
            }
            Relayout();  // 吸附回整齐槽位
            if (on_moved_) on_moved_(false);
            break;
        }
        if (b >= 0 && RowIndexAt(boxes_[b], x, y) == -2) {  // 头栏点击 = 折叠
            boxes_[b].collapsed = !boxes_[b].collapsed;
            Relayout();
            RedrawBox(b);
        }
        break;
    }
    case WM_LBUTTONDBLCLK: {
        if (b >= 0) {
            int row = RowIndexAt(boxes_[b], x, y);
            if (row >= 0) OpenFile(boxes_[b].files[row]);
        }
        break;
    }
    case WM_RBUTTONUP: {
        if (b >= 0) {
            int row = RowIndexAt(boxes_[b], x, y);
            if (row >= 0)
                RowMenu(hwnd, b, boxes_[b].files[row]);
            else if (row == -2)  // 头栏右键
                HeaderMenu(hwnd, b);
        }
        break;
    }
    case WM_CAPTURECHANGED:
        dragging_ = false;  // 拖动被系统打断（如弹窗）即复位
        box_drag_ = -1;
        break;
    }
}

void DesktopGrid::AnimateIn(HWND hwnd) {
    for (UINT i = 0; i < kBoxCount; ++i)
        SetTimer(hwnd, kTimerBase + i, 30 * i, nullptr);
    hwnd_ = hwnd;
}

void DesktopGrid::OnTimer(HWND hwnd, UINT id) {
    if (id < kTimerBase || id >= kTimerBase + kBoxCount) return;
    KillTimer(hwnd, id);
    Box& b = boxes_[id - kTimerBase];

    constexpr float T = 0.24f, A = 1.6042f, B = -4.2968f, C = 3.6926f;
    float endY = (b.rect.top - bbox_.top) * dpi_;  // 视觉偏移为 bbox 相对坐标
    ComPtr<IDCompositionAnimation> op, oy;
    if (FAILED(gfx_->device->CreateAnimation(op.GetAddressOf()))) return;
    if (FAILED(gfx_->device->CreateAnimation(oy.GetAddressOf()))) return;
    op->Reset();
    op->AddCubic(0.0, 0.f, 1.f / T, 0.f, 0.f);  // 透明度线性 0→1
    op->End(T, 1.f);
    oy->Reset();
    oy->AddCubic(0.0, endY + 14.f, -14.f * C / T, 14.f * -B / (T * T),
                 14.f * -A / (T * T * T));  // y: +14 → endY，ease-out
    oy->End(T, endY);
    b.visual->SetOffsetY(oy.Get());
    if (ComPtr<IDCompositionVisual3> v3; SUCCEEDED(b.visual.As(&v3)))
        v3->SetOpacity(op.Get());  // 动画版 SetOpacity 在 Visual3
    gfx_->device->Commit();
}

}  // namespace lb
