#include "ui/clip_panel.hpp"
#include "core/strutil.hpp"
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <ctime>

namespace gd = Gdiplus;

namespace {
// ---- 设计 token（ui-ux-pro-max：Slate 深色�?+ Teal 强调，扁平无阴影�?---
// 对比度均 �?4.5:1�?F1F5F9/#0F172A�?6:1�?94A3B8/#0F172A�?.4:1�?2DD4BF/#0F172A�?:1
const gd::Color kPanelBg(255, 15, 23, 42);     // #0F172A slate-900 面板底（须不透明：
                                               // 分层窗 alpha<255 会让桌面内容透出，实测很脏）
const gd::Color kPanelStroke(255, 45, 58, 78); // #2D3A4E 面板描边
const gd::Color kText(255, 241, 245, 249);     // #F1F5F9 slate-100 主文字
const gd::Color kTextMeta(255, 148, 163, 184); // #94A3B8 slate-400 元信息
const gd::Color kTextDim(255, 100, 116, 139);  // #64748B slate-500 弱提示
const gd::Color kAccent(255, 20, 184, 166);    // #14B8A6 teal-500 强调
const gd::Color kAccentText(255, 45, 212, 191);// #2DD4BF teal-400 强调文字
const gd::Color kSelRow(255, 24, 51, 60);      // 选中行：teal 调深实底
const gd::Color kHoverRow(255, 30, 41, 59);    // #1E293B slate-800 悬停底
const gd::Color kThumbBg(255, 11, 17, 32);     // #0B1120 缩略图衬底
const gd::Color kLine(255, 41, 53, 72);        // #293548 分隔线/按钮描边
const gd::Color kRowSep(255, 30, 41, 59);      // 行间细分隔线
const gd::Color kPin(255, 245, 158, 11);       // #F59E0B amber 置顶标记
const gd::Color kDanger(255, 239, 68, 68);     // #EF4444 破坏性操作
const gd::Color kPaused(255, 245, 158, 11);    // 暂停态琥珀
const gd::Color kPreviewBg(255, 15, 23, 42);   // 悬停预览窗底（不透明，同面板）
const gd::Color kToastBg(255, 15, 23, 42);     // Toast 底（不透明）
}  // namespace

namespace lb {
namespace {
ULONG_PTR g_token = 0;
void EnsureGdiplus() {
if (!g_token) {
Gdiplus::GdiplusStartupInput si;
Gdiplus::GdiplusStartup(&g_token, &si, nullptr);
}
}

// 时间元信息：今天/昨天/更早
std::wstring TimeStr(long long ts) {
std::time_t t = static_cast<std::time_t>(ts);
std::tm tm{};
localtime_s(&tm, &t);
std::time_t now = std::time(nullptr);
std::tm nt{};
localtime_s(&nt, &now);
wchar_t buf[40];
if (tm.tm_year == nt.tm_year && tm.tm_yday == nt.tm_yday)
wcsftime(buf, 40, L"今天 %H:%M", &tm);
else if (tm.tm_year == nt.tm_year && tm.tm_yday + 1 == nt.tm_yday)
wcsftime(buf, 40, L"昨天 %H:%M", &tm);
else
wcsftime(buf, 40, L"%m-%d %H:%M", &tm);
return buf;
}

// 载荷总大�?
std::wstring SizeStr(const ClipEntry& e) {
uint64_t s = 0;
for (auto& p : e.payloads) s += p.size;
wchar_t buf[32];
if (s >= 1024 * 1024) swprintf(buf, 32, L"%.1f MB", s / 1048576.0);
else if (s >= 1024) swprintf(buf, 32, L"%u KB", (unsigned)(s / 1024));
else swprintf(buf, 32, L"%u B", (unsigned)s);
return buf;
}

std::wstring TypeTag(const std::string& t) {
if (t == "html") return L"表格";
if (t == "files") return L"文件";
if (t == "image") return L"图片";
return L"文本";
}

// 图片扩展名（GDI+ 可解码的常见位图�?
bool IsImageFile(const std::wstring& path) {
std::wstring e = LowerExt(path);
return e == L"png" || e == L"jpg" || e == L"jpeg" || e == L"gif" ||
e == L"bmp" || e == L"dib" || e == L"tif" || e == L"tiff";
}

// 内存 DIB 上跑 GDI+ 整帧绘制 �?UpdateLayeredWindow 原子呈现（分层窗通用�?
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
g.SetInterpolationMode(gd::InterpolationModeHighQualityBicubic);
g.ScaleTransform(scale, scale);
paint(g);
}
        // 预乘 alpha（GDI+ 输出非预乘，ULW 要求预乘）
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
}  // namespace

// ---------------------------------------------------------- 生命周期

void ClipPanel::Toggle() {
if (created_ && IsWindowVisible(hwnd_)) {
OnClose();
} else {
if (!created_) {
EnsureGdiplus();
WNDCLASSW wc{};
wc.lpfnWndProc = &WndProc;
wc.hInstance = GetModuleHandleW(nullptr);
wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
wc.style = CS_DBLCLKS;  // 否则 WM_LBUTTONDBLCLK 永不触发（双击复制为死代码）
wc.lpszClassName = L"LumenBoxClipPanel";
RegisterClassW(&wc);
scale_ = GetDpiForSystem() / 96.f;
int w = (int)(kW * scale_), h = (int)((kHeaderH + 10 + kRowH * 4) * scale_);
hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
wc.lpszClassName, L"LumenBox 剪贴板", WS_POPUP,
CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr,
wc.hInstance, this);
if (!hwnd_) return;
created_ = true;
scale_ = GetDpiForWindow(hwnd_) / 96.f;  // 实际窗口 DPI（Per-Monitor V2）
}
OnOpen();
}
}

void ClipPanel::OnOpen() {
clearArm_ = false;
sel_ = 0;
scroll_ = 0;
Refresh();
MoveToTop();
ShowWindow(hwnd_, SW_SHOW);
SetForegroundWindow(hwnd_);
SetFocus(hwnd_);
}

// 呼出定位：跟随光标所在显示器。常规高度锚在垂�?1/3 处；较高（≥半屏）时垂直居中�?// 一律夹进工作区，保证完整可见�?
void ClipPanel::MoveToTop() {
POINT pt;
GetCursorPos(&pt);
HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
MONITORINFO mi{sizeof(mi)};
if (!GetMonitorInfoW(mon, &mi)) return;
int waW = mi.rcWork.right - mi.rcWork.left;
int waH = mi.rcWork.bottom - mi.rcWork.top;
int w = (int)(kW * scale_), h = (int)(winH_ * scale_);
int x = mi.rcWork.left + (waW - w) / 2;
int y = h >= waH / 2 ? mi.rcWork.top + (waH - h) / 2
: mi.rcWork.top + (waH / 3 - h / 2);
y = std::max((int)mi.rcWork.top + 8, std::min(y, (int)mi.rcWork.bottom - h - 8));
SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
float ns = GetDpiForWindow(hwnd_) / 96.f;  // 跨屏呼出：跟随目标屏 DPI 重绘
if (ns != scale_) {
scale_ = ns;
FitHeight();
Draw();
}
}

