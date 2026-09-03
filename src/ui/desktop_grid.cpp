#include "ui/desktop_grid.hpp"
#include "ui/file_drag.hpp"
#include "ui/input_box.hpp"
#include "ui/calendar.hpp"
#include "ui/shell_ctx_menu.hpp"
#include <algorithm>
#include <fstream>
#include <wchar.h>
#include <nlohmann/json.hpp>
#include "core/strutil.hpp"
#include "core/stage_log.hpp"
#include <filesystem>
#include <shellapi.h>
#include <dwrite.h>
#pragma comment(lib, "shell32")
#pragma comment(lib, "dwrite")

namespace lb {
namespace fs = std::filesystem;

// —— 日程格：格里高利历日期工具（跨月/跨年安全）——
struct ADate {
    int y, m, d;
};
static bool LeapY(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int DimOf(int y, int m) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2) return LeapY(y) ? 29 : 28;
    return d[m - 1];
}
static long long Jdn(int y, int m, int d) {  // 儒略日数（0 UTC 正午）
    long long a = (14 - m) / 12;
    long long yy = y + 4800 - a, mm = m + 12 * a - 3;
    return d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
}
static ADate FromJdn(long long j) {
    long long a = j + 32044, b = (4 * a + 3) / 146097, c = a - (146097 * b) / 4;
    long long dd = (4 * c + 3) / 1461, e = c - (1461 * dd) / 4, m = (5 * e + 2) / 153;
    int day = (int)(e - (153 * m + 2) / 5 + 1);
    int month = (int)(m + 3 - 12 * (m / 10));
    int year = (int)(100 * b + dd - 4800 + m / 10);
    return {year, month, day};
}
static ADate TodayDate() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return {st.wYear, st.wMonth, st.wDay};
}
static ADate AddDays(ADate d, int n) { return FromJdn(Jdn(d.y, d.m, d.d) + n); }
static std::wstring DateKey(ADate d) {
    wchar_t b[16];
    swprintf_s(b, L"%04d-%02d-%02d", d.y, d.m, d.d);
    return b;
}
static std::wstring DateLabel(ADate d) {
    static const wchar_t* WD[] = {L"一", L"二", L"三", L"四", L"五", L"六", L"日"};
    int wd = (int)(Jdn(d.y, d.m, d.d) % 7);  // 0=周一
    wchar_t b[32];
    swprintf_s(b, L"%d月%d日 周%s", d.m, d.d, WD[wd]);
    return b;
}

DesktopGrid::~DesktopGrid() = default;

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
    // 关掉换行：文本超出行宽时不再折行溢到下一行（配合按像素宽截断的 … 省略）
    headerFmt_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    rowFmt_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    ADate t = TodayDate();
    curY_ = t.y;
    curM_ = t.m;
    curD_ = t.d;
    LoadTasks();
    calendar_.SetOnPick([this](int y, int m, int d) {
        curY_ = y;
        curM_ = m;
        curD_ = d;
        CancelTaskEdit();
        for (int i = 0; i < (int)boxes_.size(); ++i)
            if (IsTasksBox(boxes_[i])) boxes_[i].scroll = 0;  // 选新日期回到顶部
        Relayout();
        for (int i = 0; i < (int)boxes_.size(); ++i)
            if (IsTasksBox(boxes_[i])) RedrawBox(i);
        SaveTasks();
    });
    gfx.device->Commit();
    return true;
}

std::wstring DesktopGrid::SelectedKey() const {
    return DateKey({curY_, curM_, curD_});
}

float DesktopGrid::TasksHeight(const Box& b) const {
    if (b.collapsed) return kHeaderH;
    size_t rows = std::min<size_t>(tasks_.count(SelectedKey()) ? tasks_.at(SelectedKey()).size() : 0,
                                   (size_t)kMaxRows);
    return kHeaderH + kDateH + kPad * 0.4f + static_cast<float>(rows + 1) * kRowH + 4.f;
}

float DesktopGrid::HeightOf(const Box& b) const {
    if (b.collapsed) return kHeaderH;
    if (IsTasksBox(b)) return TasksHeight(b);
    size_t rows = std::min<size_t>(b.files.size(), kMaxRows);
    return kHeaderH + kPad + static_cast<float>(rows) * kRowH + 4.f;
}

