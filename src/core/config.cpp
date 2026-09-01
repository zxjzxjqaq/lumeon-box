#include "core/config.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <shlobj.h>

namespace lb {
namespace fs = std::filesystem;
namespace json = nlohmann;

SpikeConfig ConfigStore::load() const {
    SpikeConfig c;
    std::error_code ec;
    if (!fs::exists(file_, ec)) return c;
    try {
        json::json j = json::json::parse(std::ifstream(file_, std::ios::binary));
        c.mode = j.value("mode", c.mode);
        c.anchored = j.value("anchored", c.anchored);
        if (j.contains("order") && j["order"].is_array())
            for (auto& k : j["order"]) c.order.push_back(k.get<std::string>());
        if (j.contains("collapsed") && j["collapsed"].is_array())
            for (auto& k : j["collapsed"]) c.collapsed.push_back(k.get<std::string>());
        c.x = j.value("x", c.x);
        c.y = j.value("y", c.y);
        c.w = j.value("w", c.w);
        c.h = j.value("h", c.h);
        c.watcherMode = j.value("watcherMode", c.watcherMode);
        c.hideIconsMode = j.value("hideIconsMode", c.hideIconsMode);
        if (c.mode != "independent" && c.mode != "desktop" && c.mode != "bottom")
            c.mode = "independent";
        if (c.watcherMode != "off" && c.watcherMode != "prompt" &&
            c.watcherMode != "auto")
            c.watcherMode = "prompt";
        if (c.hideIconsMode != "hook" && c.hideIconsMode != "hotkey")
            c.hideIconsMode = "hook";
    } catch (...) {
        return SpikeConfig{};  // 损坏即回默认，绝不阻塞启动
    }
    return c;
}

bool ConfigStore::save(const SpikeConfig& c) const {
    json::json j{{"mode", c.mode},
                 {"anchored", c.anchored},
                 {"order", c.order},
                 {"collapsed", c.collapsed},
                 {"x", c.x},
                 {"y", c.y},
                 {"w", c.w},
                 {"h", c.h},
                 {"watcherMode", c.watcherMode},
                 {"hideIconsMode", c.hideIconsMode}};
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    auto tmp = file_.wstring() + L".tmp";
    { std::ofstream f(tmp, std::ios::binary); f << j.dump(2); }
    fs::rename(tmp, file_, ec);  // std::filesystem::rename 在 Windows 上原子替换已有目标
    return !ec;
}

fs::path ConfigStore::default_dir() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw)))
        return L".";
    fs::path dir(raw);
    CoTaskMemFree(raw);
    return dir / L"LumenBox";
}

}  // namespace lb