// 打开状态下内容增长导致窗口变高：把窗口夹回工作区，避免底边/顶边被遮�?
void ClipPanel::ClampIntoWork() {
RECT r{};
GetWindowRect(hwnd_, &r);
HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
MONITORINFO mi{sizeof(mi)};
if (!GetMonitorInfoW(mon, &mi)) return;
int w = r.right - r.left, h = r.bottom - r.top;
int x = r.left, y = r.top;
if (y + h > (int)mi.rcWork.bottom - 8) y = (int)mi.rcWork.bottom - 8 - h;
if (y < (int)mi.rcWork.top + 8) y = (int)mi.rcWork.top + 8;
if (x + w > (int)mi.rcWork.right - 8) x = (int)mi.rcWork.right - 8 - w;
if (x < (int)mi.rcWork.left + 8) x = (int)mi.rcWork.left + 8;
if (x != r.left || y != r.top)
SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void ClipPanel::OnClose() {
if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
HidePreview();
HideToast();
hoverRow_ = -1;
KillTimer(hwnd_, 1);
KillTimer(hwnd_, 3);
scrollAlpha_ = 0.f;
scrollFading_ = false;
ClearThumbs();
clearArm_ = false;
}

void ClipPanel::OnStoreChanged() {
if (!created_ || !IsWindowVisible(hwnd_)) return;
scroll_ = 0;  // 新条目置顶可�?
Refresh();
}

// ---------------------------------------------------------- 数据 / �?

std::vector<int> ClipPanel::VisibleRows() const {
std::vector<int> out;
for (int i = 0; i < (int)items_.size(); ++i) {
const ClipEntry& e = items_[i];
std::string t = EffType(e);
bool hit = filter_ == 0 || (filter_ == 1 && (t == "text" || t == "html")) ||
(filter_ == 2 && t == "image") || (filter_ == 3 && t == "files");
if (hit) out.push_back(i);
}
return out;
}

int ClipPanel::VisibleCount() const {
return std::max(1, (winH_ - kHeaderH - 10) / kRowH);
}

void ClipPanel::Refresh() {
if (store_) items_ = store_->Snapshot();
HidePreview();  // 条目可能已变化：收起预览
hoverRow_ = -1;
KillTimer(hwnd_, 1);
rows_ = VisibleRows();
if (sel_ >= (int)rows_.size()) sel_ = (int)rows_.size() - 1;
if (sel_ < 0) sel_ = 0;
FitHeight();
scroll_ = std::min(std::max(scroll_, 0),
std::max(0, (int)rows_.size() - VisibleCount()));
Draw();
if (created_ && IsWindowVisible(hwnd_)) ClampIntoWork();  // 增高后防遮挡
}

void ClipPanel::FitHeight() {
int maxH = 0;
RECT work{};
if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) maxH = work.bottom - work.top;
maxH = (int)(maxH * 0.7 / scale_);  // 逻辑像素
int want = kHeaderH + 10 + (int)rows_.size() * kRowH;
winH_ = std::min(std::max(want, kHeaderH + 10 + kRowH), maxH);
int pw = (int)(kW * scale_), ph = (int)(winH_ * scale_);
SetWindowPos(hwnd_, nullptr, 0, 0, pw, ph, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void ClipPanel::ClearThumbs() {
for (auto& kv : thumbs_) delete static_cast<gd::Bitmap*>(kv.second);
thumbs_.clear();
}

void* ClipPanel::GetThumb(const ClipEntry& e) {
auto it = thumbs_.find(e.id);
if (it != thumbs_.end()) return it->second;
if (!store_) return nullptr;
auto* bmp = new gd::Bitmap(store_->ThumbPath(e).c_str());
if (bmp->GetLastStatus() != gd::Ok) {
delete bmp;
return nullptr;  // 缩略图缺失容错（blob 清理后重启等?
}
thumbs_[e.id] = bmp;
return bmp;
}

std::vector<std::wstring> ClipPanel::DropPaths(const ClipEntry& e) const {
auto it = pathCache_.find(e.id);
if (it != pathCache_.end()) return it->second;
std::vector<std::wstring> out;
auto split = [&](const std::string& bytes) {
out.clear();
size_t start = 0;
while (start < bytes.size()) {
size_t nl = bytes.find('\n', start);
if (nl == std::string::npos) nl = bytes.size();
std::string one = bytes.substr(start, nl - start);
if (!one.empty() && one.back() == '\r') one.pop_back();
if (!one.empty()) out.push_back(ToWide(one));
start = nl + 1;
}
};
std::string bytes;
if (store_ && store_->LoadPayload(e, "hdrop", bytes)) split(bytes);
if (out.empty() && store_ && store_->LoadPayload(e, "text", bytes)) {
    // hdrop 载荷缺失兜底（部分 .NET 应用的 OLE 延迟渲染取不到 HDROP）：
    // 文件复制的文本呈现即逐行路径——须每行都像绝对路径才采纳：
    split(bytes);
    for (auto& s : out)
        if (s.find(L":\\") == std::wstring::npos ||
            s.find_first_of(L"\"\t") != std::wstring::npos) {
            out.clear();
            break;
        }
}
pathCache_[e.id] = out;
return out;
}

void* ClipPanel::GetFileThumb(const std::wstring& path) {
std::string key = "f:" + ToUtf8(path);
auto it = thumbs_.find(key);
if (it != thumbs_.end()) return it->second;
auto* src = new gd::Bitmap(path.c_str());  // 只读文件头，懒解�?
if (src->GetLastStatus() != gd::Ok) {
delete src;
return nullptr;  // 文件已删/占用/非位图 → 占位图兜底
}
auto* th = src->GetThumbnailImage(256, 256, nullptr, nullptr);
delete src;
if (!th || th->GetLastStatus() != gd::Ok) {
delete th;
return nullptr;
}
thumbs_[key] = th;
return th;
}

// 展示归类：全部路径都是图片文件的 files 条目�?图片"呈现（标�?筛�?悬停一致）
std::string ClipPanel::EffType(const ClipEntry& e) const {
if (e.type == "files") {
const auto& paths = DropPaths(e);
if (!paths.empty()) {
bool allImg = true;
for (auto& p : paths)
if (!IsImageFile(p)) { allImg = false; break; }
if (allImg) return "image";
}
}
return e.type;
}

bool ClipPanel::CopyRow(int row) {
if (row < 0 || row >= (int)rows_.size()) return false;
const ClipEntry& e = items_[rows_[row]];
if (!copyFn_) return false;
if (copyFn_(e)) {
OnClose();  // 先关面板（清预览/计时器），再弹提�?
ShowToast(L"已复制");
return true;
}
return false;
}

// ---------------------------------------------------------- 布局常量

namespace {
RECT PinRect(int y) { return {560 - 96, y + 20, 560 - 70, y + 40}; }
RECT DelRect(int y) { return {560 - 64, y + 20, 560 - 38, y + 40}; }
bool InRect(const RECT& r, int x, int y) {
return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}
}  // namespace

int ClipPanel::HitHeader(int x, int y) const {
if (y >= kHeaderH) return 0;
// Tab：与绘制同参数（kTabX0=14, kTabW=62, kTabGap=6）
for (int i = 0; i < 4; ++i) {
    float x0 = 14.f + i * 68.f;
    if (x >= x0 && x < x0 + 62.f) return 100 + i;
}
// 右侧按钮：关闭 28px（最右）→ 清空 56px → 暂停 56px
if (x >= kW - 40 && x < kW - 12) return 202;            // 关闭
if (x >= kW - 104 && x < kW - 48) return 201;           // 清空
if (x >= kW - 168 && x < kW - 112) return 200;          // 暂停/恢复
return 0;
}

int ClipPanel::HitRow(int x, int y) const {
if (y < kHeaderH) return -1;
int row = scroll_ + (y - kHeaderH) / kRowH;
if (row < 0 || row >= (int)rows_.size()) return -1;
int ry = kHeaderH + (row - scroll_) * kRowH;
if (InRect(PinRect(ry), x, y)) return 4000 + row;
if (InRect(DelRect(ry), x, y)) return 5000 + row;
return row;  // 行本体：点击即复?
}

// ---------------------------------------------------------- 绘制

void ClipPanel::Draw() {
if (!hwnd_) return;
int w = kW, h = winH_ ? winH_ : kHeaderH + 10 + kRowH * 4;
int pw = (int)(w * scale_), ph = (int)(h * scale_);
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
if (!bmp) {
DeleteDC(mem);
ReleaseDC(nullptr, screen);
return;
}
HGDIOBJ old = SelectObject(mem, bmp);
{
gd::Graphics g(mem);
g.SetSmoothingMode(gd::SmoothingModeAntiAlias);
// CJK 必须用 GridFit：它对小字号中文字形做整像素对齐，笔画锐利；
// 换成纯 AntiAlias 会让 CJK 笔画发虚、相邻笔画粘连成重影（实测）。
g.SetTextRenderingHint(gd::TextRenderingHintAntiAliasGridFit);
g.ScaleTransform(scale_, scale_);

gd::SolidBrush clear(gd::Color(0, 0, 0, 0));
g.FillRectangle(&clear, gd::RectF(0, 0, (gd::REAL)w, (gd::REAL)h));

        // 圆角玻璃卡面（与聚光灯同款）
gd::GraphicsPath path;
const float r = 16.f;
path.AddArc(0.5f, 0.5f, r, r, 180, 90);
path.AddArc(w - r - 0.5f, 0.5f, r, r, 270, 90);
path.AddArc(w - r - 0.5f, h - r - 0.5f, r, r, 0, 90);
path.AddArc(0.5f, h - r - 0.5f, r, r, 90, 90);
path.CloseFigure();
gd::SolidBrush bg(kPanelBg);
g.FillPath(&bg, &path);
gd::Pen stroke(kPanelStroke, 1.f);
g.DrawPath(&stroke, &path);

        // ---- 头部：过滤 Tab（圆角胶囊，间距 8px；高度随 kHeaderH 居中）----
const wchar_t* tabs[4] = {L"全部", L"文本", L"图片", L"文件"};
gd::Font fTab(L"Microsoft YaHei UI", 14.f, gd::FontStyleRegular, gd::UnitPixel);
constexpr float kTabW = 62.f, kTabGap = 6.f, kTabH = 28.f;
constexpr float kTabX0 = 14.f;
float tabY = ((gd::REAL)kHeaderH - kTabH) / 2.f;
gd::StringFormat tsf;
tsf.SetAlignment(gd::StringAlignmentCenter);
tsf.SetLineAlignment(gd::StringAlignmentCenter);
tsf.SetFormatFlags(gd::StringFormatFlagsNoWrap);
for (int i = 0; i < 4; ++i) {
gd::RectF rc(kTabX0 + i * (kTabW + kTabGap), tabY, kTabW, kTabH);
// 文本矩形：向下留出整行高度，避免 GDI+ 行距导致 CJK 字形溢出胶囊（实测溢出会
// 在胶囊外再画一份，看起来像"重影"）
gd::RectF trc(rc.X, rc.Y, rc.Width, kTabH + 8.f);
if (i == filter_) {  // 选中：teal 实底圆角胶囊
    // 注意 GDI+ AddArc(x,y,w,h) 的 w/h 是椭圆【外接矩形】= 直径，不是半径。
    // 半圆直径须取整个高度 kTabH（曾误传 kTabH/2 → 胶囊仅 14px 高、文字溢出成重影）
    gd::GraphicsPath cap;
    float d = kTabH;
    cap.AddArc(rc.X, rc.Y, d, d, 90, 180);
    cap.AddArc(rc.X + rc.Width - d, rc.Y, d, d, 270, 180);
    cap.CloseFigure();
    gd::SolidBrush act(kAccent);
    g.FillPath(&act, &cap);
    gd::SolidBrush tc(gd::Color(255, 11, 17, 32));  // 深色文字：teal 底上对比度更高
    g.DrawString(tabs[i], -1, &fTab, trc, &tsf, &tc);
} else {
    gd::SolidBrush tc(kTextMeta);
    g.DrawString(tabs[i], -1, &fTab, trc, &tsf, &tc);
}
}
        // ---- 头部右侧：暂停 / 清空 / 关闭（文字胶囊按钮，44px 点击宽度）----
gd::Font fBtn(L"Microsoft YaHei UI", 12.f, gd::FontStyleRegular, gd::UnitPixel);
gd::StringFormat sf;
sf.SetAlignment(gd::StringAlignmentCenter);
sf.SetLineAlignment(gd::StringAlignmentCenter);
bool paused = pausedGet_ ? pausedGet_() : false;
constexpr float kBtnW = 56.f, kBtnH = 26.f, kBtnGap = 8.f;
float btnY = ((gd::REAL)kHeaderH - kBtnH) / 2.f;
// 关闭按钮占最右 28px，其左依次是 清空、暂停
float closeX = (gd::REAL)kW - 36.f;
float clearX = closeX - kBtnGap - kBtnW;
float pauseX = clearX - kBtnGap - kBtnW;
auto pill = [&](float x, const wchar_t* label, bool danger, bool activeFill,
                gd::Color textCol, gd::Color borderCol) {
    gd::RectF rc(x, btnY, kBtnW, kBtnH);
    // 同上：AddArc 的 w/h 是直径（= 按钮全高），误当半径会让胶囊高度减半
    gd::GraphicsPath cap;
    float d = kBtnH;
    cap.AddArc(rc.X, rc.Y, d, d, 90, 180);
    cap.AddArc(rc.X + rc.Width - d, rc.Y, d, d, 270, 180);
    cap.CloseFigure();
    if (activeFill) {
        gd::SolidBrush fill(danger ? kDanger : kHoverRow);
        g.FillPath(&fill, &cap);
    } else {
        gd::Pen bp(borderCol, 1.f);
        g.DrawPath(&bp, &cap);
    }
    gd::SolidBrush tc(textCol);
    g.DrawString(label, -1, &fBtn, rc, &sf, &tc);
};
pill(pauseX, paused ? L"已暂停" : L"暂停", false, false,
     paused ? kPaused : kTextMeta, kLine);
pill(clearX, clearArm_ ? L"确认？" : L"清空", true, clearArm_,
     clearArm_ ? kText : kTextMeta, kDanger);
{  // 关闭：X 图标，带悬停反馈区
gd::RectF rc(closeX, btnY, 24.f, kBtnH);
if (closeHot_) {
    gd::SolidBrush hb(kHoverRow);
    g.FillRectangle(&hb, rc);
}
gd::SolidBrush closecol(kTextMeta);
gd::Pen closePen(&closecol, 1.6f);
float cx = closeX + 6.f, cy = btnY + 7.f;
g.DrawLine(&closePen, cx, cy, cx + 12.f, cy + 12.f);
g.DrawLine(&closePen, cx, cy + 12.f, cx + 12.f, cy);
}
        // 分隔线
gd::SolidBrush sep(kLine);
g.FillRectangle(&sep, gd::RectF(8.f, (gd::REAL)kHeaderH, (gd::REAL)(w - 16), 1.f));

        // ---- 空状�?----
if (rows_.empty()) {
gd::Font fEmpty(L"Microsoft YaHei UI", 13.f, gd::FontStyleRegular, gd::UnitPixel);
gd::SolidBrush tc(kTextDim);
gd::StringFormat cf;
cf.SetAlignment(gd::StringAlignmentCenter);
gd::RectF rc(0.f, (gd::REAL)(kHeaderH + 20), (gd::REAL)w, 30.f);
g.DrawString(L"暂无剪贴板历史", -1, &fEmpty, rc, &cf, &tc);
gd::RectF rc2(0.f, (gd::REAL)(kHeaderH + 52), (gd::REAL)w, 24.f);
gd::Font fSub(L"Microsoft YaHei UI", 10.f, gd::FontStyleRegular, gd::UnitPixel);
g.DrawString(L"复制任意内容后自动记录", -1, &fSub, rc2, &cf, &tc);
}

        // ---- �?----
gd::Font fMeta(L"Microsoft YaHei UI", 10.f, gd::FontStyleRegular, gd::UnitPixel);
gd::Font fPrev(L"Microsoft YaHei UI", 12.f, gd::FontStyleRegular, gd::UnitPixel);
int vis = VisibleCount();
int n = (int)std::min(rows_.size(), (size_t)vis);
gd::StringFormat trim;
trim.SetTrimming(gd::StringTrimmingEllipsisCharacter);
for (int vi = 0; vi < n; ++vi) {
int row = scroll_ + vi;
const ClipEntry& e = items_[rows_[row]];
float y = (gd::REAL)(kHeaderH + vi * kRowH);
// 行底：选中 > 悬停 > 无（三者互斥，避免叠加出脏色）
if (row == sel_) {
    gd::SolidBrush hl(kSelRow);
    g.FillRectangle(&hl, gd::RectF(8.f, y + 2.f, (gd::REAL)(w - 16), (gd::REAL)kRowH - 4.f));
} else if (row == hoverRow_) {
    gd::SolidBrush hv(kHoverRow);
    g.FillRectangle(&hv, gd::RectF(8.f, y + 2.f, (gd::REAL)(w - 16), (gd::REAL)kRowH - 4.f));
}
if (e.pinned) {  // 置顶：左缘琥珀条
    gd::SolidBrush pin(kPin);
    g.FillRectangle(&pin, gd::RectF(8.f, y + 2.f, 3.f, (gd::REAL)kRowH - 4.f));
}
// 行间细分隔线（最后一行不画）
if (vi + 1 < n) {
    gd::SolidBrush sep(kRowSep);
    g.FillRectangle(&sep, gd::RectF(16.f, y + (gd::REAL)kRowH - 1.5f,
                                    (gd::REAL)(w - 32), 1.f));
}
            // 左：缩略图（图片条目 / 指向图片文件的文件条目）或类型徽标
constexpr float kBoxImg = 52.f, kBoxBadge = 36.f;
float bx = 16.f;                 // 内容左边界
void* thv = nullptr;
bool isImgRow = (e.type == "image");
bool iconOnly = false;           // 无缩略图的类型徽标
float box = kBoxBadge;
if (isImgRow) {
    thv = GetThumb(e);
    box = kBoxImg;
} else if (e.type == "files") {
    for (auto& p : DropPaths(e)) {
        if (IsImageFile(p)) { thv = GetFileThumb(p); break; }
    }
    if (EffType(e) == "image") box = kBoxImg;  // 全图文件条目：与图片行同尺寸
}
if (!thv) iconOnly = true;
// 缩略图垂直居中于行内（行高扣除上下各 2px 内边距）
float rowTop = y + 2.f, rowH = (gd::REAL)kRowH - 4.f;
float by = rowTop + (rowH - box) / 2.f;
if (thv) {
auto* th = static_cast<gd::Bitmap*>(thv);
UINT iw = th->GetWidth(), ih = th->GetHeight();
if (iw && ih) {
// 缩略图衬底（极扁/极窄图也能看出边界）
gd::SolidBrush plate(kThumbBg);
g.FillRectangle(&plate, gd::RectF(bx, by, box, box));
gd::InterpolationMode oldMode = g.GetInterpolationMode();
g.SetInterpolationMode(gd::InterpolationModeHighQualityBicubic);
float aspect = (float)iw / (float)ih;
if (aspect > 3.0f || aspect < 0.34f) {
    // 极端长宽比（如 696×37 聊天截图 / 长图）：等比放入会缩成看不清的细线，
    // 改为「填满衬底 + 居中裁剪」，至少能辨认内容
    if (aspect > 1.f) {  // 宽扁：横向裁剪
        gd::REAL visibleW = ih;  // 取 1:1 高度对应的宽度，剩余左右裁掉
        if (visibleW > iw) visibleW = (gd::REAL)iw;
        gd::REAL sx = ((gd::REAL)iw - visibleW) / 2.f;
        gd::RectF dst(bx, by, (gd::REAL)box, (gd::REAL)box);
        g.DrawImage(th, dst, sx, 0.f, visibleW, (gd::REAL)ih, gd::UnitPixel);
    } else {  // 高窄：纵向裁剪
        gd::REAL visibleH = iw;
        if (visibleH > ih) visibleH = (gd::REAL)ih;
        gd::REAL sy = ((gd::REAL)ih - visibleH) / 2.f;
        gd::RectF dst(bx, by, (gd::REAL)box, (gd::REAL)box);
        g.DrawImage(th, dst, 0.f, sy, (gd::REAL)iw, visibleH, gd::UnitPixel);
    }
} else {
    gd::REAL s = std::min((gd::REAL)box / iw, (gd::REAL)box / ih);
    gd::REAL dw = iw * s, dh = ih * s;
    g.DrawImage(th, bx + (box - dw) / 2.f, by + (box - dh) / 2.f, dw, dh);
}
g.SetInterpolationMode(oldMode);
if (row == sel_) {  // 选中态：青色描边
gd::Pen tp(kAccent, 1.5f);
g.DrawRectangle(&tp, bx, by, (gd::REAL)box, (gd::REAL)box);
}
if (e.type == "files" || e.type == "image") {  // 多图角标：+N
int imgTotal = 0;
if (e.type == "files") {
for (auto& p : DropPaths(e))
if (IsImageFile(p)) ++imgTotal;
} else {
for (auto& p : e.payloads)
if (p.fmt == "png") ++imgTotal;
}
if (imgTotal > 1) {
std::wstring n = L"+" + std::to_wstring(imgTotal - 1);
gd::SolidBrush nb(kAccent);
gd::RectF nrc(bx + box - 24, by + box - 18, 22.f, 16.f);
g.FillRectangle(&nb, nrc);
gd::SolidBrush ntc(gd::Color(255, 12, 15, 23));
gd::StringFormat nf2;
nf2.SetAlignment(gd::StringAlignmentCenter);
nf2.SetLineAlignment(gd::StringAlignmentCenter);
g.DrawString(n.c_str(), -1, &fTab, nrc, &nf2, &ntc);
}
}
}
} else if (e.type == "image") {
                // 缩略图缺�?解码失败：占位块 + 图字
gd::SolidBrush phb(kThumbBg);
gd::RectF rc(bx, by, (gd::REAL)box, (gd::REAL)box);
g.FillRectangle(&phb, rc);
gd::SolidBrush tc(kTextMeta);
gd::StringFormat cf;
cf.SetAlignment(gd::StringAlignmentCenter);
cf.SetLineAlignment(gd::StringAlignmentCenter);
g.DrawString(L"图", -1, &fTab, rc, &cf, &tc);
} else {
gd::SolidBrush badge(kThumbBg);
gd::RectF rc(bx, by, 36.f, 36.f);
g.FillRectangle(&badge, rc);
gd::SolidBrush tc(kTextMeta);
gd::StringFormat cf;
cf.SetAlignment(gd::StringAlignmentCenter);
cf.SetLineAlignment(gd::StringAlignmentCenter);
const wchar_t* glyph = e.type == "html"  ? L"表"
: e.type == "files" ? L"件"
: L"文";
g.DrawString(glyph, -1, &fTab, rc, &cf, &tc);
}
            // 中：元信息 + 预览（图片条目无文本预览——内容即图，元信息带尺寸）
// 文本列起点：缩略图区（16..16+box）+ 12px 间距；右端留出按钮区宽度
float tx = bx + box + 12.f;
float tw = (gd::REAL)(kW - tx - 100.f);
std::wstring meta = TimeStr(e.ts);
if (!e.source.empty()) meta += L" · " + e.source;
meta += L" · " + TypeTag(EffType(e)) + L" · " + SizeStr(e);
if (e.type == "image" && e.imgW > 0)
meta += L" · " + std::to_wstring(e.imgW) + L"×" + std::to_wstring(e.imgH);
gd::SolidBrush mcol(kTextMeta);
// 元信息 14px 行 + 预览 2 行 34px = 48px，整体在行内垂直居中
bool hasPreview = (e.type != "image");
float metaH = 15.f, prevH = hasPreview ? 34.f : 0.f;
float blockH = metaH + (hasPreview ? 4.f + prevH : 0.f);
float textTop = rowTop + (rowH - blockH) / 2.f;
g.DrawString(meta.c_str(), (INT)meta.size(), &fMeta,
gd::RectF(tx, textTop, tw, metaH), &trim, &mcol);
if (hasPreview) {
std::wstring prev;
if (e.type == "files") {
// files 行预览由路径载荷现场生成（历史条目的 e.text 可能是旧版误存的裸路径列表）
auto paths = DropPaths(e);
if (!paths.empty()) {
for (size_t i = 0; i < paths.size() && i < 3; ++i) {
auto& f = paths[i];
prev += f.substr(f.find_last_of(L"\\/") + 1);
if (i + 1 < paths.size() && i < 2) prev += L"、";
}
if (paths.size() > 3) prev += L"…";
}
} else {
prev = e.text;
}
bool noPrev = prev.empty();
gd::SolidBrush pcol(noPrev ? kTextDim : kText);
if (noPrev) {
gd::StringFormat vc;
vc.SetLineAlignment(gd::StringAlignmentCenter);
g.DrawString(L"（无预览内容）", -1, &fPrev,
gd::RectF(tx, textTop + metaH + 4.f, tw, prevH), &vc, &pcol);
} else {
g.DrawString(prev.c_str(), (INT)prev.size(), &fPrev,
gd::RectF(tx, textTop + metaH + 4.f, tw, prevH), &trim, &pcol);  // 两行预览
}
}
            // 右：�?/ 删（MDL2 图标；点击行本体即复制，无独立复制按钮）
auto drawBtn = [&](const RECT& rcIn, const wchar_t* glyph, bool active) {
gd::RectF rc((gd::REAL)rcIn.left, (gd::REAL)rcIn.top,
(gd::REAL)(rcIn.right - rcIn.left),
(gd::REAL)(rcIn.bottom - rcIn.top));
if (active) {
gd::SolidBrush fill(kAccent);
g.FillRectangle(&fill, rc);
} else {
gd::Pen bp(kLine, 1.f);
g.DrawRectangle(&bp, rc);
}
gd::Font fIco(L"Segoe MDL2 Assets", 12.f, gd::FontStyleRegular,
gd::UnitPixel);
gd::SolidBrush tc(active ? kText
: kTextMeta);
g.DrawString(glyph, -1, &fIco, rc, &sf, &tc);
};
drawBtn(PinRect((int)y), L"\uE718", e.pinned);  // 图钉
drawBtn(DelRect((int)y), L"\uE74D", false);     // 垃圾?
}

        // ---- 滚动条指示（滚动时显示，停止�?0.8s 后淡出） ----
if ((int)rows_.size() > vis && scrollAlpha_ > 0.f) {
float trackY0 = (gd::REAL)(kHeaderH + 4);
float trackH = (gd::REAL)(h - kHeaderH - 10);
float thumbH = std::max(20.f, trackH * vis / rows_.size());
float t = rows_.size() > vis
                          ? (float)scroll_ / (float)(rows_.size() - vis)
                          : 0.f;
float ty = trackY0 + (trackH - thumbH) * t;
gd::SolidBrush sb(gd::Color((int)(220 * scrollAlpha_), 20, 184, 166));
g.FillRectangle(&sb, gd::RectF((gd::REAL)(w - 4), ty, 2.5f, thumbH));
}
}

    // 预乘 alpha（与聚光灯一致）
auto* px = static_cast<unsigned*>(bits);
for (int i = 0; i < pw * ph; ++i) {
unsigned a = (px[i] >> 24) & 0xFF;
px[i] = (a << 24) |
((((px[i] >> 16) & 0xFF) * a / 255) << 16) |
((((px[i] >> 8) & 0xFF) * a / 255) << 8) |
(((px[i] & 0xFF) * a / 255));
}
POINT ptSrc{0, 0};
SIZE sz{pw, ph};
BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
UpdateLayeredWindow(hwnd_, nullptr, nullptr, &sz, mem, &ptSrc, 0, &bf, ULW_ALPHA);
SelectObject(mem, old);
DeleteObject(bmp);
DeleteDC(mem);
ReleaseDC(nullptr, screen);
}

// ---------------------------------------------------------- 悬停放大预览

namespace {
constexpr int kPreviewW = 460, kPreviewH = 560;  // 放大预览内容上限（逻辑像素?
}

void ClipPanel::EnsurePreview() {
if (pwnd_) return;
EnsureGdiplus();
WNDCLASSW wc{};
wc.lpfnWndProc = &PreviewWndProc;
wc.hInstance = GetModuleHandleW(nullptr);
wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
wc.lpszClassName = L"LumenBoxClipPreview";
RegisterClassW(&wc);
pwnd_ = CreateWindowExW(
WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
WS_EX_TRANSPARENT,  // 鼠标穿透：悬停判定始终留在面板行上
wc.lpszClassName, L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr,
wc.hInstance, this);
}

LRESULT CALLBACK ClipPanel::PreviewWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
auto* self = reinterpret_cast<ClipPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
if (msg == WM_NCCREATE) {
auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
self = static_cast<ClipPanel*>(cs->lpCreateParams);
SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
return TRUE;
}
switch (msg) {
case WM_PAINT: {  // 分层窗不�?WM_PAINT 绘制
PAINTSTRUCT ps;
BeginPaint(hwnd, &ps);
EndPaint(hwnd, &ps);
return 0;
}
case WM_ERASEBKGND:
return 1;
default:
break;
}
return DefWindowProcW(hwnd, msg, wp, lp);
}

void ClipPanel::UpdateHover(int hrow) {
if (hrow == hoverRow_) return;
HidePreview();
hoverRow_ = hrow;
KillTimer(hwnd_, 1);
if (hrow >= 0) SetTimer(hwnd_, 1, 400, nullptr);  // 悬停 400ms 后弹?
}

void ClipPanel::HidePreview() {
if (pwnd_ && IsWindowVisible(pwnd_)) ShowWindow(pwnd_, SW_HIDE);
prow_ = -1;
pmode_ = 0;
delete static_cast<gd::Bitmap*>(pimg_);
pimg_ = nullptr;
ptext_.clear();
for (auto* b : pimgs_) delete static_cast<gd::Bitmap*>(b);
pimgs_.clear();
pnames_.clear();
}

void ClipPanel::ShowPreview(int row) {
if (!created_ || !IsWindowVisible(hwnd_)) return;
if (row < 0 || row >= (int)rows_.size()) return;
const ClipEntry& e = items_[rows_[row]];
EnsurePreview();
if (!pwnd_) return;

    // 解析内容：图片优先出原图，其次全�?/ 路径列表
pmode_ = 0;
delete static_cast<gd::Bitmap*>(pimg_);
pimg_ = nullptr;
ptext_.clear();
for (auto* b : pimgs_) delete static_cast<gd::Bitmap*>(b);
pimgs_.clear();
pnames_.clear();
pdw_ = pdh_ = 0;
std::string eff = EffType(e);
    // 收集可放大展示的图：捕获图（含聊�?HTML 内嵌图）�?blob，文�?�?原文�?
std::vector<std::pair<std::wstring, std::wstring>> imgs;  // 路径, 标签
if (eff == "image") {
if (e.type == "image") {
for (auto& p : e.payloads)
if (p.fmt == "png" && !p.file.empty())
imgs.emplace_back(store_->BlobPath(e, p), L"");
} else {
for (auto& p : DropPaths(e))
if (IsImageFile(p))
imgs.emplace_back(p, p.substr(p.find_last_of(L"\\/") + 1));
}
if (imgs.size() >= 2) {  // 多图：网格逐张出原图（至多 6 格，避免解码风暴�?
for (size_t i = 0; i < imgs.size() && i < 6; ++i) {
auto* b = new gd::Bitmap(imgs[i].first.c_str());
if (b->GetLastStatus() == gd::Ok && b->GetWidth() && b->GetHeight()) {
pimgs_.push_back(b);
pnames_.push_back(imgs[i].second);
} else {
delete b;
}
}
if (pimgs_.size() >= 2) pmode_ = 3;
}
}
if (pmode_ != 3 && eff == "image") {  // 单图：大图放�?
std::wstring src = imgs.empty() ? std::wstring() : imgs[0].first;
if (!src.empty()) {
auto* b = new gd::Bitmap(src.c_str());
if (b->GetLastStatus() == gd::Ok && b->GetWidth() && b->GetHeight()) {
pimg_ = b;
pmode_ = 1;
} else {
delete b;
}
}
}
if (pmode_ == 0) {  // 仅在没有任何图像内容时才回退文本�?=大图 3=多图网格�?
std::wstring txt;
if (e.type == "text") {  // 全文：内联或 blob
std::string bytes;
if (store_ && store_->LoadPayload(e, "text", bytes)) txt = ToWide(bytes);
else txt = e.text;
} else if (e.type == "html") {
            // 网页复制：html �?text 载荷并存——悬停出正文全文，而非 120 字预�?
std::string bytes;
if (store_ && store_->LoadPayload(e, "text", bytes) && !bytes.empty())
txt = ToWide(bytes);
else
txt = e.text;
} else if (e.type == "files") {
for (auto& p : DropPaths(e)) {
txt += p;
txt += L"\r\n";
}
}
if (txt.size() > 6000) txt = txt.substr(0, 6000) + L"…";
if (!txt.empty()) {
ptext_ = txt;
pmode_ = 2;
}
}
if (pmode_ == 0) {
HidePreview();
return;
}

    // 计算内容尺寸（逻辑像素�?
if (pmode_ == 3) {
        // 网格�? 列（�? 张）�? 列（5-6 张）。每�?4:3 �?+ 18px 名字�?        pcols_ = pimgs_.size() <= 4 ? 2 : 3;
int rows = (int)((pimgs_.size() + pcols_ - 1) / pcols_);
int cellW = (kPreviewW - 24 - (pcols_ - 1) * 8) / pcols_;
int cellH = cellW * 3 / 4;
pdw_ = kPreviewW - 24;
pdh_ = rows * (cellH + 18) + (rows - 1) * 8;
pw_ = kPreviewW;
ph_ = std::min(pdh_ + 24, kPreviewH);
} else if (pmode_ == 1) {
UINT iw = static_cast<gd::Bitmap*>(pimg_)->GetWidth();
UINT ih = static_cast<gd::Bitmap*>(pimg_)->GetHeight();
gd::REAL s = std::min((gd::REAL)kPreviewW / iw, (gd::REAL)kPreviewH / ih);
if (s > 6.f) s = 6.f;  // 小图至多放大 6 倍，避免过度模糊
pdw_ = (int)(iw * s);
pdh_ = (int)(ih * s);
pw_ = pdw_ + 24;
ph_ = pdh_ + 24;
} else {
HDC screen = GetDC(nullptr);
gd::Graphics g(screen);
gd::Font f(L"Microsoft YaHei UI", 12.f, gd::FontStyleRegular, gd::UnitPixel);
gd::RectF lay(0, 0, (gd::REAL)(kPreviewW - 24), 10000.f);
gd::RectF bound;
gd::StringFormat sf;
sf.SetTrimming(gd::StringTrimmingEllipsisWord);
g.MeasureString(ptext_.c_str(), (INT)ptext_.size(), &f, lay, &sf, &bound);
ReleaseDC(nullptr, screen);
float th = bound.Height > 0 ? bound.Height : 60.f;
pw_ = kPreviewW;
ph_ = (int)std::min(th + 24.f, (float)kPreviewH);
if (ph_ < 60) ph_ = 60;
}

    // 定位：行旁（先右侧，放不下换左侧），纵向对齐行并夹进工作?
RECT pr{};
GetWindowRect(hwnd_, &pr);
HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
MONITORINFO mi{sizeof(mi)};
if (!GetMonitorInfoW(mon, &mi)) return;
int rowY = pr.top + (int)((kHeaderH + (row - scroll_) * kRowH) * scale_);
int pwPhys = (int)(pw_ * scale_), phPhys = (int)(ph_ * scale_);
int x = pr.right + (int)(8 * scale_);
if (x + pwPhys > mi.rcWork.right - 8) x = pr.left - pwPhys - (int)(8 * scale_);
x = std::max((int)mi.rcWork.left + 8, std::min(x, (int)mi.rcWork.right - pwPhys - 8));
int y = rowY - (int)(8 * scale_);
y = std::max((int)mi.rcWork.top + 8, std::min(y, (int)mi.rcWork.bottom - phPhys - 8));
SetWindowPos(pwnd_, HWND_TOPMOST, x, y, pwPhys, phPhys, SWP_NOACTIVATE);
prow_ = row;
DrawPreview();
ShowWindow(pwnd_, SW_SHOWNA);
}

void ClipPanel::DrawPreview() {
if (!pwnd_ || pmode_ == 0) return;
int w = pw_, h = ph_;
PresentLayered(pwnd_, (int)(w * scale_), (int)(h * scale_), scale_,
[&](gd::Graphics& g) {
g.Clear(gd::Color(0, 0, 0, 0));
                       // 圆角卡面
gd::GraphicsPath path;
const float r = 12.f;
path.AddArc(0.5f, 0.5f, r, r, 180, 90);
path.AddArc(w - r - 0.5f, 0.5f, r, r, 270, 90);
path.AddArc(w - r - 0.5f, h - r - 0.5f, r, r, 0, 90);
path.AddArc(0.5f, h - r - 0.5f, r, r, 90, 90);
path.CloseFigure();
gd::SolidBrush bg(kPreviewBg);
g.FillPath(&bg, &path);
gd::Pen stroke(gd::Color(90, 34, 211, 238), 1.f);
g.DrawPath(&stroke, &path);
if (pmode_ == 1 && pimg_) {
g.DrawImage(static_cast<gd::Bitmap*>(pimg_), 12.f, 12.f,
(gd::REAL)pdw_, (gd::REAL)pdh_);
} else if (pmode_ == 3 && !pimgs_.empty()) {
                           // 多图网格：每格等�?fit + 底部文件名小�?
gd::Font nf(L"Microsoft YaHei UI", 9.f, gd::FontStyleRegular,
gd::UnitPixel);
gd::SolidBrush nc(gd::Color(200, 160, 170, 180));
gd::SolidBrush phb(gd::Color(120, 40, 46, 60));
int rows = (int)((pimgs_.size() + pcols_ - 1) / pcols_);
int cellW = (kPreviewW - 24 - (pcols_ - 1) * 8) / pcols_;
int cellH = cellW * 3 / 4;
gd::StringFormat cf;
cf.SetTrimming(gd::StringTrimmingEllipsisCharacter);
gd::StringFormat icf;
icf.SetAlignment(gd::StringAlignmentCenter);
icf.SetLineAlignment(gd::StringAlignmentCenter);
for (size_t i = 0; i < pimgs_.size(); ++i) {
int cx = 12 + (int)i % pcols_ * (cellW + 8);
int cy = 12 + (int)i / pcols_ * (cellH + 26);
auto* b = static_cast<gd::Bitmap*>(pimgs_[i]);
UINT iw = b->GetWidth(), ih = b->GetHeight();
if (iw && ih) {
gd::REAL s = std::min((gd::REAL)cellW / iw,
(gd::REAL)cellH / ih);
gd::REAL dw2 = iw * s, dh2 = ih * s;
g.DrawImage(b, (gd::REAL)cx + (cellW - dw2) / 2,
(gd::REAL)cy + (cellH - dh2) / 2, dw2, dh2);
} else {
g.FillRectangle(&phb, cx, cy, cellW, cellH);
}
g.DrawString(pnames_[i].c_str(), (INT)pnames_[i].size(),
&nf,
gd::RectF((gd::REAL)cx, (gd::REAL)(cy + cellH),
(gd::REAL)cellW, 16.f),
&cf, &nc);
}
if (prow_ >= 0 && prow_ < (int)rows_.size()) {
const ClipEntry& pe = items_[rows_[prow_]];
int imgTotal = 0;
if (pe.type == "image") {
for (auto& q : pe.payloads)
if (q.fmt == "png") ++imgTotal;
} else {
for (auto& p : DropPaths(pe))
if (IsImageFile(p)) ++imgTotal;
}
if (imgTotal > (int)pimgs_.size()) {
std::wstring more = L"…" +
std::to_wstring(imgTotal - (int)pimgs_.size()) +
L" 张未显示";
gd::SolidBrush mc(gd::Color(255, 150, 160, 170));
int lastRow = (int)((pimgs_.size() - 1) / pcols_) + 1;
g.DrawString(more.c_str(), (INT)more.size(), &nf,
gd::RectF(12.f,
(gd::REAL)(12 + lastRow * (cellH + 26)),
(gd::REAL)pdw_, 16.f),
&icf, &mc);
}
}
} else if (pmode_ == 2) {
gd::Font f(L"Microsoft YaHei UI", 12.f, gd::FontStyleRegular,
gd::UnitPixel);
gd::SolidBrush tc(gd::Color(235, 238, 245));
gd::StringFormat sf;
sf.SetTrimming(gd::StringTrimmingEllipsisWord);
g.DrawString(ptext_.c_str(), (INT)ptext_.size(), &f,
gd::RectF(12.f, 10.f, (gd::REAL)(w - 24),
(gd::REAL)(h - 20)),
&sf, &tc);
}
});
}

// ---------------------------------------------------------- 滚动条淡�?

void ClipPanel::ScrollPulse() {
scrollAlpha_ = 1.f;
scrollIdleMs_ = 0;
if (!scrollFading_) {
SetTimer(hwnd_, 3, 40, nullptr);  // 40ms 步进：先保持后淡�?        scrollFading_ = true;
}
}

// ---------------------------------------------------------- 复制提示（Toast�?

void ClipPanel::EnsureToast() {
if (twnd_) return;
EnsureGdiplus();
WNDCLASSW wc{};
wc.lpfnWndProc = &ToastWndProc;
wc.hInstance = GetModuleHandleW(nullptr);
wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
wc.lpszClassName = L"LumenBoxClipToast";
RegisterClassW(&wc);
twnd_ = CreateWindowExW(
WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
WS_EX_TRANSPARENT,  // 鼠标穿透，不打断操�?
wc.lpszClassName, L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr,
wc.hInstance, this);
}

LRESULT CALLBACK ClipPanel::ToastWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
auto* self = reinterpret_cast<ClipPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
if (msg == WM_NCCREATE) {
auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
self = static_cast<ClipPanel*>(cs->lpCreateParams);
SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
return TRUE;
}
switch (msg) {
case WM_PAINT: {
PAINTSTRUCT ps;
BeginPaint(hwnd, &ps);
EndPaint(hwnd, &ps);
return 0;
}
case WM_ERASEBKGND:
return 1;
default:
break;
}
return DefWindowProcW(hwnd, msg, wp, lp);
}

