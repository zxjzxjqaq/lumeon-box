#pragma once
#include <filesystem>
#include <string>

namespace lb {

struct SpikeConfig {
    std::string mode = "independent";  // independent | desktop | bottom
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
