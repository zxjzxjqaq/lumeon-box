#pragma once
#include <filesystem>
#include <map>
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
    // M2：自动归类监听策略（off|prompt|auto，D4 默认提示）
    // 用户明确不要按格式自动归类，默认改为 off；仍可在托盘手动开启
    std::string watcherMode = "off";
    // M2：双击隐藏图标实现（hook|hotkey，D6 默认钩子 + 可切热键）
    std::string hideIconsMode = "hook";
    // 每格自定义显示名（类目键 → 名称）；缺省用默认类目名
    std::map<std::string, std::string> boxNames;
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
