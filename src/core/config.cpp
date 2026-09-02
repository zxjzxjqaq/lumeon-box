#include "core/config.hpp"
#include "core/strutil.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <shlobj.h>

namespace lb {
namespace fs = std::filesystem;
namespace json = nlohmann;

std::vector<BoxDef> DefaultBoxes() {
    BoxDef tasks;
    tasks.id = "tasks";
    tasks.name = L"\u5F85\u529E";  // 待办
    tasks.folder = L"";
    tasks.type = "tasks";
    BoxDef data;
    data.id = "b1";
    data.name = L"\u9ED8\u8BA4\u5BB9\u5668";  // 默认容器
    data.folder = L"box_b1";
    data.type = "data";
    return {tasks, data};
}

SpikeConfig ConfigStore::load() const {
    SpikeConfig c;
    c.boxes = DefaultBoxes();  // 兜底：缺省两盒
    std::error_code ec;
    if (!fs::exists(file_, ec)) return c;
    try {
        json::json j = json::json::parse(std::ifstream(file_, std::ios::binary));
        c.mode = j.value("mode", c.mode);
        c.anchored = j.value("anchored", c.anchored);
        c.x = j.value("x", c.x);
        c.y = j.value("y", c.y);
        c.w = j.value("w", c.w);
        c.h = j.value("h", c.h);
        c.watcherMode = j.value("watcherMode", c.watcherMode);
        c.hideIconsMode = j.value("hideIconsMode", c.hideIconsMode);
        if (j.contains("boxes") && j["boxes"].is_array() && !j["boxes"].empty()) {
            std::vector<BoxDef> loaded;
            for (auto& e : j["boxes"]) {
                BoxDef b;
                b.id = e.value("id", "");
                if (e.contains("name") && e["name"].is_string())
                    b.name = ToWide(e["name"].get<std::string>());
                if (e.contains("folder") && e["folder"].is_string())
                    b.folder = ToWide(e["folder"].get<std::string>());
                b.type = e.value("type", "data");
                b.collapsed = e.value("collapsed", false);
                if (!b.id.empty()) loaded.push_back(std::move(b));
            }
            if (!loaded.empty()) c.boxes = std::move(loaded);
        }
        // 若没有待办盒则置顶插入；待办盒必须且仅一个
        bool hasTasks = false;
        for (auto& b : c.boxes)
            if (b.type == "tasks") { hasTasks = true; break; }
        if (!hasTasks) c.boxes.insert(c.boxes.begin(), DefaultBoxes()[0]);
        if (c.mode != "independent" && c.mode != "desktop" && c.mode != "bottom")
            c.mode = "independent";
        if (c.watcherMode != "off" && c.watcherMode != "prompt" &&
            c.watcherMode != "auto")
            c.watcherMode = "off";
        if (c.hideIconsMode != "hook" && c.hideIconsMode != "hotkey")
            c.hideIconsMode = "hook";
    } catch (...) {
        c.boxes = DefaultBoxes();  // 损坏即回默认两盒，绝不阻塞启动
    }
    return c;
}

bool ConfigStore::save(const SpikeConfig& c) const {
    nlohmann::json boxesArr = nlohmann::json::array();
    for (const auto& b : c.boxes)
        boxesArr.push_back({{"id", b.id},
                            {"name", ToUtf8(b.name)},
                            {"folder", ToUtf8(b.folder)},
                            {"type", b.type},
                            {"collapsed", b.collapsed}});
    json::json j{{"mode", c.mode},
                 {"anchored", c.anchored},
                 {"boxes", boxesArr},
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
