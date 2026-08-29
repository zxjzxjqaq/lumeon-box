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
        c.free = j.value("free", c.free);
        if (j.contains("boxes") && j["boxes"].is_object()) {
            for (auto it = j["boxes"].begin(); it != j["boxes"].end(); ++it) {
                BoxState b;
                b.x = it.value().value("x", 0);
                b.y = it.value().value("y", 0);
                b.collapsed = it.value().value("collapsed", false);
                c.boxes[it.key()] = b;
            }
        }
        c.x = j.value("x", c.x);
        c.y = j.value("y", c.y);
        c.w = j.value("w", c.w);
        c.h = j.value("h", c.h);
        if (c.mode != "independent" && c.mode != "desktop" && c.mode != "bottom")
            c.mode = "independent";
    } catch (...) {
        return SpikeConfig{};  // 损坏即回默认，绝不阻塞启动
    }
    return c;
}

bool ConfigStore::save(const SpikeConfig& c) const {
    json::json boxes = json::json::object();
    for (auto& [key, b] : c.boxes)
        boxes[key] = {{"x", b.x}, {"y", b.y}, {"collapsed", b.collapsed}};
    json::json j{{"mode", c.mode},
                 {"anchored", c.anchored},
                 {"free", c.free},
                 {"boxes", boxes},
                 {"x", c.x},
                 {"y", c.y},
                 {"w", c.w},
                 {"h", c.h}};
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
