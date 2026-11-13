#include "core/clipboard_store.hpp"
#include "core/strutil.hpp"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <ctime>

namespace lb {
namespace json = nlohmann;
namespace fs = std::filesystem;

// ---------------------------------------------------------------- 纯函数

uint64_t ClipHash(const void* data, size_t len) {
    // FNV-1a 64
    const auto* p = static_cast<const unsigned char*>(data);
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

std::string ClipTypeFromFormats(const std::vector<std::string>& avail) {
    auto has = [&](const char* f) {
        for (auto& s : avail)
            if (s == f) return true;
        return false;
    };
    if (has("hdrop")) return "files";
    if (has("png")) return "image";
    if (has("html")) return "html";
    return "text";
}

std::wstring MakePreview(const std::wstring& text) {
    // 前 2 行拼接（空白行跳过），整体 ≤120（超长截断加 …）
    std::wstring out;
    int lines = 0;
    size_t i = 0;
    const size_t n = text.size();
    while (i < n && lines < 2) {
        size_t j = text.find(L'\n', i);
        if (j == std::wstring::npos) j = n;
        std::wstring line = text.substr(i, j - i);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty()) {
            if (!out.empty()) out += L" ";
            out += line;
            ++lines;
        }
        i = j + 1;
        while (i < n && (text[i] == L'\n' || text[i] == L'\r')) ++i;
    }
    if (out.size() > 120) out = out.substr(0, 120) + L"…";
    return out;
}

// ---------------------------------------------------------------- JSON 行

std::string ClipEntryToJson(const ClipEntry& e) {
    json::json ps = json::json::array();
    for (auto& p : e.payloads) {
        json::json j{{"fmt", p.fmt}, {"file", ToUtf8(p.file)}, {"size", p.size}};
        if (p.file.empty()) j["text"] = p.text;  // 内联小内容
        ps.push_back(std::move(j));
    }
    json::json j{{"id", e.id},
                 {"ts", e.ts},
                 {"type", e.type},
                 {"pinned", e.pinned},
                 {"hash", e.hash},
                 {"source", ToUtf8(e.source)},
                 {"imgW", e.imgW},
                 {"imgH", e.imgH},
                 {"text", ToUtf8(e.text)},
                 {"textInline", e.textInline},
                 {"payloads", ps}};
    return j.dump();
}

bool ClipEntryFromJson(const std::string& utf8, ClipEntry& out) {
    try {
        json::json j = json::json::parse(utf8);
        out = ClipEntry{};
        out.id = j.value("id", "");
        out.ts = j.value("ts", 0ll);
        out.type = j.value("type", "text");
        out.pinned = j.value("pinned", false);
        out.hash = j.value("hash", 0ull);
        out.source = ToWide(j.value("source", ""));
        out.imgW = j.value("imgW", 0);
        out.imgH = j.value("imgH", 0);
        out.text = ToWide(j.value("text", ""));
        out.textInline = j.value("textInline", false);
        if (j.contains("payloads") && j["payloads"].is_array()) {
            for (auto& q : j["payloads"]) {
                ClipPayload p;
                p.fmt = q.value("fmt", "");
                p.file = ToWide(q.value("file", ""));
                p.size = q.value("size", 0ull);
                if (q.contains("text")) p.text = q.value("text", "");
                if (!p.fmt.empty()) out.payloads.push_back(std::move(p));
            }
        }
        return !out.id.empty();
    } catch (...) {
        return false;  // 损坏行跳过（journal 同款语义）
    }
}

// ---------------------------------------------------------------- Store

namespace {
const char* BlobExt(const std::string& fmt) {
    if (fmt == "html") return "html";
    if (fmt == "png") return "png";
    return "txt";  // text / hdrop 均按 UTF-8 行文本存
}

void WriteFileAtomic(const std::wstring& path, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    auto tmp = path + L".tmp";
    bool ok = false;
    { std::ofstream f(tmp, std::ios::binary); f << bytes; f.flush(); ok = f.good(); }
    if (!ok) { fs::remove(tmp, ec); return; }  // 写失败（磁盘满等）不覆盖正常文件，避免静默数据丢失
    fs::rename(tmp, path, ec);
}
}  // namespace

void ClipStore::Load() {
    std::lock_guard<std::mutex> lk(mu_);
    items_.clear();
    std::ifstream f(dir_ + L"\\index.jsonl", std::ios::binary);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        ClipEntry e;
        if (ClipEntryFromJson(line, e)) items_.push_back(std::move(e));
        // blob 缺失不剔除：条目保留，复制时 LoadPayload 失败即静默
    }
}

