#include "core/clipboard_monitor.hpp"
#include "core/stage_log.hpp"
#include "core/strutil.hpp"
#include <objbase.h>
#include <objidl.h>
#include <shellapi.h>
#include <shlobj_core.h>  // DROPFILES（新 SDK 的 shellapi.h 不含此结构）
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <ctime>

#pragma comment(lib, "windowscodecs")

namespace lb {

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------- 自写抑制标志

namespace {
// 置位期间 WM_CLIPBOARDUPDATE 一律忽略（CopyClipToClipboard 的回写窗口）。
// WM_CLIPBOARDUPDATE 与回写发生在同一线程，跨线程可见性由 atomic 保证。
// 另设短时间窗：WM_CLIPBOARDUPDATE 是 POSTED 消息，回写返回后才被处理——
// 布尔标志届时已清除，故以时间窗兜底（如多图 HDROP 回写的重捕获）。
std::atomic<int> g_clipSelfWrite{0};
std::atomic<ULONGLONG> g_clipSelfWriteUntilMs{0};

struct SelfWriteGuard {
    SelfWriteGuard() { g_clipSelfWrite.fetch_add(1, std::memory_order_acq_rel); }
    ~SelfWriteGuard() {
        g_clipSelfWrite.fetch_sub(1, std::memory_order_acq_rel);
        g_clipSelfWriteUntilMs.store(GetTickCount64() + 700, std::memory_order_release);
    }
};

// WIC 工厂（每次转换创建，转换频率 = 复制图片频率，可忽略）
bool WicFactory(IWICImagingFactory** f) {
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(f)));
}

// IWICBitmapSource → PNG 字节（内存流）
bool EncodeWicToPng(IWICBitmapSource* src, UINT w, UINT h, std::string& out) {
    ComPtr<IWICImagingFactory> fac;
    if (!WicFactory(&fac)) return false;
    ComPtr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return false;
    ComPtr<IWICBitmapEncoder> enc;
    if (FAILED(fac->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc))) return false;
    if (FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(enc->CreateNewFrame(&frame, nullptr))) return false;
    if (FAILED(frame->Initialize(nullptr))) return false;
    if (FAILED(frame->SetSize(w, h))) return false;
    if (FAILED(frame->WriteSource(src, nullptr))) return false;
    if (FAILED(frame->Commit())) return false;
    if (FAILED(enc->Commit())) return false;
    HGLOBAL hg = nullptr;
    if (FAILED(GetHGlobalFromStream(stream.Get(), &hg))) return false;
    SIZE_T n = GlobalSize(hg);
    auto* p = static_cast<unsigned char*>(GlobalLock(hg));
    if (!p) return false;
    out.assign(reinterpret_cast<const char*>(p), n);
    GlobalUnlock(hg);
    // PNG 帧可能带尾部零填充，用 IHDR 实际尺寸裁不了——PNG 解码器忽略即可，不裁
    return !out.empty();
}

// 裸 DIB 字节（BITMAPINFO+像素，剪贴板 CF_DIB 原样）→ PNG。v5=BITMAPV5HEADER。
bool DibToPng(const std::string& dib, bool v5, std::string& png, int& wOut, int& hOut) {
    if (dib.size() < sizeof(BITMAPINFOHEADER)) return false;
    auto* bi = reinterpret_cast<const BITMAPINFOHEADER*>(dib.data());
    int w = bi->biWidth, h = bi->biHeight < 0 ? -bi->biHeight : bi->biHeight;
    UINT bits = static_cast<UINT>(bi->biBitCount);
    wOut = w;
    hOut = h;
    if (w <= 0 || h <= 0 || bits < 8 || bits > 32) return false;

    // 像素偏移：头(含掩码语义) + 调色板。V5 头掩码在头内；40 字节头 + BITFIELDS
    // 时紧跟 3 个掩码 DWORD（BI_ALPHABITFIELDS 极罕见，转换失败自然降级）。
    size_t off = static_cast<size_t>(bi->biSize);
    if (bi->biCompression == BI_BITFIELDS && bi->biClrUsed == 0 &&
        bi->biSize <= sizeof(BITMAPINFOHEADER))
        off += 3 * sizeof(DWORD);
    UINT colors = bi->biClrUsed ? bi->biClrUsed : (bits <= 8 ? (1u << bits) : 0u);
    off += static_cast<size_t>(colors) * 4;
    size_t row = (static_cast<size_t>(w) * bits + 31) / 32 * 4;
    size_t imgSize = row * static_cast<size_t>(h);
    if (dib.size() < off + imgSize) return false;  // 数据不完整，放弃转码

    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    void* bitsOut = nullptr;
    std::vector<char> bmi(dib.begin(), dib.begin() + static_cast<long>(off));
    HBITMAP bmp =
        CreateDIBSection(dc, reinterpret_cast<const BITMAPINFO*>(bmi.data()),
                         DIB_RGB_COLORS, &bitsOut, nullptr, 0);
    bool ok = false;
    if (bmp && bitsOut) {
        memcpy(bitsOut, dib.data() + off, imgSize);  // 行序同向（自底向上对自底向上）
        ComPtr<IWICImagingFactory> fac;
        ComPtr<IWICBitmap> wic;
        if (WicFactory(&fac) &&
            SUCCEEDED(fac->CreateBitmapFromHBITMAP(
                bmp, nullptr,
                (v5 && bits == 32) ? WICBitmapUseAlpha : WICBitmapIgnoreAlpha, &wic))) {
            ok = EncodeWicToPng(wic.Get(), static_cast<UINT>(w), static_cast<UINT>(h), png);
        }
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(dc);
    return ok;
}

// PNG → 缩略图 PNG（最长边 128px，WIC Scaler）
bool ScalePngThumb(const std::string& png, std::string& thumb) {
    ComPtr<IWICImagingFactory> fac;
    if (!WicFactory(&fac)) return false;
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, png.size());
    if (!hg) return false;
    {
        auto* p = GlobalLock(hg);
        if (p) { memcpy(p, png.data(), png.size()); GlobalUnlock(hg); }  // 锁失败跳过，下游解码优雅失败
    }
    ComPtr<IStream> stream;
    bool ok = false;
    if (SUCCEEDED(CreateStreamOnHGlobal(hg, FALSE, &stream))) {  // FALSE：流不接管 hg，末尾统一 GlobalFree
        ComPtr<IWICBitmapDecoder> dec;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> conv;
        UINT w = 0, h = 0;
        if (SUCCEEDED(fac->CreateDecoderFromStream(stream.Get(), nullptr,
                                                   WICDecodeMetadataCacheOnDemand, &dec)) &&
            SUCCEEDED(dec->GetFrame(0, &frame)) &&
            SUCCEEDED(fac->CreateFormatConverter(&conv)) &&
            SUCCEEDED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                                       WICBitmapDitherTypeNone, nullptr, 0.f,
                                       WICBitmapPaletteTypeCustom)) &&
            SUCCEEDED(conv->GetSize(&w, &h)) && w && h) {
            UINT tw = w, th = h;
            UINT longest = w > h ? w : h;
            if (longest > 128) {  // 等比缩到最长边 128
                tw = w * 128 / longest;
                th = h * 128 / longest;
                if (!tw) tw = 1;
                if (!th) th = 1;
                ComPtr<IWICBitmapScaler> scaler;
                if (SUCCEEDED(fac->CreateBitmapScaler(&scaler)) &&
                    SUCCEEDED(scaler->Initialize(conv.Get(), tw, th,
                                                 WICBitmapInterpolationModeFant)))
                    ok = EncodeWicToPng(scaler.Get(), tw, th, thumb);
            } else {
                ok = EncodeWicToPng(conv.Get(), w, h, thumb);
            }
        }
    }
    GlobalFree(hg);
    return ok;
}