void ClipPanel::ShowToast(const std::wstring& text) {
EnsureToast();
if (!twnd_) return;
toastText_ = text;
toastW_ = 148, toastH_ = 44;  // �?+ 已复�?    // 定位：光标右上，越界上折，夹进工作区
POINT pt;
GetCursorPos(&pt);
HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
MONITORINFO mi{sizeof(mi)};
if (GetMonitorInfoW(mon, &mi)) {
int x = pt.x + 18, y = pt.y - toastH_ - 14;
if (y < (int)mi.rcWork.top + 8) y = pt.y + 22;
x = std::max((int)mi.rcWork.left + 8,
std::min(x, (int)mi.rcWork.right - toastW_ - 8));
y = std::max((int)mi.rcWork.top + 8,
std::min(y, (int)mi.rcWork.bottom - toastH_ - 8));
SetWindowPos(twnd_, HWND_TOPMOST, x, y, (int)(toastW_ * scale_),
(int)(toastH_ * scale_), SWP_NOACTIVATE);
}
DrawToast();
ShowWindow(twnd_, SW_SHOWNA);
SetTimer(hwnd_, 2, 1100, nullptr);  // 1.1s 后自动消?
}

void ClipPanel::HideToast() {
if (twnd_ && IsWindowVisible(twnd_)) ShowWindow(twnd_, SW_HIDE);
KillTimer(hwnd_, 2);
}

