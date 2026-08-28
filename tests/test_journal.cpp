#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include "core/journal.hpp"

using namespace lb;
namespace fs = std::filesystem;

static fs::path J() {
    auto d = fs::temp_directory_path() / L"lumenbox_tests";
    fs::create_directories(d);
    return d / L"journal.jsonl";
}

TEST_CASE("批次记录与分组", "[journal]") {
    auto f = J();
    std::error_code ec;
    fs::remove(f, ec);
    Journal jr(f.wstring());
    jr.BeginBatch();
    jr.LogMove({L"C:\\a.txt", L"C:\\b\\a.txt"});
    jr.LogMove({L"C:\\b.txt", L"C:\\b\\b.txt"});
    jr.BeginBatch();
    jr.LogMove({L"C:\\c.txt", L"C:\\o\\c.txt"});
    auto batches = jr.Batches();
    REQUIRE(batches.size() == 2);
    REQUIRE(batches[0].size() == 2);
    REQUIRE(batches[1].size() == 1);
    REQUIRE(batches[0][0].dst == L"C:\\b\\a.txt");
    REQUIRE(batches[1][0].src == L"C:\\c.txt");
}

TEST_CASE("损坏行被跳过；无批次头的 move 忽略", "[journal]") {
    auto f = J();
    std::error_code ec;
    fs::remove(f, ec);
    { std::ofstream o(f, std::ios::binary); o << "{broken\n"; }  // 无任何合法批次头
    Journal jr(f.wstring());
    jr.LogMove({L"x", L"y"});   // 无批次头的 move 被忽略
    REQUIRE(jr.Batches().empty());
}

TEST_CASE("RemoveLastBatch 截断最后一批", "[journal]") {
    auto f = J();
    std::error_code ec;
    fs::remove(f, ec);
    Journal jr(f.wstring());
    jr.BeginBatch();
    jr.LogMove({L"a", L"b"});
    jr.BeginBatch();
    jr.LogMove({L"c", L"d"});
    REQUIRE(jr.RemoveLastBatch());
    auto b = jr.Batches();
    REQUIRE(b.size() == 1);
    REQUIRE(b[0][0].src == L"a");
    REQUIRE(jr.RemoveLastBatch());
    REQUIRE(jr.Batches().empty());
    REQUIRE_FALSE(jr.RemoveLastBatch());  // 空日志再撤 = false
}
