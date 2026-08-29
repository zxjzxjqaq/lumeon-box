#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace lb {

struct SpikeConfig {
    std::string mode = "independent";  // independent | desktop | bottom
    bool anchored = true;              // true=自动锚定工作区右下；false=用户手动摆放
    // 格子槽位顺序：8 个槽位（2 行 × 4 列），值为类目键，空串 = 空槽
    std::vector<std::string> order;
    std::vector<std::string> collapsed;  // 折叠的类目键
    int x = 200, y = 200;
    int w = 520, h = 320;
};

class ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path file) : file_(std::move(file)) {}

    SpikeConfig load() const;               // 缺失/损坏 → 默认值，不抛异常
    bool save(const SpikeConfig& c) const;  // tmp + rename 原子写

    static std::filesystem::path default_dir();  // %APPDATA%\LumenBox

private:
    std::filesystem::path file_;
};

}  // namespace lb