void ClipPanel::DrawToast() {
if (!twnd_) return;
int w = toastW_, h = toastH_;
PresentLayered(twnd_, (int)(w * scale_), (int)(h * scale_), scale_,
[&](gd::Graphics& g) {
g.Clear(gd::Color(0, 0, 0, 0));
gd::GraphicsPath path;
const float r = 10.f;
path.AddArc(0.5f, 0.5f, r, r, 180, 90);
path.AddArc(w - r - 0.5f, 0.5f, r, r, 270, 90);
path.AddArc(w - r - 0.5f, h - r - 0.5f, r, r, 0, 90);
path.AddArc(0.5f, h - r - 0.5f, r, r, 90, 90);
path.CloseFigure();
gd::SolidBrush bg(kToastBg);
g.FillPath(&bg, &path);
gd::Pen stroke(gd::Color(110, 34, 211, 238), 1.f);
g.DrawPath(&stroke, &path);
gd::Font fChk(L"Segoe UI Symbol", 13.f, gd::FontStyleBold,
gd::UnitPixel);
gd::SolidBrush chk(gd::Color(255, 52, 211, 153));
gd::RectF rcChk(12.f, 8.f, 22.f, (gd::REAL)(h - 16.f));
gd::StringFormat cf;
cf.SetAlignment(gd::StringAlignmentCenter);
cf.SetLineAlignment(gd::StringAlignmentCenter);
g.DrawString(L"\x2713", -1, &fChk, rcChk, &cf, &chk);
gd::Font f(L"Microsoft YaHei UI", 13.f, gd::FontStyleRegular,
gd::UnitPixel);
gd::SolidBrush tc(gd::Color(240, 244, 250));
g.DrawString(toastText_.c_str(), (INT)toastText_.size(), &f,
gd::RectF(38.f, 8.f, (gd::REAL)(w - 46),
(gd::REAL)(h - 16.f)),
&cf, &tc);
});
}