void DesktopGrid::LayoutGridPositions() {
    int n = (int)boxes_.size();
    if (n == 0) return;
    int gridCols = std::min(kPerRow, n);            // 网格总列数（由最多盒子的行决定）
    int rows = (n + kPerRow - 1) / kPerRow;
    std::vector<float> rowMaxH(rows, 0.f);
    for (int i = 0; i < n; ++i) {
        int row = i / kPerRow;
        rowMaxH[row] = std::max(rowMaxH[row], HeightOf(boxes_[i]));
    }
    std::vector<float> rowY(rows, 0.f);
    for (int r = 1; r < rows; ++r) rowY[r] = rowY[r - 1] + rowMaxH[r - 1] + kGap;
    // 从左到右排，满 4 个换行；不满的末行右对齐（空位留在左侧）
    for (int r = 0; r < rows; ++r) {
        int start = r * kPerRow;
        int count = std::min(kPerRow, n - start);   // 本行盒子数
        int baseCol = gridCols - count;             // 本行右对齐偏置
        for (int k = 0; k < count; ++k) {
            int i = start + k;
            int col = baseCol + k;
            float x = col * (kBoxW + kGap), y = rowY[r];
            boxes_[i].rect = {x, y, x + kBoxW, y + HeightOf(boxes_[i])};
        }
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

void DesktopGrid::SetBoxes(const std::vector<BoxDef>& defs) {
    // 复用已有视觉；比旧清单多的则新建，少的则移除
    while (boxes_.size() > defs.size()) {
        if (root_ && boxes_.back().visual) root_->RemoveVisual(boxes_.back().visual.Get());
        boxes_.pop_back();
    }
    while (boxes_.size() < defs.size()) {
        Box b;
        if (FAILED(gfx_->device->CreateVisual(b.visual.GetAddressOf()))) return;
        b.visual->SetOffsetX(0.f);
        b.visual->SetOffsetY(0.f);
        root_->AddVisual(b.visual.Get(), TRUE, nullptr);
        boxes_.push_back(std::move(b));
    }
    for (int i = 0; i < (int)defs.size(); ++i) {
        Box& b = boxes_[i];
        b.id = defs[i].id;
        b.name = defs[i].name;
        b.folder = defs[i].folder;
        b.isTasks = defs[i].type == "tasks";
        b.collapsed = defs[i].collapsed;
        b.scroll = 0;
    }
    Relayout();
}

std::vector<BoxDef> DesktopGrid::BoxesOut() const {
    std::vector<BoxDef> out;
    out.reserve(boxes_.size());
    for (auto& b : boxes_) {
        BoxDef d;
        d.id = b.id;
        d.name = b.name;
        d.folder = b.folder;
        d.type = b.isTasks ? "tasks" : "data";
        d.collapsed = b.collapsed;
        out.push_back(std::move(d));
    }
    return out;
}

BoxDef DesktopGrid::BoxDefAt(int i) const {
    BoxDef d;
    if (i >= 0 && i < (int)boxes_.size()) {
        const Box& b = boxes_[i];
        d.id = b.id;
        d.name = b.name;
        d.folder = b.folder;
        d.type = b.isTasks ? "tasks" : "data";
        d.collapsed = b.collapsed;
    }
    return d;
}

int DesktopGrid::AddBox(const std::wstring& name) {
    if (!org_ || (int)boxes_.size() >= kMaxBoxes) return -1;
    int idx = (int)boxes_.size();
    std::wstring folder = org_->MakeFolder(L"box_b" + std::to_wstring(idx));  // idx0=待办,idx1=默认(box_b1)
    Box b;
    b.id = ToUtf8(folder);  // 以容器子目录为唯一键（MakeFolder 已去重）
    b.name = name.empty() ? L"\u65B0\u5BB9\u5668" : name;  // 新容器
    b.folder = folder;
    b.isTasks = false;
    if (FAILED(gfx_->device->CreateVisual(b.visual.GetAddressOf()))) return -1;
    b.visual->SetOffsetX(0.f);
    b.visual->SetOffsetY(0.f);
    root_->AddVisual(b.visual.Get(), TRUE, nullptr);
    boxes_.push_back(std::move(b));
    Relayout();
    RedrawBox(idx);
    if (on_boxes_changed_) on_boxes_changed_();
    return idx;
}

void DesktopGrid::RemoveBox(int i) {
    if (i < 0 || i >= (int)boxes_.size()) return;
    if (boxes_[i].isTasks) return;  // 待办盒不可删
    if (org_ && !boxes_[i].folder.empty()) {
        std::error_code ec;
        fs::remove(org_->FolderDir(boxes_[i].folder), ec);  // 仅空目录可删，文件已放回桌面
    }
    if (root_ && boxes_[i].visual) root_->RemoveVisual(boxes_[i].visual.Get());
    boxes_.erase(boxes_.begin() + i);  // 目录与文件保留（删盒不清文件）
    Relayout();
    if (on_boxes_changed_) on_boxes_changed_();
}

std::wstring DesktopGrid::DisplayName(const Box& b) const {
    return b.name;
}



void DesktopGrid::RedrawBox(int i) {
    Box& b = boxes_[i];
    ClampScroll(b);
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
        ComPtr<::ID2D1SolidColorBrush> hl;
        ctx->CreateSolidColorBrush(D2D1_COLOR_F{1.f, 1.f, 1.f, 0.25f}, hl.GetAddressOf());

        const D2D1_ROUNDED_RECT rr{{0.5f, 0.5f, w - 0.5f, h - 0.5f}, 12.f, 12.f};
        ctx->FillRoundedRectangle(rr, fill.Get());

        // 头栏：类名（就地编辑态则画缓冲 + 选中高亮 + 光标） + 计数
        if (renaming_ && i == rename_box_) {
            D2D1_RECT_F tr{12.f, 6.f, w - 40.f, kHeaderH};
            if (rename_selA_ < rename_selB_) {
                float sx = tr.left + HeaderTextWidth(rename_buf_.substr(0, rename_selA_));
                float ex = tr.left + HeaderTextWidth(rename_buf_.substr(0, rename_selB_));
                if (ex > tr.right - 2) ex = tr.right - 2;
                if (ex > sx) ctx->FillRectangle(D2D1::RectF(sx, 9.f, ex, kHeaderH - 2), hl.Get());
            }
            if (!rename_buf_.empty())
                ctx->DrawText(rename_buf_.c_str(), (UINT32)rename_buf_.size(), headerFmt_.Get(),
                              tr, text.Get());
            float cx = tr.left + HeaderTextWidth(rename_buf_.substr(0, rename_caret_));
            if (cx > tr.right - 2) cx = tr.right - 2;
            if (caret_on_)
                ctx->FillRectangle(D2D1::RectF(cx, 9.f, cx + 2.f, kHeaderH - 2), dim.Get());
        } else {
            std::wstring title = DisplayName(b);
            ctx->DrawText(title.c_str(), (UINT32)title.size(), headerFmt_.Get(),
                          {12.f, 6.f, w - 40.f, kHeaderH}, text.Get());
        }
        std::wstring count;
        if (IsTasksBox(b)) {  // 日程格：已办/总数
            auto it = tasks_.find(SelectedKey());
            int total = it != tasks_.end() ? (int)it->second.size() : 0;
            int done = 0;
            if (it != tasks_.end())
                for (auto& tk : it->second)
                    if (tk.done) ++done;
            count = std::to_wstring(done) + L"/" + std::to_wstring(total);
        } else {
            count = std::to_wstring(b.files.size());
        }
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
            if (IsTasksBox(b)) {  // 日程格：日期条 + 事项 + 添加行
                DrawTasksBox(ctx, i, w, h);
            } else {
                size_t rows = std::min<size_t>(b.files.size(), kMaxRows);
                for (size_t r = 0; r < rows; ++r) {
                    int idx = b.scroll + (int)r;
                    if (idx < 0 || idx >= (int)b.files.size()) break;
                    std::wstring name =
                        EllipsizeW(fs::path(b.files[idx]).filename().wstring(), w - 12.f - 36.f - 2.f);
                    float y = kHeaderH + kPad * 0.4f + static_cast<float>(r) * kRowH;
                    if (auto* icon = icons_.Get(ctx, b.files[idx])) {
                        ctx->DrawBitmap(icon, D2D1::RectF(14.f, y + 3.f, 30.f, y + 19.f), 1.f,
                                        D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
                    }
                    ctx->DrawText(name.c_str(), (UINT32)name.size(), rowFmt_.Get(),
                                  {36.f, y, w - 12.f, y + kRowH + 3.f}, text.Get());
                }
                // 超出上限 → 滚动条（淡入淡出）
                if (b.files.size() > kMaxRows)
                    DrawScroll(ctx, w, kHeaderH + kPad * 0.4f,
                               kHeaderH + kPad * 0.4f + rows * kRowH, (int)rows,
                               (int)b.files.size(), b.scroll,
                               scrollbar_box_ == i ? sb_alpha_ : 0.f);
            }
        }
    });
    if (surface) {
        b.visual->SetContent(surface.Get());
        gfx_->device->Commit();
    }
}

