#pragma once
// 剪贴板历史存储：条目模型 / 哈希去重 / LRU 淘汰 / index.jsonl 持久化。
// 纯 C++ 核心（除文件 IO 外无 Win32 依赖），可独立单测。
// 并发：后台落盘线程调 Prepend/Save，UI 线程调 Items/Remove —— 内部互斥锁保护。
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace lb {

// 一个格式载荷。file 为空 = 数据内联在 text（≤4KB 的小内容）；否则指向 blobs\ 下文件。
struct ClipPayload {
    std::string fmt;        // "text" | "html" | "png" | "hdrop"
    std::wstring file;      // blob 文件名（不含路径）
    uint64_t size = 0;      // 字节数
    std::string text;       // 内联数据（UTF-8）
};

// 一条剪贴板历史。一次复制事件 = 一个条目 = 多个格式载荷（多格式一起回写）。
struct ClipEntry {
    std::string id;         // ts_base36 + 随机 4 字节（hex）
    long long ts = 0;       // Unix 秒
    std::string type;       // 展示类型："text" | "html" | "image" | "files"
    bool pinned = false;    // 置顶：不淘汰、不参与清空
    std::wstring text;      // 预览文本（textInline=true 时为完整内联文本）
    bool textInline = false;
    uint64_t hash = 0;      // FNV-1a 64（主载荷字节）
    std::wstring source;    // 来源 exe 名（可空）
    int imgW = 0, imgH = 0; // 图片尺寸（仅 image）
    std::vector<ClipPayload> payloads;
};

// FNV-1a 64 哈希（去重依据）
uint64_t ClipHash(const void* data, size_t len);
inline uint64_t ClipHash(const std::string& s) { return ClipHash(s.data(), s.size()); }

// 生成条目 id：ts_base36 + 随机 4 字节（hex）。采集时即分配（blob 文件名依赖它）。
std::string ClipNewId(long long ts);

// 格式优先级判定：输入按优先级顺序排列的可用格式名（归一化小写：
// "hdrop"/"png"/"html"/"text"），输出展示类型。全不认识 → "text"。
// 规则（设计文档 §5.2）：files > image > html > text；同条目多格式并存。
std::string ClipTypeFromFormats(const std::vector<std::string>& avail);

// 预览生成：多行文本取前 2 行（每行截断），整体 ≤120 字符；空串安全。
std::wstring MakePreview(const std::wstring& text);

// 条目 → JSON 行 / JSON 行 → 条目（损坏返回 false，调用方跳过该行）
std::string ClipEntryToJson(const ClipEntry& e);
bool ClipEntryFromJson(const std::string& utf8, ClipEntry& out);

// 历史存储：dir\index.jsonl + dir\blobs\<file> + dir\thumbs\<file>
class ClipStore {
public:
    explicit ClipStore(std::wstring dir) : dir_(std::move(dir)) {}

    // 启动时读入 index.jsonl（缺失/损坏行跳过；blob 缺失容错保留条目）
    void Load();

    // 新条目入列（移动到最前）。哈希命中已有条目 → 移顶 + 刷新时间戳，返回 false；
    // 新条目返回 true。随后自动执行条数/字节淘汰（置顶豁免）并追加写 index.jsonl。
    // 注意：淘汰需要重写快照（截掉尾部行），故这里统一走 Save()。
    bool Prepend(ClipEntry e, int maxEntries, uint64_t blobMaxBytes);

    // 删除单条（含 blob + 缩略图）。找不到返回 false。成功后自动持久化。
    bool Remove(const std::string& id);

    // 一键清空：删除全部非置顶条目（含 blob）；置顶保留。成功后自动持久化。
    bool Clear();

    // 全量原子重写 index.jsonl（tmp + rename）。所有变更操作内部已自动调用。
    bool Save();

    // 快照：UI 用（拷贝出条目列表，锁内完成；跨线程安全读取统一走此接口）
    std::vector<ClipEntry> Snapshot() const;

    // 置顶/取消置顶（置顶数上限 10，超出返回 false 并不变更）。成功后自动持久化。
    bool SetPinned(const std::string& id, bool pinned);

    bool Empty() const;
    size_t Count() const;

    // 读取载荷字节：内联取 text，blob 读文件（缺失返回 false）
    bool LoadPayload(const ClipEntry& e, const std::string& fmt, std::string& out) const;

    // blob / 缩略图绝对路径
    std::wstring BlobDir() const;
    std::wstring ThumbDir() const;
    std::wstring BlobPath(const ClipEntry& e, const ClipPayload& p) const;
    std::wstring ThumbPath(const ClipEntry& e) const;

    // 写 blob 文件（后台线程调用）；写入成功后 p.file 被赋值
    // idx>0：同格式多载荷命名 <id>.<idx+1>.<ext>（如聊天片段的多个内嵌图）
    bool WriteBlob(const ClipEntry& e, ClipPayload& p, const std::string& bytes,
                   int idx = 0);
    bool WriteThumb(const ClipEntry& e, const std::string& pngBytes);

private:
    void EnforceCaps(int maxEntries, uint64_t blobMaxBytes);
    bool SaveLocked() const;  // 须已持锁
    void DeleteEntryFiles(const ClipEntry& e);

    std::wstring dir_;
    mutable std::mutex mu_;
    std::vector<ClipEntry> items_;  // 最新在最前（含置顶，置顶排前面）
};

}  // namespace lb
