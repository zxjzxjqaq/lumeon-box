#include <catch2/catch_test_macros.hpp>
#include "core/classifier.hpp"

using namespace lb;

TEST_CASE("文件分类", "[classifier]") {
    RuleSet rs;
    REQUIRE(ClassifyFile(L"C:\\d\\report.PDF", rs) == Category::Doc);   // 大写扩展名
    REQUIRE(ClassifyFile(L"C:\\d\\photo.jpg", rs) == Category::Image);
    REQUIRE(ClassifyFile(L"C:\\d\\clip.mkv", rs) == Category::Video);
    REQUIRE(ClassifyFile(L"C:\\d\\setup.msi", rs) == Category::Installer);
    REQUIRE(ClassifyFile(L"C:\\d\\noext", rs) == Category::Other);      // 无扩展名
    REQUIRE(ClassifyFile(L"C:\\d\\weird.zzz", rs) == Category::Other);  // 未收录
    REQUIRE(ClassifyFile(L"C:\\d.a\\readme", rs) == Category::Other);   // 点在目录名里
}