void DesktopGrid::DrawTasksBox(::ID2D1DeviceContext* ctx, int ti, float w, float h) const {
    (void)h;
    ComPtr<::ID2D1SolidColorBrush> text, dim, hl, accent, white;
    ctx->CreateSolidColorBrush(D2D1_COLOR_F{1.f, 1.f, 1.f, 0.92f}, text.GetAddressOf());
    ctx->CreateSolidColorBrush(D2D1_COLOR_F{1.f, 1.f, 1.f, 0.55f}, dim.GetAddressOf());
    ctx->CreateSolidColorBrush(D2D1_COLOR_F{1.f, 1.f, 1.f, 0.25f}, hl.GetAddressOf());
    ctx->CreateSolidColorBrush(D2D1_COLOR_F{0.133f, 0.827f, 0.933f, 0.85f}, accent.GetAddressOf());
    ctx->CreateSolidColorBrush(D2D1_COLOR_F{1.f, 1.f, 1.f, 0.92f}, white.GetAddressOf());

    float dy = kHeaderH;  // 日期条顶
    // ◀ / ▶
    ctx->DrawText(L"\u25C0", 1, rowFmt_.Get(), {kPad, dy + 2.f, kPad + 18.f, dy + kDateH},
                  dim.Get());
    ctx->DrawText(L"\u25B6", 1, rowFmt_.Get(), {w - kPad - 18.f, dy + 2.f, w - kPad, dy + kDateH},
                  dim.Get());
    // 日期
    std::wstring lbl = DateLabel({curY_, curM_, curD_});
    ctx->DrawText(lbl.c_str(), (UINT32)lbl.size(), rowFmt_.Get(),
                  {kPad + 26.f, dy + 2.f, w - kPad - 26.f, dy + kDateH}, text.Get());
    ctx->DrawLine({10.f, dy + kDateH}, {w - 10.f, dy + kDateH}, dim.Get(), 0.7f);

    auto it = tasks_.find(SelectedKey());
    const std::vector<TaskRec>* lst = it != tasks_.end() ? &it->second : nullptr;
    size_t shown = lst ? std::min<size_t>(lst->size(), kMaxRows) : 0;
    float y0 = dy + kDateH + kPad * 0.4f;
    for (size_t r = 0; r < shown; ++r) {
        int idx = boxes_[ti].scroll + (int)r;
        if (idx < 0 || idx >= (int)lst->size()) break;
        const TaskRec& tk = (*lst)[idx];
        float ry = y0 + static_cast<float>(r) * kRowH;
        // 打勾框：未完成=描边，完成=青色填充+对勾
        D2D1_ROUNDED_RECT cb{{14.f, ry + 5.f, 28.f, ry + 19.f}, 3.f, 3.f};
        if (tk.done) {
            ctx->FillRoundedRectangle(cb, accent.Get());
            ctx->DrawLine({16.f, ry + 12.f}, {20.f, ry + 16.f}, white.Get(), 1.6f);
            ctx->DrawLine({20.f, ry + 16.f}, {26.f, ry + 8.f}, white.Get(), 1.6f);
        } else {
            ctx->DrawRoundedRectangle(cb, hl.Get(), 1.f);
        }
        // 事项文本
        std::wstring nm = EllipsizeW(tk.text, w - 10.f - 36.f - 2.f);
        auto* br = tk.done ? dim.Get() : text.Get();
        if (tasks_editing_ && task_edit_box_ == ti && task_edit_row_ == idx) {
            D2D1_RECT_F tr{36.f, ry, w - 10.f, ry + kRowH};
            if (task_selA_ < task_selB_) {
                float sx = tr.left + RowTextWidth(task_buf_.substr(0, task_selA_));
                float ex = tr.left + RowTextWidth(task_buf_.substr(0, task_selB_));
                if (ex > tr.right - 2) ex = tr.right - 2;
                if (ex > sx) ctx->FillRectangle(D2D1::RectF(sx, ry + 2.f, ex, ry + kRowH - 3), hl.Get());
            }
            if (!task_buf_.empty())
                ctx->DrawText(task_buf_.c_str(), (UINT32)task_buf_.size(), rowFmt_.Get(), tr,
                              text.Get());
            float cx = tr.left + RowTextWidth(task_buf_.substr(0, task_caret_));
            if (cx > tr.right - 2) cx = tr.right - 2;
            if (caret_on_)
                ctx->FillRectangle(D2D1::RectF(cx, ry + 2.f, cx + 2.f, ry + kRowH - 3), dim.Get());
        } else {
            ctx->DrawText(nm.c_str(), (UINT32)nm.size(), rowFmt_.Get(),
                          {36.f, ry, w - 10.f, ry + kRowH + 3.f}, br);
        }
    }
    // 添加行
    float ay = y0 + static_cast<float>(shown) * kRowH;
    if (tasks_editing_ && task_edit_box_ == ti && task_edit_row_ < 0) {
        std::wstring pfx = L"\uFF0B ";  // ＋
        D2D1_RECT_F tr{14.f, ay, w - 10.f, ay + kRowH};
        if (task_selA_ < task_selB_) {
            float base = tr.left + HeaderTextWidth(pfx);
            float sx = base + HeaderTextWidth(task_buf_.substr(0, task_selA_));
            float ex = base + HeaderTextWidth(task_buf_.substr(0, task_selB_));
            if (ex > tr.right - 2) ex = tr.right - 2;
            if (ex > sx) ctx->FillRectangle(D2D1::RectF(sx, ay + 2.f, ex, ay + kRowH - 3), hl.Get());
        }
        std::wstring shownStr = pfx + task_buf_;
        ctx->DrawText(shownStr.c_str(), (UINT32)shownStr.size(), rowFmt_.Get(), tr, text.Get());
        float cx = tr.left + RowTextWidth(pfx + task_buf_.substr(0, task_caret_));
        if (cx > tr.right - 2) cx = tr.right - 2;
        if (caret_on_)
            ctx->FillRectangle(D2D1::RectF(cx, ay + 2.f, cx + 2.f, ay + kRowH - 3), dim.Get());
    } else {
        const std::wstring addHint = L"\uFF0B \u6DFB\u52A0\u4E8B\u9879";  // ＋ 添加事项
        ctx->DrawText(addHint.c_str(), (UINT32)addHint.size(), rowFmt_.Get(),
                      {14.f, ay, w - 10.f, ay + kRowH + 3.f}, dim.Get());
    }
    // 事项超出上限 → 滚动条（淡入淡出）
    if (lst && lst->size() > kMaxRows)
        DrawScroll(ctx, w, y0, ay, (int)shown, (int)lst->size(), boxes_[ti].scroll,
                   scrollbar_box_ == ti ? sb_alpha_ : 0.f);
}