// PNG 头解析尺寸（IHDR：16..20 宽 / 20..24 高，大端）
bool PngSize(const std::string& png, int& w, int& h) {
    if (png.size() < 24 || memcmp(png.data(), "\x89PNG", 4) != 0) return false;
    auto rd = [&](size_t o) {
        return (static_cast<int>(static_cast<unsigned char>(png[o])) << 24) |
               (static_cast<int>(static_cast<unsigned char>(png[o + 1])) << 16) |
               (static_cast<int>(static_cast<unsigned char>(png[o + 2])) << 8) |
               static_cast<int>(static_cast<unsigned char>(png[o + 3]));
    };
    w = rd(16);
    h = rd(20);
    return w > 0 && h > 0;
}

// HTML Format → 纯文本预览（取 StartFragment 区，剥标签 + 基本实体）
std::wstring HtmlPreviewText(const std::string& html) {
    std::string frag = html;
    auto s = frag.find("<!--StartFragment-->");
    if (s != std::string::npos) frag = frag.substr(s + 20);
    auto e = frag.find("<!--EndFragment-->");
    if (e != std::string::npos) frag = frag.substr(0, e);
    std::string txt;
    bool inTag = false;
    for (size_t i = 0; i < frag.size(); ++i) {
        char c = frag[i];
        if (c == '<') { inTag = true; continue; }
        if (c == '>') { inTag = false; txt += ' '; continue; }
        if (inTag) continue;
        if (c == '&') {
            if (frag.compare(i, 5, "&amp;") == 0) { txt += '&'; i += 4; }
            else if (frag.compare(i, 4, "&lt;") == 0) { txt += '<'; i += 3; }
            else if (frag.compare(i, 4, "&gt;") == 0) { txt += '>'; i += 3; }
            else if (frag.compare(i, 6, "&quot;") == 0) { txt += '"'; i += 5; }
            else if (frag.compare(i, 6, "&nbsp;") == 0) { txt += ' '; i += 5; }
            else txt += c;
        } else {
            txt += c;
        }
    }
    return MakePreview(ToWide(txt));
}

// UTF-8 按 1MB 截断（回退到完整字符边界）
std::string TruncUtf8(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t cut = maxBytes;
    while (cut > maxBytes - 3 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut);
}

// 最小 base64 解码（标准字母表；容忍空白，遇填充/非法字符即止）
static std::string Base64Decode(const char* p, size_t n) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    out.reserve(n / 4 * 3);
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; ++i) {
        char c = p[i];
        if (c == '=') break;
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        int v = val(c);
        if (v < 0) break;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(char((acc >> bits) & 0xFF));
        }
    }
    return out;
}

// 剪贴板 HDROP 字节 → 路径列表
std::vector<std::wstring> ParseDropFiles(const void* data, SIZE_T size) {
    std::vector<std::wstring> out;
    if (size < sizeof(DROPFILES)) return out;
    auto* df = static_cast<const DROPFILES*>(data);
    if (df->pFiles < sizeof(DROPFILES) || df->pFiles >= size) return out;
    const char* base = static_cast<const char*>(data);
    if (df->fWide) {
        const wchar_t* p = reinterpret_cast<const wchar_t*>(base + df->pFiles);
        const wchar_t* end = reinterpret_cast<const wchar_t*>(base + size);
        while (p < end && *p) {
            std::wstring s(p);
            if (s.empty()) break;
            out.push_back(s);
            p += s.size() + 1;
        }
    } else {
        const char* p = base + df->pFiles;
        const char* end = base + size;
        while (p < end && *p) {
            std::string s(p);
            if (s.empty()) break;
            out.push_back(ToWide(s));  // ANSI 路径按系统代码页转宽
            p += s.size() + 1;
        }
    }
    return out;
}