std::string ClipNewId(long long ts) {
    static std::mt19937 rng(std::random_device{}());
    char base36[32];
    // ts → base36
    unsigned long long v = static_cast<unsigned long long>(ts);
    int i = 0;
    const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (v == 0) base36[i++] = '0';
    while (v) { base36[i++] = digits[v % 36]; v /= 36; }
    std::string id(base36, base36 + i);
    unsigned r = rng();
    for (int k = 0; k < 4; ++k) {  // 随机 4 字节，防同秒碰撞
        id += digits[(r >> (k * 8)) & 0xF];
        id += digits[((r >> (k * 8)) >> 4) & 0xF];
    }
    std::reverse(id.begin(), id.end());
    return id;
}

bool ClipStore::Prepend(ClipEntry e, int maxEntries, uint64_t blobMaxBytes) {
    std::lock_guard<std::mutex> lk(mu_);
    if (e.id.empty()) e.id = ClipNewId(e.ts ? e.ts : std::time(nullptr));
    bool isNew = true;
    for (auto& it : items_) {
        if (it.hash == e.hash && !it.payloads.empty() && !e.payloads.empty() &&
            it.payloads[0].fmt == e.payloads[0].fmt) {
            // 去重：移顶 + 刷新时间戳（置顶条目留在置顶区，仅刷新）
            it.ts = e.ts;
            isNew = false;
            break;
        }
    }
    if (isNew) {
        // 插到置顶区之后
        size_t pos = 0;
        while (pos < items_.size() && items_[pos].pinned) ++pos;
        items_.insert(items_.begin() + static_cast<long>(pos), std::move(e));
    } else {
        // 重排：pinned 优先，其余按 ts 降序
        std::stable_sort(items_.begin(), items_.end(), [](const ClipEntry& a, const ClipEntry& b) {
            if (a.pinned != b.pinned) return a.pinned > b.pinned;
            return a.ts > b.ts;
        });
    }
    EnforceCaps(maxEntries, blobMaxBytes);
    SaveLocked();
    return isNew;
}

void ClipStore::EnforceCaps(int maxEntries, uint64_t blobMaxBytes) {
    // 要求：items_ 已排好（pinned 在前）。淘汰只针对非置顶、从最旧开始（尾部）。
    auto blobBytes = [&] {
        uint64_t s = 0;
        for (auto& it : items_)
            for (auto& p : it.payloads) s += p.size;
        return s;
    };
    // 条数上限
    while (static_cast<int>(items_.size()) > maxEntries) {
        // 找最旧的非置顶
        long idx = -1;
        for (long i = static_cast<long>(items_.size()) - 1; i >= 0; --i)
            if (!items_[static_cast<size_t>(i)].pinned) { idx = i; break; }
        if (idx < 0) break;
        DeleteEntryFiles(items_[static_cast<size_t>(idx)]);
        items_.erase(items_.begin() + idx);
    }
    // 字节上限
    while (blobBytes() > blobMaxBytes) {
        long idx = -1;
        for (long i = static_cast<long>(items_.size()) - 1; i >= 0; --i)
            if (!items_[static_cast<size_t>(i)].pinned) { idx = i; break; }
        if (idx < 0) break;
        DeleteEntryFiles(items_[static_cast<size_t>(idx)]);
        items_.erase(items_.begin() + idx);
    }
}

bool ClipStore::Remove(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].id == id) {
            DeleteEntryFiles(items_[i]);
            items_.erase(items_.begin() + static_cast<long>(i));
            SaveLocked();
            return true;
        }
    }
    return false;
}

bool ClipStore::Clear() {
    std::lock_guard<std::mutex> lk(mu_);
    bool any = false;  // 是否确实清除了非置顶条目（只剩置顶时返回 false）
    std::vector<ClipEntry> keep;
    for (auto& it : items_) {
        if (it.pinned)
            keep.push_back(std::move(it));
        else {
            DeleteEntryFiles(it);
            any = true;
        }
    }
    items_.swap(keep);
    if (any) SaveLocked();
    return any;
}

bool ClipStore::SetPinned(const std::string& id, bool pinned) {
    std::lock_guard<std::mutex> lk(mu_);
    // 置顶数上限 10
    if (pinned) {
        size_t n = 0;
        for (auto& it : items_)
            if (it.pinned) ++n;
        if (n >= 10) return false;
    }
    for (auto& it : items_) {
        if (it.id == id) {
            it.pinned = pinned;
            std::stable_sort(items_.begin(), items_.end(),
                             [](const ClipEntry& a, const ClipEntry& b) {
                                 if (a.pinned != b.pinned) return a.pinned > b.pinned;
                                 return a.ts > b.ts;
                             });
            SaveLocked();
            return true;
        }
    }
    return false;
}