void DesktopGrid::DrawScroll(ID2D1DeviceContext* ctx, float w, float y0, float y1, int shown,
                             int total, int scroll, float alpha) const {
    if (alpha <= 0.01f) return;  // 未显示
    if (total <= shown) return;
    float trackH = y1 - y0;
    if (trackH <= 0) return;
    ComPtr<::ID2D1SolidColorBrush> track, thumb;
    ctx->CreateSolidColorBrush(D2D1_COLOR_F{1.f, 1.f, 1.f, 0.22f * alpha}, track.GetAddressOf());
    ctx->CreateSolidColorBrush(D2D1_COLOR_F{1.f, 1.f, 1.f, 0.55f * alpha}, thumb.GetAddressOf());
    ctx->FillRectangle(D2D1::RectF(w - 8.f, y0, w - 5.f, y1), track.Get());  // 轨道
    float th = std::max(6.f, trackH * ((float)shown / (float)total));  // 滑块
    float ty = y0 + (trackH - th) * ((float)scroll / (float)(total - shown));
    ctx->FillRectangle(D2D1::RectF(w - 8.f, ty, w - 5.f, ty + th), thumb.Get());
}

void DesktopGrid::ClampScroll(Box& b) {
    int total = 0;
    if (IsTasksBox(b)) {
        auto it = tasks_.find(SelectedKey());
        total = it != tasks_.end() ? (int)it->second.size() : 0;
    } else {
        total = (int)b.files.size();
    }
    int shown = std::min(total, kMaxRows);
    int maxScroll = std::max(0, total - shown);
    if (b.scroll < 0) b.scroll = 0;
    if (b.scroll > maxScroll) b.scroll = maxScroll;
}

void DesktopGrid::Refresh() {
    if (!org_) return;
    for (auto& b : boxes_) {
        if (IsTasksBox(b)) {  // 日程格不列文件
            b.files.clear();
            b.scroll = 0;
            continue;
        }
        b.files = org_->FilesInFolder(b.folder);
        std::sort(b.files.begin(), b.files.end(), [](const std::wstring& a, const std::wstring& c) {
            return _wcsicmp(fs::path(a).filename().c_str(), fs::path(c).filename().c_str()) < 0;
        });
        ClampScroll(b);
    }
    Relayout();
    for (int i = 0; i < (int)boxes_.size(); ++i) RedrawBox(i);
}

void DesktopGrid::ResetToToday() {
    ADate t = TodayDate();
    curY_ = t.y;
    curM_ = t.m;
    curD_ = t.d;
    CancelTaskEdit();
    for (int i = 0; i < (int)boxes_.size(); ++i)
        if (IsTasksBox(boxes_[i])) boxes_[i].scroll = 0;  // 回今天回到顶部
    Relayout();
    for (int i = 0; i < (int)boxes_.size(); ++i)
        if (IsTasksBox(boxes_[i])) RedrawBox(i);
    SaveTasks();
}

void DesktopGrid::LoadTasks() {
    tasks_.clear();
    if (tasks_file_.empty()) return;
    std::ifstream f(tasks_file_, std::ios::binary);
    if (!f) return;
    try {
        nlohmann::json j = nlohmann::json::parse(f);
        if (auto it = j.find("selDate"); it != j.end() && it->is_string()) {
            std::wstring s = ToWide(it->get<std::string>());
            int y = 0, m = 0, d = 0;
            if (swscanf_s(s.c_str(), L"%04d-%02d-%02d", &y, &m, &d) == 3 && m >= 1 && m <= 12 &&
                d >= 1 && d <= DimOf(y, m)) {
                curY_ = y;
                curM_ = m;
                curD_ = d;
            }
        }
        auto it = j.find("tasks");
        if (it != j.end() && it->is_object()) {
            for (auto& [k, v] : it->items()) {
                std::vector<TaskRec> vec;
                for (auto& e : v) {
                    TaskRec r;
                    r.text = ToWide(e.value("text", std::string()));
                    r.done = e.value("done", false);
                    vec.push_back(std::move(r));
                }
                tasks_[ToWide(k)] = std::move(vec);
            }
        }
    } catch (...) {
    }
}

void DesktopGrid::SaveTasks() {
    if (tasks_file_.empty()) return;
    nlohmann::json tj = nlohmann::json::object();
    for (auto& [k, vec] : tasks_) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& r : vec)
            arr.push_back(nlohmann::json{{"text", ToUtf8(r.text)}, {"done", r.done}});
        tj[ToUtf8(k)] = std::move(arr);
    }
    nlohmann::json j;
    j["selDate"] = ToUtf8(SelectedKey());
    j["tasks"] = std::move(tj);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(tasks_file_).parent_path(), ec);
    std::ofstream f(tasks_file_, std::ios::binary);
    f << j.dump(2);
}

void DesktopGrid::StartTaskEdit(int boxIdx, int rowIdx) {
    if (boxIdx < 0 || boxIdx >= (int)boxes_.size()) return;
    tasks_editing_ = true;
    task_edit_box_ = boxIdx;
    task_edit_row_ = rowIdx;  // rowIdx<0 → 新增行
    auto it = tasks_.find(SelectedKey());
    if (rowIdx >= 0 && it != tasks_.end() && (size_t)rowIdx < it->second.size())
        task_buf_ = it->second[(size_t)rowIdx].text;
    else
        task_buf_.clear();
    task_caret_ = (int)task_buf_.size();
    task_selA_ = 0;
    task_selB_ = (int)task_buf_.size();  // 全选，直接输入替换
    caret_on_ = true;
    if (hwnd_) {
        SetFocus(hwnd_);
        SetTimer(hwnd_, kCaretTimer, kCaretBlink, nullptr);  // 光标闪烁
    }
    RedrawBox(boxIdx);
}