std::wstring ExeNameOf(HWND owner) {
    DWORD pid = 0;
    GetWindowThreadProcessId(owner, &pid);
    if (!pid) return L"";
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    wchar_t buf[MAX_PATH] = {};
    DWORD n = MAX_PATH;
    std::wstring out;
    if (QueryFullProcessImageNameW(h, 0, buf, &n)) {
        out = buf;
        size_t slash = out.find_last_of(L"\\/");
        if (slash != std::wstring::npos) out = out.substr(slash + 1);
    }
    CloseHandle(h);
    return out;
}

std::string WideBytes(const std::wstring& w) {
    return std::string(reinterpret_cast<const char*>(w.c_str()),
                       (w.size() + 1) * sizeof(wchar_t));
}
}  // namespace

// ---------------------------------------------------------- 监听

bool ClipboardMonitor::Start(ClipStore* store, HWND notify, UINT uiMsg,
                             const ClipboardOpts& opts) {
    store_ = store;
    notify_ = notify;
    uiMsg_ = uiMsg;
    opts_ = opts;
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LumenBoxClipMon";
    RegisterClassW(&wc);
    // 隐藏弹出小窗（不用 HWND_MESSAGE：保证监听消息在所有系统上可达）
    hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr,
                            nullptr, wc.hInstance, this);
    if (!hwnd_) return false;
    if (!AddClipboardFormatListener(hwnd_)) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    lastSeq_ = GetClipboardSequenceNumber();  // 忽略启动前的旧内容
    quit_ = false;
    th_ = std::thread(&ClipboardMonitor::WorkerMain, this);
    return true;
}

void ClipboardMonitor::Stop() {
    if (th_.joinable()) {
        quit_ = true;
        qcv_.notify_all();
        th_.join();
    }
    if (hwnd_) {
        RemoveClipboardFormatListener(hwnd_);
        DestroyWindow(hwnd_);  // 须由建窗线程（主线程）调用——main.cpp 在主线程 Stop
        hwnd_ = nullptr;
    }
}

