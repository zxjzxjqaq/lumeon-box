// tests/test_config.cpp —— 配置存储：默认值 / 往返 / 原子写 / 损坏容错
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include "core/config.hpp"

using namespace lb;
namespace fs = std::filesystem;

static fs::path temp_file() {
    auto dir = fs::temp_directory_path() / L"lumenbox_tests";
    fs::create_directories(dir);
    return dir / L"test-config.json";
}

TEST_CASE("load: 文件缺失返回默认值", "[config]") {
    ConfigStore store(fs::temp_directory_path() / L"no-such-dir" / L"x.json");
    auto c = store.load();
    REQUIRE(c.mode == "independent");
    REQUIRE(c.x == 200);
    REQUIRE(c.w == 520);
}

TEST_CASE("save+load 往返一致", "[config]") {
    auto file = temp_file();
    fs::remove(file);
    ConfigStore store(file);
    SpikeConfig c;
    c.mode = "desktop";
    c.x = 640;
    c.y = 480;
    c.anchored = false;  // 用户手动摆放过
    c.free = true;       // 格子自由摆放
    c.boxes["doc"] = {10, 20, true};
    c.boxes["image"] = {300, 40, false};
    REQUIRE(store.save(c));
    auto r = store.load();
    REQUIRE(r.mode == "desktop");
    REQUIRE(r.x == 640);
    REQUIRE(r.y == 480);
    REQUIRE_FALSE(r.anchored);
    REQUIRE(r.free);
    REQUIRE(r.boxes.size() == 2);
    REQUIRE(r.boxes["doc"].x == 10);
    REQUIRE(r.boxes["doc"].y == 20);
    REQUIRE(r.boxes["doc"].collapsed);
    REQUIRE(r.boxes["image"].x == 300);
    REQUIRE_FALSE(r.boxes["image"].collapsed);
}

TEST_CASE("save 原子替换：不留 .tmp 残留", "[config]") {
    auto file = temp_file();
    ConfigStore store(file);
    REQUIRE(store.save(SpikeConfig{}));
    REQUIRE_FALSE(fs::exists(file.wstring() + L".tmp"));
}

TEST_CASE("load: 文件损坏返回默认值且不抛异常", "[config]") {
    auto file = temp_file();
    { std::ofstream f(file, std::ios::binary); f << "{ not valid json"; }
    ConfigStore store(file);
    auto c = store.load();
    REQUIRE(c.mode == "independent");
}
