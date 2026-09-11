#pragma once
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <string>
#include <thread>
#include "core/clipboard_store.hpp"

namespace lb {

// 采集参数（config 注入）
struct ClipboardOpts {
    int maxEntries = 100;             // 条数上限（20–500）
    uint64_t blobMaxMB = 64;          // 磁盘 blob 硬顶（MB）
    bool recordSource = true;         // 记录来源 exe 名
    int textMaxBytes = 1 << 20;       // 文本 >1MB 截断
    int imgMaxPixels = 8192;          // 图片最长边拒收阈值（D9）
    uint64_t imgMaxBytes = 32ull << 20;  // 解码后字节数拒收阈值（D9）
};

// 快捷复制回写（UI 线程调用）：把条目全部载荷还原进系统剪贴板。
// loader(fmt, bytes) 负责按格式名取原始字节（store.LoadPayload 的包装）。
// pngLister(out)（可选）：列出条目全部 png blob 路径——多图条目（聊天内嵌多图）
// 回写为 CF_HDROP 文件列表，粘贴/拖拽得到全部图片（与资源管理器复制多图一致）；
// 单图仍写 PNG+CF_DIB 位图。
// 全程置自写抑制标志——自己的回写不会触发再次记录。返回是否有任一格式写入成功。
bool CopyClipToClipboard(
    HWND owner, const ClipEntry& e,
    const std::function<bool(const std::string&, std::string&)>& loader,
    const std::function<void(std::vector<std::wstring>&)>& pngLister = nullptr);

// 剪贴板监听：隐藏小窗 + AddClipboardFormatListener（消息驱动，零轮询，非钩子）。
// Start 须在主（UI）线程调用；WM_CLIPBOARDUPDATE 在主循环分发，采集只做 memcpy（≤5ms），
// DIB→PNG 编码 / 缩略图 / 落盘 / Prepend+Save 都在内部后台线程，完成后 PostMessageW(notify, uiMsg)。
// 图片超限拒收时 PostMessageW(notify, uiMsg+1)（主线程转为托盘一次性气泡）。
class ClipboardMonitor {
public:
    ~ClipboardMonitor() { Stop(); }

    bool Start(ClipStore* store, HWND notify, UINT uiMsg, const ClipboardOpts& opts);
    void Stop();

    void SetPaused(bool p) { paused_ = p; }
    bool Paused() const { return paused_; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    // UI 线程：打开剪贴板并 memcpy 各格式字节，组装条目 → 交给后台线程
    void OnClipboardUpdate();
    void WorkerMain();

    struct Job {
        ClipEntry entry;         // 元数据 + 内联小载荷（png/hdrop/dib 原始字节在 raw 里）
        std::string dibRaw;      // 原始 CF_DIB / CF_DIBV5 字节（待转 PNG；空=无需转换）
        bool dibIsV5 = false;    // V5 头（32bpp 可带 alpha）
        bool reject = false;     // 图片超限拒收（仅用于后台线程发 uiMsg+1）
    };

    ClipStore* store_ = nullptr;
    HWND notify_ = nullptr;
    UINT uiMsg_ = 0;
    ClipboardOpts opts_;
    HWND hwnd_ = nullptr;
    DWORD lastSeq_ = 0;
    // 图片防抖：同类型+同尺寸+500ms 内的多次更新视为同一次复制
    ULONGLONG lastCapMs_ = 0;
    std::string lastCapType_;
    int lastCapW_ = 0, lastCapH_ = 0;
    std::atomic<bool> paused_{false};
    std::atomic<bool> quit_{false};

    // 后台落盘线程（沿用 watcher 线程模式）
    std::thread th_;
    std::mutex qmu_;
    std::condition_variable qcv_;
    std::deque<Job> queue_;
};

}  // namespace lb