LRESULT CALLBACK ClipboardMonitor::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<ClipboardMonitor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<ClipboardMonitor*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CLIPBOARDUPDATE:
        self->OnClipboardUpdate();
        return 0;
    case WM_DESTROY:
        RemoveClipboardFormatListener(hwnd);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ClipboardMonitor::OnClipboardUpdate() {
    if (!store_ || paused_.load(std::memory_order_relaxed)) return;
    if (g_clipSelfWrite.load(std::memory_order_acquire) > 0 ||
        GetTickCount64() < g_clipSelfWriteUntilMs.load(std::memory_order_acquire))
        return;  // 自己的回写（含 POSTED 延迟窗口）
    DWORD seq = GetClipboardSequenceNumber();
    if (seq == 0 || seq == lastSeq_) return;

    // 打开争用：重试 ≤5 × 10ms（设计文档 §5.1）
    // 不在 UI 线程 Sleep 阻塞（会卡界面与他进程复制）；最多 3 次即时重试，失败即跳过待下次变更
    bool opened = false;
    for (int i = 0; i < 3 && !opened; ++i) opened = OpenClipboard(hwnd_) != FALSE;
    if (!opened) return;  // 下次内容变化会再来
    // 异常安全守卫：开剪贴板后若抛异常（如 32MB DIB bad_alloc）保证关闭，
    // 否则剪贴板被本进程长期占用 → 全系统复制粘贴锁死。正常路径仍提前显式关闭以缩短持锁。
    struct ClipGuard { ~ClipGuard() { CloseClipboard(); } } clipGuard;

    // 临时诊断：枚举剪贴板全部格式名（定位微信混合复制的图片来源）
    {
        UINT f = 0;
        std::string names;
        while ((f = EnumClipboardFormats(f)) != 0) {
            wchar_t nm[128] = {};
            GetClipboardFormatNameW(f, nm, 128);
            if (f == CF_UNICODETEXT)
                names += "[13]CF_UNICODETEXT ";
            else if (f == CF_BITMAP)
                names += "[2]CF_BITMAP ";
            else if (f == CF_DIB)
                names += "[8]CF_DIB ";
            else if (f == CF_DIBV5)
                names += "[17]CF_DIBV5 ";
            else if (f == CF_ENHMETAFILE)
                names += "[14]CF_ENHMETAFILE ";
            else if (f == CF_METAFILEPICT)
                names += "[3]CF_METAFILEPICT ";
            else if (f == CF_HDROP)
                names += "[15]CF_HDROP ";
            else
                names += "[" + std::to_string(f) + "]" + ToUtf8(nm) + " ";
        }
        StageTrace(("fmts " + names).c_str());
    }

    Job job;
    auto fail = [&] {
        CloseClipboard();
    };

    // 密码管理器豁免：存在 Clipboard Viewer Ignore → 整条跳过
    UINT fmtIgnore = RegisterClipboardFormatW(L"Clipboard Viewer Ignore");
    if (fmtIgnore && GetClipboardData(fmtIgnore)) return fail();

    UINT fmtHtml = RegisterClipboardFormatW(L"HTML Format");
    UINT fmtPng = RegisterClipboardFormatW(L"PNG");

    std::vector<std::string> avail;
    bool hasText = IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
    bool hasHtml = fmtHtml && IsClipboardFormatAvailable(fmtHtml);
    bool hasPng = fmtPng && IsClipboardFormatAvailable(fmtPng);
    bool hasDibV5 = IsClipboardFormatAvailable(CF_DIBV5) != FALSE;
    bool hasDib = IsClipboardFormatAvailable(CF_DIB) != FALSE;
    bool hasDrop = IsClipboardFormatAvailable(CF_HDROP) != FALSE;
    if (hasDrop) avail.push_back("hdrop");
    if (hasPng || hasDibV5 || hasDib) avail.push_back("png");
    if (hasHtml) avail.push_back("html");
    if (hasText) avail.push_back("text");
    if (avail.empty()) return fail();  // 全是不认识的格式（如仅 CF_BITMAP）

    std::string type = ClipTypeFromFormats(avail);

    // ---- 逐格式采集（GlobalLock + memcpy，不做任何转换）----
    auto readData = [&](UINT fmt, std::string& out) -> bool {
        HANDLE h = GetClipboardData(fmt);
        if (!h) return false;
        SIZE_T n = GlobalSize(h);
        auto* p = static_cast<const unsigned char*>(GlobalLock(h));
        if (!p || !n) {
            if (p) GlobalUnlock(h);
            return false;
        }
        out.assign(reinterpret_cast<const char*>(p), n);
        GlobalUnlock(h);
        return true;
    };

    std::string primaryBytes;  // 哈希用：按展示优先级取第一个可用载荷
    if (hasDrop) {
        // OLE 剪贴板的 HDROP 可能是延迟渲染：取不到时短暂重试，给源进程渲染时间
        // GetClipboardData 已同步触发源进程延迟渲染，无需 Sleep 重试（避免阻塞 UI 线程）
        HANDLE h = GetClipboardData(CF_HDROP);
        SIZE_T n = h ? GlobalSize(h) : 0;
        auto* p = h ? static_cast<const void*>(GlobalLock(h)) : nullptr;
        if (p) {
            auto paths = ParseDropFiles(p, n);
            GlobalUnlock(h);
            if (!paths.empty()) {
                std::string joined;
                for (auto& s : paths) {
                    joined += ToUtf8(s);
                    joined += '\n';
                }
                ClipPayload pl;
                pl.fmt = "hdrop";
                pl.size = joined.size();
                pl.text = joined;  // ≤4KB 内联；大内容后台线程落盘
                job.entry.payloads.push_back(std::move(pl));
                primaryBytes = joined;
                // 预览：N 个文件：a、b、c…
                std::wstring pv = std::to_wstring(paths.size()) + L" 个文件：";
                for (size_t i = 0; i < paths.size() && i < 3; ++i) {
                    auto& f = paths[i];
                    pv += f.substr(f.find_last_of(L"\\/") + 1);
                    pv += (i + 1 < paths.size() && i < 2) ? L"、" : L"";
                }
                if (paths.size() > 3) pv += L"…";
                job.entry.text = pv;
            }
        }
    }

    int imgW = 0, imgH = 0;
    bool imgTooBig = false;
    std::string pngRaw;   // 原始 PNG（有的话直接用）
    std::string dibRaw;   // 需要 DIB→PNG 转码
    bool dibIsV5 = false;
    if (hasPng) {
        std::string raw;
        if (readData(fmtPng, raw) && PngSize(raw, imgW, imgH)) pngRaw = raw;
    }
    if (pngRaw.empty() && (hasDibV5 || hasDib)) {
        std::string raw;
        if (hasDibV5) {
            if (readData(CF_DIBV5, raw)) dibIsV5 = true;
        }
        if (dibRaw.empty() && !raw.empty()) dibRaw = raw;
        if (dibRaw.empty() && hasDib) {
            if (readData(CF_DIB, raw)) dibIsV5 = false;
            if (!raw.empty()) dibRaw = raw;
        }
        if (!dibRaw.empty() && dibRaw.size() >= sizeof(BITMAPINFOHEADER)) {
            auto* bi = reinterpret_cast<const BITMAPINFOHEADER*>(dibRaw.data());
            imgW = bi->biWidth;
            imgH = bi->biHeight < 0 ? -bi->biHeight : bi->biHeight;
        }
    }
    if (!pngRaw.empty() || !dibRaw.empty()) {
        // D9 护栏：最长边 / 解码字节数超限 → 拒收图片载荷
        if (imgW > opts_.imgMaxPixels || imgH > opts_.imgMaxPixels ||
            static_cast<uint64_t>(imgW) * imgH * 4 > opts_.imgMaxBytes) {
            imgTooBig = true;
        } else {
            ClipPayload pl;
            pl.fmt = "png";
            pl.text = pngRaw;  // 裸 PNG 暂存；dib 路径后台转码后再填
            pl.size = pngRaw.size();
            job.entry.payloads.push_back(std::move(pl));
            if (primaryBytes.empty()) primaryBytes = pngRaw.empty() ? dibRaw : pngRaw;
        }
    }

    if (hasHtml) {
        std::string raw;
        if (readData(fmtHtml, raw) && !raw.empty()) {
            ClipPayload pl;
            pl.fmt = "html";
            pl.size = raw.size();
            pl.text = raw;
            job.entry.payloads.push_back(std::move(pl));
            if (primaryBytes.empty()) primaryBytes = raw;
            if (job.entry.text.empty()) job.entry.text = HtmlPreviewText(raw);
        }
    }

    if (hasText) {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            auto* p = static_cast<const wchar_t*>(GlobalLock(h));
            if (p) {
                SIZE_T n = GlobalSize(h) / sizeof(wchar_t);
                size_t len = 0;
                while (len < n && p[len]) ++len;  // 以 \0 为界（GlobalSize 是分配粒度）
                std::wstring wide(p, len);
                GlobalUnlock(h);
                std::string utf8 = ToUtf8(wide);
                utf8 = TruncUtf8(utf8, static_cast<size_t>(opts_.textMaxBytes));
                ClipPayload pl;
                pl.fmt = "text";
                pl.size = utf8.size();
                pl.text = utf8;
                job.entry.payloads.push_back(std::move(pl));
                if (utf8.size() <= 4096) {
                    // 图片/文件条目的附带文本是工具噪声（截图临时路径、文件列表的
                    // 文本呈现=裸路径多行）——不覆盖已生成的专属预览
                    if (type != "image" && type != "files") {
                        job.entry.text = ToWide(utf8);  // 小文本内联全文
                        job.entry.textInline = true;
                    }
                } else if (job.entry.text.empty() && type != "image" && type != "files") {
                    job.entry.text = MakePreview(ToWide(utf8));
                }
                if (primaryBytes.empty()) primaryBytes = utf8;
            }
        }
    }

    // 关闭剪贴板前捕获微信/QQ 富文本与 json 原始字节：GetClipboardData 必须在开剪贴板时调用，
    // 关闭后再读恒为 NULL（原 bug：输入框配图还原 / json 存档整段失效）。
    // 耗时的本地文件读取/base64 解码放到关闭之后，尽量缩短持锁时间。
    std::vector<std::pair<UINT, std::string>> richEditCaptured;
    for (const wchar_t* rn : {L"WeChat_RichEdit_Format", L"QQ_Unicode_RichEdit_Format"}) {
        UINT f = RegisterClipboardFormatW(rn);
        if (f && IsClipboardFormatAvailable(f)) {
            std::string tmp;
            if (readData(f, tmp) && !tmp.empty()) richEditCaptured.emplace_back(f, std::move(tmp));
        }
    }
    std::string wxJsonRaw;
    {
        UINT f = RegisterClipboardFormatW(L"json");
        if (f && IsClipboardFormatAvailable(f)) {
            std::string tmp;
            if (readData(f, tmp) && !tmp.empty() && tmp.size() <= 64 * 1024)
                wxJsonRaw = std::move(tmp);
        }
    }

    CloseClipboard();
    lastSeq_ = seq;

    // 微信等聊天应用复制带图消息：整段聊天以 HTML 复制（图片 base64 内嵌），
    // 文本呈现只是"[图片]"占位——提取全部内嵌图（至多 6 张），条目按图片呈现
    // （缩略图/悬停网格原图/回写为图片，而非一排占位文字）
    if (type == "html") {
        std::string html;
        for (auto& pl : job.entry.payloads)
            if (pl.fmt == "html" && !pl.text.empty()) {
                html = pl.text;  // 拷贝：后续 insert 会使引用失效
                break;
            }
        if (!html.empty()) {
            int nimg = 0;
            size_t p = html.find("data:image/");
            while (p != std::string::npos && nimg < 6) {
                size_t b64 = html.find(";base64,", p);
                size_t stop = html.find_first_of("\"'>", p);
                if (b64 != std::string::npos &&
                    (stop == std::string::npos || b64 < stop)) {
                    size_t d0 = b64 + 8;
                    size_t d1 = html.find_first_of("\"'>", d0);
                    if (d1 == std::string::npos)
                        d1 = std::min(html.size(), d0 + ((size_t)1 << 24));
                    std::string bytes = Base64Decode(html.data() + d0, d1 - d0);
                    int w = 0, h = 0;
                    if (bytes.size() >= 100 && PngSize(bytes, w, h) &&
                        w <= opts_.imgMaxPixels && h <= opts_.imgMaxPixels &&
                        (uint64_t)w * h * 4 <= opts_.imgMaxBytes) {
                        ClipPayload pl;
                        pl.fmt = "png";
                        pl.size = bytes.size();
                        pl.text = std::move(bytes);
                        job.entry.payloads.insert(job.entry.payloads.begin() + nimg,
                                                  std::move(pl));  // png 依序置首
                        if (nimg == 0) {
                            imgW = w;  // 走统一赋值路径（job.entry.imgW = imgW）
                            imgH = h;
                            job.entry.text.clear();  // "[图片]"占位不作为内容
                            job.entry.textInline = false;
                            type = "image";
                        }
                        ++nimg;
                    }
                }
                p = html.find("data:image/", p + 12);
            }
            if (nimg > 0) {
                // 消息列表混合复制：文本里"[图片]"是占位符（图已在 png 载荷），
                // 显示与回写都剥掉，保留真文字
                auto stripPlaceholder = [](std::wstring t) {
                    size_t pos;
                    while ((pos = t.find(L"[图片]")) != std::wstring::npos)
                        t.erase(pos, 4);
                    size_t b = t.find_first_not_of(L" \t\r\n");
                    if (b == std::wstring::npos) return std::wstring();
                    size_t e = t.find_last_not_of(L" \t\r\n");
                    return t.substr(b, e - b + 1);
                };
                std::wstring clean = stripPlaceholder(job.entry.text);
                job.entry.text = clean;
                for (auto& pl : job.entry.payloads)
                    if (pl.fmt == "text") {
                        std::wstring t = stripPlaceholder(ToWide(pl.text));
                        pl.text = ToUtf8(t);
                        pl.size = pl.text.size();
                        if (pl.text.empty()) pl.fmt.clear();
                    }
            }
        }
    }

    // 微信/QQ 输入框图文混合复制：图片本体不在剪贴板——微信把它写到本地
    // 临时文件，富文本格式（WeChat/QQ_RichEdit_Format）以
    // <EditElement type="1" filepath="...png"> 引用，文字在 CDATA。
    // 读引用文件转 png 载荷（与聊天片段 base64 提取同管线），文字保留回写。
    if (type == "html") {
        for (auto& cap : richEditCaptured) {
            const std::string& re = cap.second;
            if (re.empty()) continue;
            int nimg = 0;
            size_t p = re.find("filepath=\"");
            while (p != std::string::npos && nimg < 6) {
                size_t q0 = p + 10;
                size_t q1 = re.find('"', q0);
                if (q1 == std::string::npos) break;
                std::wstring path = ToWide(re.substr(q0, q1 - q0));
                HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                        nullptr);
                if (hf != INVALID_HANDLE_VALUE) {
                    LARGE_INTEGER sz{};
                    if (GetFileSizeEx(hf, &sz) && sz.QuadPart > 100 &&
                        sz.QuadPart <= (LONGLONG)64 << 20) {
                        std::string bytes(static_cast<size_t>(sz.QuadPart), '\0');
                        DWORD got = 0;
                        if (ReadFile(hf, bytes.data(), static_cast<DWORD>(bytes.size()),
                                     &got, nullptr) &&
                            got == bytes.size()) {
                            int w = 0, h2 = 0;
                            if (PngSize(bytes, w, h2) &&
                                (uint64_t)w * h2 * 4 <= opts_.imgMaxBytes) {
                                ClipPayload pl;
                                pl.fmt = "png";
                                pl.size = bytes.size();
                                pl.text = std::move(bytes);
                                job.entry.payloads.insert(
                                    job.entry.payloads.begin() + nimg,
                                    std::move(pl));  // png 依序置首
                                if (nimg == 0) {
                                    imgW = w;
                                    imgH = h2;
                                    type = "image";
                                }
                                ++nimg;
                            }
                        }
                    }
                    CloseHandle(hf);
                }
                p = re.find("filepath=\"", q1);
            }
            if (nimg > 0) {
                // 文字：CDATA 拼接 → 替换"[图片]"占位（行内显示与回写都用真文字）
                std::string cdata;
                size_t c0 = re.find("<![CDATA[");
                while (c0 != std::string::npos) {
                    size_t c1 = re.find("]]>", c0 + 9);
                    if (c1 == std::string::npos) break;
                    if (!cdata.empty()) cdata += '\n';
                    cdata += re.substr(c0 + 9, c1 - c0 - 9);
                    c0 = re.find("<![CDATA[", c1 + 3);
                }
                std::wstring txt = ToWide(cdata);
                job.entry.text = txt;
                for (auto& pl : job.entry.payloads)
                    if (pl.fmt == "text") {
                        pl.text = ToUtf8(txt);
                        pl.size = pl.text.size();
                        if (pl.text.empty()) pl.fmt.clear();
                    }
                break;
            }
        }
    }

    // 微信私有 "json" 格式存档：混合复制（图片+文字消息）时微信把消息结构/
    // 资源引用放此格式（图片本体不进剪贴板；粘贴回微信由微信按引用还原）。
    // 仅存档诊断/供后续解析图片引用，不参与展示与回写。
    if (!wxJsonRaw.empty()) {
        ClipPayload pl;
        pl.fmt = "json";
        pl.size = wxJsonRaw.size();
        pl.text = std::move(wxJsonRaw);
        job.entry.payloads.push_back(std::move(pl));
    }

    job.entry.ts = std::time(nullptr);
    job.entry.type = type;
    job.entry.hash = ClipHash(primaryBytes);
    job.entry.imgW = imgW;
    job.entry.imgH = imgH;
    job.dibRaw = std::move(dibRaw);  // 关键修复：DIB 原始字节随任务交给后台线程
    job.dibIsV5 = dibIsV5;
    if (opts_.recordSource) job.entry.source = ExeNameOf(GetClipboardOwner());
    if (job.entry.text.size() > 120 && !job.entry.textInline)
        job.entry.text = job.entry.text.substr(0, 120) + L"…";

    if (imgTooBig) {
        // 图片被拒收：若还有 text/html 载荷则按普通条目收，否则发一次性提示
        bool hasOther = false;
        for (auto& pl : job.entry.payloads)
            if (pl.fmt != "png") hasOther = true;
        if (!hasOther) {
            job.reject = true;
            job.entry.payloads.clear();
            job.entry.type = "image";
        } else {
            // 图片被拒：按剩余载荷重算类型
            std::vector<std::string> rest;
            for (auto& pl : job.entry.payloads)
                if (pl.fmt != "png") rest.push_back(pl.fmt);
            job.entry.type = ClipTypeFromFormats(rest);
            job.entry.imgW = job.entry.imgH = 0;
        }
    }
    if (job.entry.payloads.empty() && !job.reject) return;  // 无有效载荷

    // 防抖：同类型图片、同尺寸、500ms 内的连续更新 = 同一次复制
    // （SetImage 等会依次设 DIBV5+DIB，触发多次 WM_CLIPBOARDUPDATE，只记第一条）
    ULONGLONG nowMs = GetTickCount64();
    if (job.entry.type == "image" && lastCapType_ == "image" &&
        job.entry.imgW == lastCapW_ && job.entry.imgH == lastCapH_ &&
        nowMs - lastCapMs_ < 500) {
        lastCapMs_ = nowMs;  // 滑动窗口，连续更新全部吸收
        return;
    }
    lastCapType_ = job.entry.type;
    lastCapW_ = job.entry.imgW;
    lastCapH_ = job.entry.imgH;
    lastCapMs_ = nowMs;

    {
        std::lock_guard<std::mutex> lk(qmu_);
        while (queue_.size() >= 64) queue_.pop_front();  // 队列上限：快速复制大图防积压数百 MB（丢最旧保最新）
        queue_.push_back(std::move(job));
    }
    qcv_.notify_one();
}

