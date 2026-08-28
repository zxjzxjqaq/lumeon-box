#pragma once
#include <string>
#include <vector>

namespace lb {

struct MoveRecord {
    std::wstring src, dst;
};

// JSONL 操作日志：{"type":"batch","ts":..} 开批，{"type":"move","src":..,"dst":..} 记移动。
// 撤销 = 逆序回放最后一个批次；文件损坏行静默跳过。
class Journal {
public:
    explicit Journal(std::wstring path) : path_(std::move(path)) {}

    void BeginBatch();
    void LogMove(const MoveRecord& r);
    std::vector<std::vector<MoveRecord>> Batches() const;   // 空批次会被剔除
    bool RemoveLastBatch();                                  // 撤销成功后调用（原子重写）

private:
    void AppendLine(const std::string& utf8);
    std::wstring path_;
};

}  // namespace lb