void DesktopGrid::CommitTaskEdit() {
    if (!tasks_editing_) return;
    int boxIdx = task_edit_box_, rowIdx = task_edit_row_;
    std::wstring txt = task_buf_;
    tasks_editing_ = false;
    task_edit_box_ = -1;
    if (hwnd_) KillTimer(hwnd_, kCaretTimer);
    size_t a = txt.find_first_not_of(L" \t\r\n");  // 去首尾空白
    if (a != std::wstring::npos) {
        size_t b2 = txt.find_last_not_of(L" \t\r\n");
        txt = txt.substr(a, b2 - a + 1);
    } else
        txt.clear();
    auto& vec = tasks_[SelectedKey()];
    if (rowIdx >= 0 && (size_t)rowIdx < vec.size()) {  // 编辑既有
        if (!txt.empty())
            vec[(size_t)rowIdx].text = txt;
        else
            vec.erase(vec.begin() + rowIdx);
    } else if (!txt.empty()) {  // 新增
        vec.push_back({txt, false});
    }
    if (vec.empty()) tasks_.erase(SelectedKey());
    SaveTasks();
    Relayout();
    RedrawBox(boxIdx);
}

void DesktopGrid::CancelTaskEdit() {
    if (!tasks_editing_) return;
    int boxIdx = task_edit_box_;
    tasks_editing_ = false;
    task_edit_box_ = -1;
    if (hwnd_) KillTimer(hwnd_, kCaretTimer);
    RedrawBox(boxIdx);
}

int DesktopGrid::BoxAt(float x, float y) const {
    // 客户区原点是窗口左上，grid 坐标原点是包围盒左上——自由布局下二者有偏移
    return BoxAtGrid(x + bbox_.left, y + bbox_.top);
}

