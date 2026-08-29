#pragma once
#include <filesystem>
#include <map>
#include <string>

namespace lb {

// 单个格子的自由摆放状态（grid 坐标，DIP）
struct BoxState {
    int x = 0, y = 0;
    bool collapsed = false;
};

struct SpikeConfig {
    std::string mode = "independent";  // independent | desktop | bottom
    bool anchored = true;              // true=自动锚定工作区右下；false=用户手动摆放
    bool free = false;                 // true=格子自由摆放（每格独立位置）
    std::map<std::string, BoxState> boxes;  // key: doc/image/video/audio/archive/installer/other
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
