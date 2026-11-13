#include "core/journal.hpp"
#include <ctime>
#include <fstream>
#include <nlohmann/json.hpp>
#include "core/strutil.hpp"

namespace lb {
namespace json = nlohmann;

static std::wstring NowStamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    wchar_t buf[32];
    wcsftime(buf, 32, L"%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

void Journal::AppendLine(const std::string& utf8) {
    std::ofstream f(path_, std::ios::app | std::ios::binary);
    f << utf8 << '\n';
}

// 滚动清理：只保留最近 maxLines 行，防止 journal.jsonl 无限增长（设计 §5.3/§8 磁盘预算）
static void TrimJournal(const std::wstring& path, size_t maxLines) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) lines.push_back(line);
    f.close();
    if (lines.size() <= maxLines) return;
    const size_t drop = lines.size() - maxLines;
    auto tmp = path + L".tmp";
    bool ok = false;
    { std::ofstream o(tmp, std::ios::binary);
      for (size_t i = drop; i < lines.size(); ++i) o << lines[i] << '\n';
      o.flush(); ok = o.good(); }
    std::error_code ec;
    if (!ok) { std::filesystem::remove(tmp, ec); return; }
    std::filesystem::rename(tmp, path, ec);
}

void Journal::BeginBatch() {
    json::json j{{"type", "batch"}, {"ts", ToUtf8(NowStamp())}};
    AppendLine(j.dump());
    TrimJournal(path_, 800);  // 每批次触发一次滚动清理（低频，开销可忽略）
}

void Journal::LogMove(const MoveRecord& r) {
    json::json j{{"type", "move"}, {"src", ToUtf8(r.src)}, {"dst", ToUtf8(r.dst)}};
    AppendLine(j.dump());
}

std::vector<std::vector<MoveRecord>> Journal::Batches() const {
    std::vector<std::vector<MoveRecord>> out;
    std::ifstream f(path_, std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        try {
            json::json j = json::json::parse(line);
            if (j.value("type", "") == "batch") {
                out.emplace_back();
            } else if (j.value("type", "") == "move" && !out.empty()) {
                out.back().push_back(
                    {ToWide(j.value("src", "")), ToWide(j.value("dst", ""))});
            }
        } catch (...) {
            // 损坏行跳过
        }
    }
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const auto& b) { return b.empty(); }),
              out.end());
    return out;
}

bool Journal::RemoveLastBatch() {
    auto batches = Batches();
    if (batches.empty()) return false;

    // 重读原始行，截掉最后一个非空批次头及其后所有行
    std::ifstream f(path_, std::ios::binary);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) lines.push_back(line);
    f.close();

    size_t lastHeader = std::string::npos;
    size_t groupMoves = 0;
    std::vector<size_t> headerOf;          // 每行如果是批次头记录其行号
    std::vector<std::pair<size_t, size_t>> groups;  // {headerLine, moveCount}
    for (size_t i = 0; i < lines.size(); ++i) {
        int type = 0;  // 1=batch 2=move
        try {
            auto j = json::json::parse(lines[i]);
            if (j.value("type", "") == "batch") type = 1;
            else if (j.value("type", "") == "move") type = 2;
        } catch (...) {}
        if (type == 1) {
            groups.push_back({i, 0});
        } else if (type == 2 && !groups.empty()) {
            groups.back().second++;
        }
    }
    for (auto it = groups.rbegin(); it != groups.rend(); ++it) {
        if (it->second > 0) { lastHeader = it->first; break; }
    }
    if (lastHeader == std::string::npos) return false;

    std::ofstream o(path_ + L".tmp", std::ios::binary);
    for (size_t i = 0; i < lastHeader; ++i) o << lines[i] << '\n';
    o.close();
    std::error_code ec;
    std::filesystem::rename(path_ + L".tmp", path_, ec);
    return !ec;
}

}  // namespace lb