// ---------------------------------------------------------- 后台落盘线程

void ClipboardMonitor::WorkerMain() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool com = SUCCEEDED(hr);
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(qmu_);
            qcv_.wait(lk, [&] { return quit_.load() || !queue_.empty(); });
            if (quit_.load() && queue_.empty()) break;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        if (job.reject) {  // 图片超限：主线程转托盘一次性气泡
            PostMessageW(notify_, uiMsg_ + 1, 0, 0);
            continue;
        }
        if (job.entry.id.empty())
            job.entry.id = ClipNewId(job.entry.ts);  // blob 文件名依赖 id，须在落盘前分配

        // 去重预检：同哈希同主格式 → 无需落盘，直接移顶刷新
        bool dup = false;
        if (!job.entry.payloads.empty()) {
            auto items = store_->Snapshot();
            for (auto& it : items) {
                if (it.hash == job.entry.hash && !it.payloads.empty() &&
                    it.payloads[0].fmt == job.entry.payloads[0].fmt) {
                    dup = true;
                    break;
                }
            }
        }

        int pngIdx = 0;  // 同条目第几个 png（多内嵌图命名 <id>.2.png…，缩略图取主图）
        for (auto& pl : job.entry.payloads) {
            if (dup) break;
            if (pl.fmt == "png") {
                std::string pngBytes = pl.text;
                if (pngBytes.empty() && !job.dibRaw.empty() && pngIdx == 0) {
                    int w = 0, h = 0;
                    DibToPng(job.dibRaw, job.dibIsV5, pngBytes, w, h);  // 失败→载荷清空
                }
                if (pngBytes.empty()) {
                    pl.fmt.clear();
                    continue;
                }
                if (pngIdx == 0) {
                    std::string thumb;
                    if (ScalePngThumb(pngBytes, thumb)) store_->WriteThumb(job.entry, thumb);
                }
                ClipPayload persisted;
                persisted.fmt = "png";
                if (store_->WriteBlob(job.entry, persisted, pngBytes, pngIdx)) {
                    pl.file = persisted.file;
                    pl.size = persisted.size;
                } else {
                    pl.text = pngBytes;  // 落盘失败退回内联
                    pl.size = pngBytes.size();
                }
                ++pngIdx;
                continue;
            }
            if (pl.size > 4096) {  // 大文本/HTML/路径表 → blob
                ClipPayload persisted;
                persisted.fmt = pl.fmt;
                if (store_->WriteBlob(job.entry, persisted, pl.text)) {
                    pl.file = persisted.file;
                    pl.text.clear();
                }
            }
        }
        if (!dup) {
            job.entry.payloads.erase(std::remove_if(job.entry.payloads.begin(),
                                                    job.entry.payloads.end(),
                                                    [](const ClipPayload& p) {
                                                        return p.fmt.empty();
                                                    }),
                                     job.entry.payloads.end());
            if (job.entry.payloads.empty()) continue;  // 全部失败 → 丢弃
        }
        store_->Prepend(job.entry, opts_.maxEntries, opts_.blobMaxMB << 20);
        PostMessageW(notify_, uiMsg_, 0, 0);  // UI 刷新（面板打开时实时插入）
    }
    if (com) CoUninitialize();
}

