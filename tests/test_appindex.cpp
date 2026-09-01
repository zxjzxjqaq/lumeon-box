// tests/test_appindex.cpp —— 应用索引：搜索过滤/排序（纯函数）+ 去重
#include <catch2/catch_test_macros.hpp>
#include "core/appindex.hpp"

using namespace lb;

TEST_CASE("search: 大小写不敏感子串命中 name 或文件名", "[appindex]") {
    std::vector<AppItem> items{{L"记事本", L"C:\\Windows\\notepad.exe"},
                               {L"计算器", L"C:\\Windows\\System32\\calc.exe"},
                               {L"Word", L"C:\\Program Files\\word.exe"}};
    REQUIRE(SearchItems(items, L"notep").size() == 1);
    REQUIRE(SearchItems(items, L"CALC").size() == 1);           // 命中文件名
    REQUIRE(SearchItems(items, L"word").size() == 1);           // 命中 name
    REQUIRE(SearchItems(items, L"zzz").empty());
}

TEST_CASE("search: 前缀命中排在子串命中之前", "[appindex]") {
    std::vector<AppItem> items{{L"blender", L"C:\\x\\blender.exe"},
                               {L"Notepad++", L"C:\\x\\notepadpp.exe"},
                               {L"notepad", L"C:\\x\\notepad.exe"}};
    auto r = SearchItems(items, L"notepad");
    REQUIRE(r.size() == 2);                       // notepad 与 Notepad++
    REQUIRE(r[0].name == L"notepad");             // 前缀命中优先
    REQUIRE(r[1].name == L"Notepad++");           // 子串（不区分大小写）
}

TEST_CASE("search: 空查询返回全部且按显示名排序", "[appindex]") {
    std::vector<AppItem> items{{L"b", L"C:\\x\\b.exe"}, {L"a", L"C:\\x\\a.exe"}};
    auto r = SearchItems(items, L"");
    REQUIRE(r.size() == 2);
    REQUIRE(r[0].name == L"a");
    REQUIRE(r[1].name == L"b");
}

TEST_CASE("addunique: 按路径去重（大小写不敏感）且首个优先", "[appindex]") {
    std::vector<AppItem> items;
    AddUniqueItem(items, {L"a", L"C:\\X\\A.exe"});
    AddUniqueItem(items, {L"b", L"c:\\x\\a.EXE"});   // 重复路径（不同大小写）
    AddUniqueItem(items, {L"c", L"C:\\X\\B.exe"});
    REQUIRE(items.size() == 2);
    REQUIRE(items[0].name == L"a");
}

TEST_CASE("addunique: 空路径忽略", "[appindex]") {
    std::vector<AppItem> items;
    AddUniqueItem(items, {L"x", L""});
    REQUIRE(items.empty());
}