// ---------------------------------------------------------- 事件

LRESULT CALLBACK ClipPanel::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
auto* self = reinterpret_cast<ClipPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
if (msg == WM_NCCREATE) {
auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
self = static_cast<ClipPanel*>(cs->lpCreateParams);
SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
return TRUE;
}
if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

auto moveSel = [&](int d) {
int nn = (int)self->rows_.size();
if (!nn) return;
self->sel_ = std::min(std::max(self->sel_ + d, 0), nn - 1);
int vis = self->VisibleCount();
if (self->sel_ < self->scroll_) self->scroll_ = self->sel_;
if (self->sel_ >= self->scroll_ + vis) self->scroll_ = self->sel_ - vis + 1;
self->ScrollPulse();  // 内容滚动：显示滚动条（停止后淡出?
self->HidePreview();  // 键盘换行：收起旧行预
self->hoverRow_ = -1;
KillTimer(hwnd, 1);
self->Draw();
};

switch (msg) {
case WM_PAINT: {  // 分层窗不处理 WM_PAINT 绘制，仅清除更新区域
        PAINTSTRUCT ps;
BeginPaint(hwnd, &ps);
EndPaint(hwnd, &ps);
return 0;
}
case WM_ERASEBKGND:
return 1;
case WM_KEYDOWN:
if (wp == VK_ESCAPE) {
self->clearArm_ = false;
self->OnClose();
} else if (wp == VK_RETURN) {
self->CopyRow(self->sel_);
} else if (wp == VK_UP) {
moveSel(-1);
} else if (wp == VK_DOWN) {
moveSel(1);
} else if (wp == VK_PRIOR) {
moveSel(-self->VisibleCount());  // 翻页同步移动选中项（原 scroll 先跳再被 moveSel(0) 重置回去 → 翻页无效）
} else if (wp == VK_NEXT) {
moveSel(self->VisibleCount());
} else if (wp == VK_DELETE) {
if (self->sel_ >= 0 && self->sel_ < (int)self->rows_.size() && self->store_) {
self->store_->Remove(self->items_[self->rows_[self->sel_]].id);
self->Refresh();
}
} else if (wp == VK_TAB) {
self->filter_ = (self->filter_ + 1) % 4;
self->sel_ = 0;
self->scroll_ = 0;
self->Refresh();
}
return 0;
case WM_MOUSEMOVE: {
float s = self->scale_ ? self->scale_ : 1.f;
int x = (int)(GET_X_LPARAM(lp) / s), y = (int)(GET_Y_LPARAM(lp) / s);
if (!self->trackMouse_) {  // 登记离开跟踪（一次性，离开后需重登记）
TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
TrackMouseEvent(&tme);
self->trackMouse_ = true;
}
int row = self->HitRow(x, y);
// 悬停只做视觉反馈，不改变选中项（原实现 sel_ = row 会让鼠标移动即抢走
// 键盘选中，方向键导航随即失效——两者互相打架）
int hrow = (row >= 0 && row < 3000) ? row : -1;
bool closeHot = (self->HitHeader(x, y) == 202);
if (hrow != self->hoverRow_ || closeHot != self->closeHot_) {
    self->hoverRow_ = hrow;
    self->closeHot_ = closeHot;
    self->Draw();
}
self->UpdateHover(hrow);
return 0;
}
case WM_MOUSELEAVE:
self->trackMouse_ = false;
self->hoverRow_ = -1;
self->closeHot_ = false;
KillTimer(hwnd, 1);
self->HidePreview();
self->Draw();
return 0;
case WM_TIMER:
if (wp == 1) {  // 悬停计时到：弹出该行放大预览
KillTimer(hwnd, 1);
if (self->hoverRow_ >= 0 && self->hoverRow_ < (int)self->rows_.size())
self->ShowPreview(self->hoverRow_);
} else if (wp == 2) {  // 复制提示到期
self->HideToast();
} else if (wp == 3) {  // 滚动条淡出动?
self->scrollIdleMs_ += 40;
if (self->scrollIdleMs_ < 800) return 0;  // 滚动后保持片?
self->scrollAlpha_ -= 0.12f;
if (self->scrollAlpha_ <= 0.f) {
self->scrollAlpha_ = 0.f;
self->scrollFading_ = false;
KillTimer(hwnd, 3);
}
self->Draw();
}
return 0;
case WM_LBUTTONDOWN: {
float s = self->scale_ ? self->scale_ : 1.f;
int x = (int)(GET_X_LPARAM(lp) / s), y = (int)(GET_Y_LPARAM(lp) / s);
int h_ = self->HitHeader(x, y);
if (h_ >= 100 && h_ < 104) {
self->filter_ = h_ - 100;
self->sel_ = 0;
self->scroll_ = 0;
self->Refresh();
return 0;
}
if (h_ == 200) {  // 暂停/恢复
if (self->pausedSet_) self->pausedSet_(!(self->pausedGet_ ? self->pausedGet_() : false));
self->Draw();
return 0;
}
if (h_ == 201) {  // 清空：二次确�?
if (self->clearArm_) {
if (self->store_) self->store_->Clear();
self->clearArm_ = false;
self->sel_ = 0;
self->scroll_ = 0;
self->Refresh();
} else {
self->clearArm_ = true;
self->Draw();
}
return 0;
}
if (h_ == 202) {
self->OnClose();
return 0;
}
int hit = self->HitRow(x, y);
if (hit >= 4000 && hit < 5000) {  // 置顶切换
int row = hit - 4000;
if (self->store_ && row < (int)self->rows_.size()) {
const ClipEntry& e = self->items_[self->rows_[row]];
self->store_->SetPinned(e.id, !e.pinned);
self->Refresh();
}
return 0;
}
if (hit >= 5000) {  // 删除
int row = hit - 5000;
if (self->store_ && row < (int)self->rows_.size()) {
self->store_->Remove(self->items_[self->rows_[row]].id);
self->Refresh();
}
return 0;
}
if (hit >= 0) {  // 点击行本体：直接复制并关闭面?
self->sel_ = hit;
self->CopyRow(hit);
}
return 0;
}
case WM_LBUTTONDBLCLK: {
float s = self->scale_ ? self->scale_ : 1.f;
int x = (int)(GET_X_LPARAM(lp) / s), y = (int)(GET_Y_LPARAM(lp) / s);
int hit = self->HitRow(x, y);
if (hit >= 0 && hit < 3000) self->CopyRow(hit);
return 0;
}
case WM_MOUSEWHEEL: {
int delta = GET_WHEEL_DELTA_WPARAM(wp);
int steps = delta / WHEEL_DELTA;
self->scroll_ -= steps * 3;
int maxScroll = std::max(0, (int)self->rows_.size() - self->VisibleCount());
self->scroll_ = std::min(std::max(self->scroll_, 0), maxScroll);
self->ScrollPulse();  // 滚轮：显示滚动条（停止后淡出?
self->HidePreview();  // 行随滚动位移：收起预
self->hoverRow_ = -1;
KillTimer(hwnd, 1);
moveSel(0);
return 0;
}
case WM_ACTIVATE:
if (LOWORD(wp) == WA_INACTIVE) self->OnClose();  // 失焦自动关闭（预览/提示窗均 NOACTIVATE，不会误触发）
return 0;
case WM_CLOSE:
self->OnClose();
return 0;
default:
break;
}
return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace lb