bool ClipStore::Save() {
    std::lock_guard<std::mutex> lk(mu_);
    return SaveLocked();
}

bool ClipStore::SaveLocked() const {
    std::string out;
    for (auto& e : items_) {
        out += ClipEntryToJson(e);
        out += '\n';
    }
    std::error_code ec;
    fs::create_directories(dir_, ec);
    auto tmp = dir_ + L"\\index.jsonl.tmp";
    bool ok = false;
    { std::ofstream f(tmp, std::ios::binary); f << out; f.flush(); ok = f.good(); }
    if (!ok) { fs::remove(tmp, ec); return false; }  // 写失败不覆盖正常索引
    fs::rename(tmp, dir_ + L"\\index.jsonl", ec);  // Windows 上原子替换
    return !ec;
}

std::vector<ClipEntry> ClipStore::Snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    return items_;
}

bool ClipStore::Empty() const {
    std::lock_guard<std::mutex> lk(mu_);
    return items_.empty();
}

size_t ClipStore::Count() const {
    std::lock_guard<std::mutex> lk(mu_);
    return items_.size();
}

bool ClipStore::LoadPayload(const ClipEntry& e, const std::string& fmt,
                            std::string& out) const {
    for (auto& p : e.payloads) {
        if (p.fmt != fmt) continue;
        if (p.file.empty()) {
            out = p.text;
            return true;
        }
        std::ifstream f(BlobPath(e, p), std::ios::binary);
        if (!f) return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return !out.empty() || p.size == 0;
    }
    return false;
}

std::wstring ClipStore::BlobDir() const { return dir_ + L"\\blobs"; }
std::wstring ClipStore::ThumbDir() const { return dir_ + L"\\thumbs"; }

std::wstring ClipStore::BlobPath(const ClipEntry& e, const ClipPayload& p) const {
    // blob 引用名是事实来源；缺失时按 id+格式回退（与 WriteBlob 命名一致）
    if (!p.file.empty()) return dir_ + L"\\blobs\\" + p.file;
    return dir_ + L"\\blobs\\" + ToWide(e.id) + L"." + ToWide(BlobExt(p.fmt));
}

std::wstring ClipStore::ThumbPath(const ClipEntry& e) const {
    return dir_ + L"\\thumbs\\" + ToWide(e.id) + L".png";
}

bool ClipStore::WriteBlob(const ClipEntry& e, ClipPayload& p, const std::string& bytes,
                          int idx) {
    if (bytes.empty()) return false;
    std::lock_guard<std::mutex> lk(mu_);
    p.file = ToWide(e.id) + (idx > 0 ? L"." + std::to_wstring(idx + 1) : L"") + L"." +
             ToWide(BlobExt(p.fmt));
    WriteFileAtomic(BlobDir() + L"\\" + p.file, bytes);
    p.size = bytes.size();
    std::error_code ec;
    bool ok = fs::exists(BlobDir() + L"\\" + p.file, ec) && !ec;
    if (ok) {
        // 登记到已存条目（「先 Prepend 后补 blob」的调用顺序下保持 store 自洽：
        // Remove/Clear 才能连带清理 blob 文件）。同格式多载荷按出现序号消耗，
        // 避免第二个 png 被登记成第一个的名字。
        for (auto& it : items_) {
            if (it.id != e.id) continue;
            int seen = 0;
            for (auto& q : it.payloads) {
                if (q.fmt != p.fmt || !q.file.empty()) continue;
                if (seen == idx) {
                    q.file = p.file;
                    q.size = p.size;
                    break;
                }
                ++seen;
            }
            break;
        }
    }
    return ok;
}

bool ClipStore::WriteThumb(const ClipEntry& e, const std::string& pngBytes) {
    if (pngBytes.empty()) return false;
    WriteFileAtomic(ThumbDir() + L"\\" + ToWide(e.id) + L".png", pngBytes);
    return true;
}

void ClipStore::DeleteEntryFiles(const ClipEntry& e) {
    std::error_code ec;
    for (auto& p : e.payloads) {
        if (!p.file.empty()) fs::remove(BlobPath(e, p), ec);
    }
    fs::remove(ThumbPath(e), ec);
}

}  // namespace lb