int DesktopGrid::BoxAtGrid(float x, float y) const {
    for (int i = (int)boxes_.size() - 1; i >= 0; --i) {  // 倒序：重叠时上层（后添加）优先
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
    int actual = b.scroll + idx;  // 滚动偏移 → 实际下标
    if (actual < 0 || actual >= (int)b.files.size()) return -1;
    return actual;
}

DesktopGrid::TaskHit DesktopGrid::TaskHitTest(const Box& b, float x, float y,
                                              int& taskIdx) const {
    taskIdx = -1;
    if (b.collapsed) return TaskHit::Header;
    float relY = y - b.rect.top;
    if (relY < kHeaderH) return TaskHit::Header;
    float w = kBoxW, lx = x - b.rect.left;
    float bodyY = relY - kHeaderH;
    if (bodyY < kDateH) {  // 日期条：◀ / ▶ / 日期文字
        if (lx >= kPad && lx <= kPad + 22) return TaskHit::PrevDate;
        if (lx >= w - kPad - 22 && lx <= w - kPad) return TaskHit::NextDate;
        return TaskHit::DateLabel;
    }
    float relList = bodyY - kDateH - kPad * 0.4f;
    if (relList < 0) return TaskHit::None;
    int idx = static_cast<int>(relList / kRowH);
    auto it = tasks_.find(SelectedKey());
    const std::vector<TaskRec>* lst = it != tasks_.end() ? &it->second : nullptr;
    size_t shown = lst ? std::min<size_t>(lst->size(), kMaxRows) : 0;
    if (idx < 0) return TaskHit::None;
    if ((size_t)idx < shown) {
        taskIdx = b.scroll + idx;  // 滚动偏移 → 实际下标
        if (lx <= 36.f) return TaskHit::Check;  // 打勾区
        return TaskHit::Row;
    }
    if ((size_t)idx == shown) return TaskHit::AddRow;
    return TaskHit::None;
}

void DesktopGrid::OpenFile(const std::wstring& path) const {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void DesktopGrid::RowMenu(HWND hwnd, int boxIdx, const std::wstring& path) {
    (void)boxIdx;
    if (!shell_menu_) shell_menu_ = std::make_unique<ShellCtxMenu>();
    if (shell_menu_->Build(hwnd, path)) {
        POINT pt;
        GetCursorPos(&pt);
        int custom = shell_menu_->Show(hwnd, pt.x, pt.y);
        if (custom == (int)ShellCtxMenu::kCustomBackToDesktop) {
            if (org_ && org_->MoveBackToDesktop(path)) Refresh();
        }
        shell_menu_->Reset();
        return;
    }
    // 外壳菜单不可用（如文件已不存在）→ 回退简单菜单
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

LRESULT DesktopGrid::OnShellMenuMsg(UINT msg, WPARAM wp, LPARAM lp) {
    if (shell_menu_) return shell_menu_->HandleMsg(msg, wp, lp);
    return 0;
}

void DesktopGrid::HeaderMenu(HWND hwnd, int boxIdx) {
    if (boxIdx < 0 || boxIdx >= (int)boxes_.size()) return;
    bool tasks = boxes_[boxIdx].isTasks;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"重命名此格…");
    AppendMenuW(m, MF_STRING, 2, L"清空此格（放回桌面）");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (tasks ? MF_DISABLED : 0), 3, L"删除此容器…");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                             pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(m);
    switch (cmd) {
    case 1:
        StartRename(boxIdx);  // 就地编辑格名（不再弹窗）
        break;
    case 2:
        if (on_clear_box_) on_clear_box_(boxIdx);
        break;
    case 3: {  // 删除容器：文件先放回桌面，再删盒子（待办不可删）
        if (tasks) break;
        std::wstring msg =
            L"\u786E\u5B9A\u5220\u9664\u5BB9\u5668\u300C" + boxes_[boxIdx].name +
            L"\u300D\u5417\uFF1F\n\u5176\u4E2D\u7684\u6587\u4EF6\u4F1A\u653E\u56DE\u684C\u9762\u3002";  // 确定删除容器「X」吗？\n其中的文件会放回桌面。
        if (MessageBoxW(hwnd, msg.c_str(), L"LumenBox", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) !=
            IDYES)
            break;
        if (!boxes_[boxIdx].folder.empty() && org_)
            for (auto& p : org_->FilesInFolder(boxes_[boxIdx].folder)) org_->MoveBackToDesktop(p);
        RemoveBox(boxIdx);
        Refresh();
        break;
    }
    }
}

float DesktopGrid::HeaderTextWidth(const std::wstring& s) const {
    if (s.empty()) return 0.f;
    ComPtr<IDWriteTextLayout> lay;
    if (FAILED(dwrite_->CreateTextLayout(s.c_str(), (UINT32)s.size(), headerFmt_.Get(),
                                         kBoxW - 40.f, kHeaderH, &lay)))
        return 0.f;
    DWRITE_TEXT_METRICS m{};
    lay->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

float DesktopGrid::RowTextWidth(const std::wstring& s) const {
    if (s.empty()) return 0.f;
    ComPtr<IDWriteTextLayout> lay;
    if (FAILED(dwrite_->CreateTextLayout(s.c_str(), (UINT32)s.size(), rowFmt_.Get(),
                                         kBoxW - 40.f, kRowH, &lay)))
        return 0.f;
    DWRITE_TEXT_METRICS m{};
    lay->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

std::wstring DesktopGrid::EllipsizeW(const std::wstring& s, float maxW) const {
    if (s.empty()) return s;
    if (RowTextWidth(s) <= maxW) return s;
    std::wstring tail = L"\u2026";  // …
    size_t n = s.size();
    while (n > 0) {
        std::wstring cut = s.substr(0, --n) + tail;
        if (RowTextWidth(cut) <= maxW) return cut;
    }
    return tail;
}

void DesktopGrid::StartRename(int boxIdx) {
    if (boxIdx < 0 || boxIdx >= (int)boxes_.size()) return;
    renaming_ = true;
    rename_box_ = boxIdx;
    rename_buf_ = boxes_[boxIdx].name;
    rename_caret_ = (int)rename_buf_.size();
    rename_selA_ = 0;
    rename_selB_ = (int)rename_buf_.size();  // 打开即全选，直接输入替换
    caret_on_ = true;
    if (hwnd_) {
        SetFocus(hwnd_);
        SetTimer(hwnd_, kCaretTimer, kCaretBlink, nullptr);  // 光标闪烁
    }
    RedrawBox(boxIdx);
}

void DesktopGrid::CommitRename() {
    if (!renaming_) return;
    int boxIdx = rename_box_;
    renaming_ = false;
    if (hwnd_) KillTimer(hwnd_, kCaretTimer);
    std::wstring name = rename_buf_;
    if (!name.empty() && name != boxes_[boxIdx].name) {
        boxes_[boxIdx].name = name;
        if (on_boxes_changed_) on_boxes_changed_();  // 宿主持久化
    }
    RedrawBox(boxIdx);
}

void DesktopGrid::CancelRename() {
    if (!renaming_) return;
    int boxIdx = rename_box_;
    renaming_ = false;
    if (hwnd_) KillTimer(hwnd_, kCaretTimer);
    RedrawBox(boxIdx);
}

bool DesktopGrid::OnKeyboard(UINT msg, UINT wp) {
    if (msg == WM_KILLFOCUS) {  // 失焦（打开搜索/切窗）→ 提交
        if (tasks_editing_) {
            CommitTaskEdit();
            return true;
        }
        if (!renaming_) return false;
        CommitRename();
        return true;
    }
    if (tasks_editing_) {  // 日程格事项编辑（新增/修改）
        if (msg == WM_CHAR) {
            if (wp >= 0x20 && wp <= 0xFFFF) {
                if (task_selA_ < task_selB_) {
                    task_buf_.erase(task_selA_, task_selB_ - task_selA_);
                    task_caret_ = task_selA_;
                    task_selA_ = task_selB_ = 0;
                }
                task_buf_.insert(task_caret_, 1, (wchar_t)wp);
                ++task_caret_;
                RedrawBox(task_edit_box_);
            }
            return true;
        }
        if (msg == WM_KEYDOWN) {
            switch ((int)wp) {
            case VK_LEFT:  if (task_caret_ > 0) --task_caret_; task_selA_ = task_selB_ = 0; break;
            case VK_RIGHT: if (task_caret_ < (int)task_buf_.size()) ++task_caret_; task_selA_ = task_selB_ = 0; break;
            case VK_HOME:  task_caret_ = 0; task_selA_ = task_selB_ = 0; break;
            case VK_END:   task_caret_ = (int)task_buf_.size(); task_selA_ = task_selB_ = 0; break;
            case VK_BACK:
                if (task_selA_ < task_selB_) {
                    task_buf_.erase(task_selA_, task_selB_ - task_selA_);
                    task_caret_ = task_selA_; task_selA_ = task_selB_ = 0;
                } else if (task_caret_ > 0) {
                    task_buf_.erase(task_caret_ - 1, 1);
                    --task_caret_;
                }
                break;
            case VK_DELETE:
                if (task_selA_ < task_selB_) {
                    task_buf_.erase(task_selA_, task_selB_ - task_selA_);
                    task_caret_ = task_selA_; task_selA_ = task_selB_ = 0;
                } else if (task_caret_ < (int)task_buf_.size()) {
                    task_buf_.erase(task_caret_, 1);
                }
                break;
            case VK_RETURN: CommitTaskEdit(); return true;
            case VK_ESCAPE: CancelTaskEdit(); return true;
            default: return true;  // 编辑期吞掉其他键
            }
            RedrawBox(task_edit_box_);
            return true;
        }
        return true;
    }
    if (!renaming_) return false;
    if (msg == WM_CHAR) {
        if (wp >= 0x20 && wp <= 0xFFFF) {
            if (rename_selA_ < rename_selB_) {
                rename_buf_.erase(rename_selA_, rename_selB_ - rename_selA_);
                rename_caret_ = rename_selA_;
                rename_selA_ = rename_selB_ = 0;
            }
            rename_buf_.insert(rename_caret_, 1, (wchar_t)wp);
            ++rename_caret_;
            RedrawBox(rename_box_);
        }
        return true;
    }
    if (msg == WM_KEYDOWN) {
        switch ((int)wp) {
        case VK_LEFT:  if (rename_caret_ > 0) --rename_caret_; rename_selA_ = rename_selB_ = 0; break;
        case VK_RIGHT: if (rename_caret_ < (int)rename_buf_.size()) ++rename_caret_; rename_selA_ = rename_selB_ = 0; break;
        case VK_HOME:  rename_caret_ = 0; rename_selA_ = rename_selB_ = 0; break;
        case VK_END:   rename_caret_ = (int)rename_buf_.size(); rename_selA_ = rename_selB_ = 0; break;
        case VK_BACK:
            if (rename_selA_ < rename_selB_) {
                rename_buf_.erase(rename_selA_, rename_selB_ - rename_selA_);
                rename_caret_ = rename_selA_; rename_selA_ = rename_selB_ = 0;
            } else if (rename_caret_ > 0) {
                rename_buf_.erase(rename_caret_ - 1, 1);
                --rename_caret_;
            }
            break;
        case VK_DELETE:
            if (rename_selA_ < rename_selB_) {
                rename_buf_.erase(rename_selA_, rename_selB_ - rename_selA_);
                rename_caret_ = rename_selA_; rename_selA_ = rename_selB_ = 0;
            } else if (rename_caret_ < (int)rename_buf_.size()) {
                rename_buf_.erase(rename_caret_, 1);
            }
            break;
        case VK_RETURN: CommitRename(); return true;
        case VK_ESCAPE: CancelRename(); return true;
        default: return true;  // 编辑期吞掉其他键
        }
        RedrawBox(rename_box_);
        return true;
    }
    return true;  // 编辑期其他消息一律吞掉
}

void DesktopGrid::OnMouse(HWND hwnd, UINT msg, float x, float y) {
    hwnd_ = hwnd;
    x += bbox_.left;  // 客户区 → grid 坐标（矩形/命中都在 grid 空间）
    y += bbox_.top;
    if (msg == WM_LBUTTONDOWN) {
        if (renaming_) CommitRename();        // 编辑中点击别处 → 提交（格名）
        if (tasks_editing_) CommitTaskEdit();  // 事项编辑中点击别处 → 提交
    }
    int b = BoxAtGrid(x, y);
    switch (msg) {
    case WM_MOUSEMOVE: {
        if (file_press_) {  // 文件行按下：超过系统拖放阈值才发起 OLE 拖动（避免单击抖动误触发）
            POINT now{};
            GetCursorPos(&now);
            int dx = now.x - drag_cursor0_.x, dy = now.y - drag_cursor0_.y;
            int t = GetSystemMetrics(SM_CXDRAG);  // 系统“拖动”判定距离（默认 4px）
            if (!drag_moved_ && (abs(dx) > t || abs(dy) > t)) drag_moved_ = true;
            if (drag_moved_) {
                ReleaseCapture();
                std::wstring p = pending_file_;
                file_press_ = false;
                pending_file_.clear();
                dragging_ = false;
                box_drag_ = -1;
                StartFileDrag(p);  // 拖到桌面/资源管理器/其他格
                Refresh();                // 放置后重新列格，纳入桌面/跨盒移动
            }
            return;
        }
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
                // 实时高亮交换目标（拖动盒中心落点盒）
                float cx = box_rect0_.left + ddx + kBoxW / 2;
                float cy = box_rect0_.top + ddy + HeightOf(b) / 2;
                SetDragBox(BoxAtGrid(cx, cy));
            } else {  // 整窗
                SetWindowPos(hwnd_, nullptr, drag_rect0_.left + dx, drag_rect0_.top + dy, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return;
        }
        int row = b >= 0 ? RowIndexAt(boxes_[b], x, y) : -1;
        bool hand = row >= 0;
        if (b >= 0 && IsTasksBox(boxes_[b])) {  // 日程格：互动区手型
            int tk = -1;
            switch (TaskHitTest(boxes_[b], x, y, tk)) {
            case TaskHit::PrevDate:
            case TaskHit::NextDate:
            case TaskHit::Check:
            case TaskHit::AddRow:
            case TaskHit::Row:
                hand = true;
                break;
            default:
                hand = false;
                break;
            }
        }
        SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
        if (b != hovered_) {
            int old = hovered_;
            hovered_ = b;
            if (old >= 0) RedrawBox(old);
            if (b >= 0) RedrawBox(b);
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        calendar_.Close();  // 点别处先关月历
        if (b >= 0 && IsTasksBox(boxes_[b])) {  // 日程格：日期导航/打勾/添加，其余交拖动
            int tk = -1;
            TaskHit th = TaskHitTest(boxes_[b], x, y, tk);
            if (th == TaskHit::Header || th == TaskHit::None) {  // 头栏/空区 → 拖动该单格
                SetCapture(hwnd);
                dragging_ = true;
                box_drag_ = b;
                drag_index0_ = b;
                drag_on_header_ = (th == TaskHit::Header);
                drag_moved_ = false;
                GetCursorPos(&drag_cursor0_);
                box_rect0_ = boxes_[b].rect;
                SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                break;
            }
            switch (th) {
            case TaskHit::PrevDate:
            case TaskHit::NextDate: {
                ADate d = {curY_, curM_, curD_};
                d = AddDays(d, th == TaskHit::PrevDate ? -1 : 1);
                curY_ = d.y;
                curM_ = d.m;
                curD_ = d.d;
                CancelTaskEdit();
                boxes_[b].scroll = 0;  // 换日期回到顶部
                Relayout();
                for (int i = 0; i < (int)boxes_.size(); ++i)
                    if (IsTasksBox(boxes_[i])) RedrawBox(i);
                SaveTasks();
                break;
            }
            case TaskHit::Check: {
                auto it = tasks_.find(SelectedKey());
                if (it != tasks_.end() && tk >= 0 && (size_t)tk < it->second.size()) {
                    it->second[(size_t)tk].done = !it->second[(size_t)tk].done;
                    SaveTasks();
                    RedrawBox(b);
                }
                break;
            }
            case TaskHit::AddRow:
                StartTaskEdit(b, -1);
                break;
            case TaskHit::Row:
                break;  // 单击行不动；双击编辑；右键删除
            case TaskHit::DateLabel: {  // 点日期 → 打开月历选择
                POINT pt{
                    (LONG)((boxes_[b].rect.left + kPad - bbox_.left) * dpi_),
                    (LONG)((boxes_[b].rect.top + kHeaderH + kDateH + 4.f) * dpi_)};
                ClientToScreen(hwnd_, &pt);
                calendar_.Open(hwnd_, curY_, curM_, curD_, pt.x, pt.y);
                break;
            }
            default:
                break;
            }
            break;
        }
        int row = b >= 0 ? RowIndexAt(boxes_[b], x, y) : -9;
        {
            char t[96];
            snprintf(t, 96, "d:down b=%d row=%d x=%.0f y=%.0f", b, row, x, y);
            StageTrace(t);
        }
        if (b >= 0 && row >= 0) {  // 文件行：按下即为拖出单个文件的候选
            file_press_ = true;
            pending_file_ = boxes_[b].files[row];
            drag_moved_ = false;
            GetCursorPos(&drag_cursor0_);
            SetCapture(hwnd);
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
        } else if (b >= 0) {  // 头栏 / 内边距 / 空卡片体 → 拖动该单盒
            SetCapture(hwnd);
            dragging_ = true;
            box_drag_ = b;
            drag_index0_ = b;
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
        if (file_press_) {  // 文件行单击（未移动）：不做动作；双击由 DBLCLK 打开
            ReleaseCapture();
            file_press_ = false;
            pending_file_.clear();
            SetCursor(LoadCursorW(nullptr, b >= 0 ? IDC_HAND : IDC_ARROW));
            return;
        }
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
            if (!moved) {  // 未位移的头栏单击 → 延时折叠（约250ms，双击改名则不折叠）
                StageTrace("d:up click-header");
                if (drag_on_header_) {
                    collapse_pending_ = bd;
                    SetTimer(hwnd, kCollapseTimer, kCollapseDelay, nullptr);
                }
                break;
            }
            // 换位：松手光标位置（拖动盒中心）落点盒；没落到盒子则保持原位（归位）
            SetDragBox(-1);  // 清除拖动中的目标高亮
            float cx = box_rect0_.left + drag_delta_.x / dpi_ + kBoxW / 2;
            float cy = box_rect0_.top + drag_delta_.y / dpi_ + HeightOf(boxes_[bd]) / 2;
            int target = BoxAtGrid(cx, cy);
            if (bd < 0 || bd >= (int)boxes_.size()) break;  // 越界保护
            if (target >= 0 && target != bd) {
                std::swap(boxes_[target], boxes_[bd]);  // 交换两盒（含视觉）
                Relayout();
                if (on_moved_) on_moved_(false);
            } else {
                Relayout();  // 未落到其他盒 → 吸附回原位
            }
            break;
        }
        break;  // 其余 UP（含双击第二次 UP）不折叠：折叠仅走延时定时器
    }
    case WM_LBUTTONDBLCLK: {
        KillTimer(hwnd, kCollapseTimer);  // 双击：取消待折叠（还未来得及折叠，故不闪）
        collapse_pending_ = -1;
        if (b >= 0 && IsTasksBox(boxes_[b])) {  // 日程格：双击行编辑/双击日期回今天
            int tk = -1;
            TaskHit th = TaskHitTest(boxes_[b], x, y, tk);
            if (th == TaskHit::Row && tk >= 0)
                StartTaskEdit(b, tk);
            else if (th == TaskHit::DateLabel)
                ResetToToday();
            break;
        }
        if (b >= 0) {
            int row = RowIndexAt(boxes_[b], x, y);
            if (row == -2)
                StartRename(b);                        // 双击格头 → 就地改名
            else if (row >= 0)
                OpenFile(boxes_[b].files[row]);        // 双击文件行 → 打开
        }
        break;
    }
    case WM_RBUTTONUP: {
        if (b >= 0 && IsTasksBox(boxes_[b])) {  // 日程格：右键事项 → 删除/编辑
            int tk = -1;
            TaskHit th = TaskHitTest(boxes_[b], x, y, tk);
            if (th == TaskHit::Row && tk >= 0) {
                HMENU m = CreatePopupMenu();
                AppendMenuW(m, MF_STRING, 1, L"编辑");
                AppendMenuW(m, MF_STRING, 2, L"删除");
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                         pt.x, pt.y, 0, hwnd, nullptr);
                DestroyMenu(m);
                auto it = tasks_.find(SelectedKey());
                if (it != tasks_.end() && tk >= 0 && (size_t)tk < it->second.size()) {
                    if (cmd == 1)
                        StartTaskEdit(b, tk);
                    else if (cmd == 2) {
                        it->second.erase(it->second.begin() + tk);
                        if (it->second.empty()) tasks_.erase(it);
                        SaveTasks();
                        Relayout();
                        RedrawBox(b);
                    }
                }
            } else if (th == TaskHit::Header) {
                HeaderMenu(hwnd, b);
            }
            break;
        }
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
        file_press_ = false;
        pending_file_.clear();
        break;
    }
}

void DesktopGrid::OnWheel(int delta, float x, float y) {
    x += bbox_.left;  // 客户区 DIP → grid 坐标
    y += bbox_.top;
    int b = BoxAtGrid(x, y);
    if (b < 0) return;
    Box& bx = boxes_[b];
    if (bx.collapsed) return;
    int total = 0;
    if (IsTasksBox(bx)) {
        auto it = tasks_.find(SelectedKey());
        if (it == tasks_.end()) return;
        total = (int)it->second.size();
    } else {
        total = (int)bx.files.size();
    }
    int shown = std::min(total, kMaxRows);
    if (total <= shown) return;  // 内容未超上限，无需滚动
    int maxScroll = total - shown;
    bx.scroll -= (delta / 120) * 2;  // 每格滚 2 行；上滚(正值)向开头，下滚(负值)向末尾
    if (bx.scroll < 0) bx.scroll = 0;
    if (bx.scroll > maxScroll) bx.scroll = maxScroll;
    // 滚动时滚动条淡入，停止 1100ms 后淡出
    scrollbar_box_ = b;
    sb_target_ = 1.f;
    SetTimer(hwnd_, kScrollbarFade, 1100, nullptr);  // 停止滚动后开始淡出
    SetTimer(hwnd_, kScrollbarTick, 30, nullptr);    // 立即开始淡入动画
    RedrawBox(b);
}

void DesktopGrid::AnimateIn(HWND hwnd) {
    for (UINT i = 0; i < boxes_.size(); ++i)
        SetTimer(hwnd, kTimerBase + i, 30 * i, nullptr);
    hwnd_ = hwnd;
}

void DesktopGrid::OnTimer(HWND hwnd, UINT id) {
    if (id == kCaretTimer) {  // 编辑光标闪烁
        caret_on_ = !caret_on_;
        if (renaming_) RedrawBox(rename_box_);
        else if (tasks_editing_) RedrawBox(task_edit_box_);
        return;
    }
    if (id == kScrollbarFade) {  // 停止滚动 1100ms 后开始淡出
        KillTimer(hwnd, kScrollbarFade);
        sb_target_ = 0.f;
        SetTimer(hwnd, kScrollbarTick, 30, nullptr);  // 淡出动画
        return;
    }
    if (id == kScrollbarTick) {  // 滚动条淡入淡出动画帧
        const float step = 0.08f;
        if (sb_alpha_ < sb_target_) sb_alpha_ = std::min(sb_target_, sb_alpha_ + step);
        else sb_alpha_ = std::max(sb_target_, sb_alpha_ - step);
        if (sb_alpha_ == sb_target_) {
            KillTimer(hwnd, kScrollbarTick);
            if (sb_target_ == 0.f) scrollbar_box_ = -1;  // 完全隐藏
        }
        if (scrollbar_box_ >= 0) RedrawBox(scrollbar_box_);
        return;
    }
    if (id == kCollapseTimer) {  // 单击格头延时折叠（双击则取消并改名，避免闪烁）
        KillTimer(hwnd, kCollapseTimer);
        if (collapse_pending_ >= 0) {
            int p = collapse_pending_;
            collapse_pending_ = -1;
            boxes_[p].collapsed = !boxes_[p].collapsed;
            Relayout();
            RedrawBox(p);
            if (on_moved_) on_moved_(false);
        }
        return;
    }
    if (id < kTimerBase || id >= kTimerBase + boxes_.size()) return;
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
