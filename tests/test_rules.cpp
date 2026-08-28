#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include "core/rules.hpp"

using namespace lb;
namespace fs = std::filesystem;

TEST_CASE("默认规则表", "[rules]") {
    RuleSet rs;
    REQUIRE(rs.Match(L"pdf") == Category::Doc);
    REQUIRE(rs.Match(L"docx") == Category::Doc);
    REQUIRE(rs.Match(L"png") == Category::Image);
    REQUIRE(rs.Match(L"mp4") == Category::Video);
    REQUIRE(rs.Match(L"mp3") == Category::Audio);
    REQUIRE(rs.Match(L"zip") == Category::Archive);
    REQUIRE(rs.Match(L"exe") == Category::Installer);
    REQUIRE(rs.Match(L"apk") == Category::Installer);   // 桌面上的 apk 属安装包
    REQUIRE(rs.Match(L"appx") == Category::Installer);
    REQUIRE(rs.Match(L"xyz123") == Category::None);
}

TEST_CASE("自定义规则覆盖默认并持久化", "[rules]") {
    auto file = fs::temp_directory_path() / L"lumenbox_tests" / L"rules.json";
    std::error_code ec;
    fs::remove(file, ec);
    RuleSet rs;
    rs.SetCustom(L"pdf", Category::Other);  // 覆盖默认
    rs.SetCustom(L"psd", Category::Image);  // 新增
    rs.Save(file);
    auto loaded = RuleSet::Load(file);
    REQUIRE(loaded.Match(L"pdf") == Category::Other);
    REQUIRE(loaded.Match(L"psd") == Category::Image);
    REQUIRE(loaded.Match(L"png") == Category::Image);  // 默认仍在
}

TEST_CASE("损坏的 rules.json 回默认", "[rules]") {
    auto file = fs::temp_directory_path() / L"lumenbox_tests" / L"rules_bad.json";
    { std::ofstream o(file, std::ios::binary); o << "{oops"; }
    auto loaded = RuleSet::Load(file);
    REQUIRE(loaded.Match(L"pdf") == Category::Doc);  // 默认表完好
    REQUIRE(loaded.Match(L"psd") == Category::None); // 自定义丢失
}

TEST_CASE("类目中文名与键往返", "[rules]") {
    REQUIRE(CategoryName(Category::Doc) == L"文档");
    REQUIRE(CategoryName(Category::Other) == L"其他");
    for (Category c : {Category::Doc, Category::Image, Category::Video, Category::Audio,
                       Category::Archive, Category::Installer, Category::Other}) {
        REQUIRE(CategoryFromKey(CategoryKey(c)) == c);
    }
}