// ---------------------------------------------------------- 快捷复制回写

bool CopyClipToClipboard(
    HWND owner, const ClipEntry& e,
    const std::function<bool(const std::string&, std::string&)>& loader,
    const std::function<void(std::vector<std::wstring>&)>& pngLister) {
    if (!owner || !IsWindow(owner)) return false;
    SelfWriteGuard guard;  // 之后打开剪贴板/Empty 触发的 WM_CLIPBOARDUPDATE 被忽略
    // OpenClipboard 可能被常驻应用（聊天工具轮询等）占用：短重试
    bool opened = false;
    for (int i = 0; i < 6 && !opened; ++i) {
        opened = OpenClipboard(owner) != FALSE;
        if (!opened) Sleep(40);
    }
    if (!opened) return false;
    EmptyClipboard();  // 关键：清掉旧内容，否则旧格式残留导致"粘贴出旧内容"

    bool ok = false;
    auto setFmt = [&](UINT fmt, const std::string& data) {
        if (data.empty()) return false;
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, data.size());
        if (!h) return false;
        auto* p = GlobalLock(h);
        if (!p) {
            GlobalFree(h);
            return false;
        }
        memcpy(p, data.data(), data.size());
        GlobalUnlock(h);
        if (!SetClipboardData(fmt, h)) {  // 成功后所有权归系统
            GlobalFree(h);
            return false;
        }
        return true;
    };

    // 载荷写入顺序：文本优先（HTML/PNG 的纯文本兜底不再重复写）
    bool textWritten = false;
    std::string bytes;

    // text（纯文本条目 + 图文混合条目（image+真文字，微信输入框混合复制）；
    // html 条目的纯文本由 html 分支附带；纯图条目的"[图片]"占位已在捕获时剥除）
    for (auto& pl : e.payloads) {
        if ((e.type == "text" || e.type == "image") && pl.fmt == "text" &&
            loader("text", bytes) && !bytes.empty()) {
            textWritten = setFmt(CF_UNICODETEXT, WideBytes(ToWide(bytes)));
            ok = ok || textWritten;
            break;
        }
    }
    // html（表格关键：连同纯文本一起回写）
    UINT fmtHtml = RegisterClipboardFormatW(L"HTML Format");
    for (auto& pl : e.payloads) {
        // 仅 html 条目回写 HTML；image 条目（含聊天内嵌图提取）带着的 html 是
        // 原聊天片段——粘贴端会优先读它得到"[图片]"占位文字，必须跳过
        if (pl.fmt == "html" && e.type == "html" && fmtHtml && loader("html", bytes) &&
            !bytes.empty()) {
            bool w = setFmt(fmtHtml, bytes);
            ok = ok || w;
            if (!textWritten) {
                std::string txt;
                if (loader("text", txt) && !txt.empty())
                    setFmt(CF_UNICODETEXT, WideBytes(ToWide(txt)));  // 完整文本，而非 120 字预览
                else if (!e.text.empty())
                    setFmt(CF_UNICODETEXT, WideBytes(e.text));
            }
            break;
        }
    }
    // png → 单图：PNG 格式 + CF_DIB（粘贴出位图）；多图（聊天内嵌多图）→
    // CF_HDROP 文件列表——Windows 剪贴板一次只能承载一个位图对象，文件列表
    // 才能一次粘贴/拖出全部图片（与资源管理器复制多图文件一致）
    UINT fmtPng = RegisterClipboardFormatW(L"PNG");
    std::vector<std::wstring> pngFiles;
    if (pngLister) pngLister(pngFiles);
    bool multiImg = e.type == "image" && pngFiles.size() >= 2;
    if (multiImg) {
        std::wstring list;
        for (auto& f : pngFiles) {
            list += f;
            list += L'\0';
        }
        list += L'\0';
        DROPFILES df{};
        df.pFiles = sizeof(DROPFILES);
        df.fWide = TRUE;
        std::string out;
        out.append(reinterpret_cast<const char*>(&df), sizeof(df));
        out.append(reinterpret_cast<const char*>(list.c_str()),
                   list.size() * sizeof(wchar_t));
        bool wh = setFmt(CF_HDROP, out);
        ok = ok || wh;
    }
    if (!multiImg)
    for (auto& pl : e.payloads) {
        if (pl.fmt == "png" && fmtPng && loader("png", bytes) && !bytes.empty()) {
            bool w = setFmt(fmtPng, bytes);
            ok = ok || w;
            // PNG → 32bpp 顶朝下 DIB
            ComPtr<IWICImagingFactory> fac;
            if (WicFactory(&fac)) {
                HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
                if (hg) {
                    {
                        auto* p = GlobalLock(hg);
                        if (p) { memcpy(p, bytes.data(), bytes.size()); GlobalUnlock(hg); }  // 锁失败跳过
                    }
                    ComPtr<IStream> stream;
                    ComPtr<IWICBitmapDecoder> dec;
                    ComPtr<IWICBitmapFrameDecode> frame;
                    ComPtr<IWICFormatConverter> conv;
                    UINT iw = 0, ih = 0;
                if (SUCCEEDED(CreateStreamOnHGlobal(hg, FALSE, &stream)) &&
                        SUCCEEDED(fac->CreateDecoderFromStream(
                            stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec)) &&
                        SUCCEEDED(dec->GetFrame(0, &frame)) &&
                        SUCCEEDED(fac->CreateFormatConverter(&conv)) &&
                        SUCCEEDED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                                                   WICBitmapDitherTypeNone, nullptr, 0.f,
                                                   WICBitmapPaletteTypeCustom)) &&
                        SUCCEEDED(conv->GetSize(&iw, &ih)) && iw && ih) {
                        BITMAPINFO bi{};
                        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                        bi.bmiHeader.biWidth = static_cast<LONG>(iw);
                        bi.bmiHeader.biHeight = -static_cast<LONG>(ih);  // 顶朝下
                        bi.bmiHeader.biPlanes = 1;
                        bi.bmiHeader.biBitCount = 32;
                        bi.bmiHeader.biCompression = BI_RGB;
                        bi.bmiHeader.biSizeImage = iw * ih * 4;
                        std::vector<char> dib(sizeof(BITMAPINFOHEADER) + iw * ih * 4);
                        memcpy(dib.data(), &bi.bmiHeader, sizeof(BITMAPINFOHEADER));
                        WICRect rc{0, 0, static_cast<INT>(iw), static_cast<INT>(ih)};
                        if (SUCCEEDED(conv->CopyPixels(&rc, iw * 4, iw * ih * 4,
                                                       reinterpret_cast<BYTE*>(dib.data()) +
                                                           sizeof(BITMAPINFOHEADER)))) {
                            bool wd = setFmt(CF_DIB, std::string(dib.data(), dib.size()));
                            ok = ok || wd;
                        }
                    }
                    GlobalFree(hg);
                }
            }
            break;
        }
    }
    // hdrop → CF_HDROP + 纯文本兜底
    for (auto& pl : e.payloads) {
        if (pl.fmt == "hdrop" && loader("hdrop", bytes) && !bytes.empty()) {
            std::vector<std::wstring> paths;
            size_t start = 0;
            while (start < bytes.size()) {
                size_t nl = bytes.find('\n', start);
                if (nl == std::string::npos) nl = bytes.size();
                std::string one = bytes.substr(start, nl - start);
                if (!one.empty() && one.back() == '\r') one.pop_back();
                if (!one.empty()) paths.push_back(ToWide(one));
                start = nl + 1;
            }
            if (!paths.empty()) {
                std::wstring list;
                for (auto& p : paths) list += p + L'\0';
                list += L'\0';
                DROPFILES df{};
                df.pFiles = sizeof(DROPFILES);
                df.fWide = TRUE;
                std::string out;
                out.append(reinterpret_cast<const char*>(&df), sizeof(df));
                out.append(reinterpret_cast<const char*>(list.c_str()),
                           list.size() * sizeof(wchar_t));
                bool wh = setFmt(CF_HDROP, out);
                ok = ok || wh;
                if (!textWritten && !e.text.empty())
                    setFmt(CF_UNICODETEXT, WideBytes(e.text));
            }
            break;
        }
    }
    // 条目只有预览文本（如记录时的边缘情况）
    if (!ok && !e.text.empty()) {
        ok = setFmt(CF_UNICODETEXT, WideBytes(e.text));
    }
    CloseClipboard();
    return ok;
}

}  // namespace lb
