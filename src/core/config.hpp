#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace lb {

// 一个容器/格子：用户可自建。盒子=容器根下 <folder> 子文件夹；待办盒特殊（无文件）。
struct BoxDef {
    std::string id;      // 稳定唯一键：tasks / b1 / b2 ...
    std::wstring name;   // 显示名（用户可就地重命名）
    std::wstring folder; // 容器内子目录（data 盒为 box_bN；tasks 盒为空）
    std::string type = "data";  // "tasks" | "data"
    bool collapsed = false;
};

struct SpikeConfig {
    std::string mode = "independent";  // independent | desktop | bottom
    bool anchored = true;              // true=自动锚定工作区右下；false=用户手动摆放
    // 容器清单（显示顺序）。缺省=待办 + 默认容器 两盒。
    std::vector<BoxDef> boxes;
    int x = 200, y = 200;
    int w = 520, h = 320;
    // M2：自动归类监听策略（off|prompt|auto；不要按格式自动归类，默认 off）
    std::string watcherMode = "off";
    // M2：双击隐藏图标实现（hook|hotkey）
    std::string hideIconsMode = "hook";
    // M5：剪贴板历史（设计文档 D7–D9 已拍板）
    bool clipboardEnabled = true;
    std::string clipboardHotkey = "Ctrl+B";  // 呼出热键（可改）
    int clipboardMaxEntries = 100;           // 条数上限（20–500）
    int clipboardBlobMB = 64;                // 磁盘 blob 硬顶（MB）
    bool clipboardRecordSource = true;       // 记录来源 exe 名
    bool clipboardPaused = false;            // 暂停记录（托盘/面板共享，持久化）
};

// 新装/无 boxes 时的默认两盒：待办 + 默认容器
std::vector<BoxDef> DefaultBoxes();

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
