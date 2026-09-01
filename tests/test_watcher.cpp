// tests/test_watcher.cpp —— 建议过滤：与 OrganizeAll 一致的跳过语义 + 分类
#include <windows.h>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include "core/watcher.hpp"

using namespace lb;
namespace fs = std::filesystem;

static fs::path temp_root() {
    auto dir = fs::temp_directory_path() / L"lumenbox_tests";
    fs::create_directories(dir);
    return dir;
}

static std::wstring make_file(const fs::path& dir, const wchar_t* name,
                              DWORD attrs = FILE_ATTRIBUTE_NORMAL) {
    fs::path p = dir / name;
    { std::ofstream f(p, std::ios::binary); f << "x"; }
    SetFileAttributesW(p.c_str(), attrs);
    return p.wstring();
}

static std::vector<FileLanding> suggest(const std::vector<std::wstring>& paths,
                                        const RuleSet& rules, const std::wstring& box) {
    return SuggestLandings(paths, rules, box);
}

TEST_CASE("suggest: 按扩展名分类", "[watcher]") {
    auto d = temp_root() / L"ws-classify";
    fs::create_directories(d);
    RuleSet rules;
    auto report = make_file(d, L"report.pdf");
    auto photo = make_file(d, L"photo.png");
    auto landings = suggest({report, photo}, rules, L"");
    REQUIRE(landings.size() == 2);
    REQUIRE(landings[0].cat == Category::Doc);
    REQUIRE(landings[1].cat == Category::Image);
    fs::remove_all(d);
}

TEST_CASE("suggest: 跳过目录", "[watcher]") {
    auto d = temp_root() / L"ws-dir";
    fs::create_directories(d);
    RuleSet rules;
    fs::create_directories(d / L"sub");
    auto f = make_file(d, L"a.txt");
    auto landings = suggest({f, (d / L"sub").wstring()}, rules, L"");
    REQUIRE(landings.size() == 1);
    REQUIRE(landings[0].cat == Category::Doc);
    fs::remove_all(d);
}

TEST_CASE("suggest: 跳过 .lnk / ~$ / 白名单 / 系统隐藏", "[watcher]") {
    auto d = temp_root() / L"ws-skip";
    fs::create_directories(d);
    RuleSet rules;
    rules.AddNever(L"secret.txt");
    auto lnk = make_file(d, L"app.lnk");
    auto tmp = make_file(d, L"~$doc.docx");
    auto secret = make_file(d, L"secret.txt");
    auto normal = make_file(d, L"keep.md");
    auto hidden = make_file(d, L"hide.txt", FILE_ATTRIBUTE_HIDDEN);
    auto landings = suggest({lnk, tmp, secret, normal, hidden}, rules, L"");
    REQUIRE(landings.size() == 1);
    REQUIRE(landings[0].path == normal);
    fs::remove_all(d);
}

TEST_CASE("suggest: 跳过 boxDir 内文件与 LumenBox 同名文件", "[watcher]") {
    auto d = temp_root() / L"ws-box";
    fs::create_directories(d / L"LumenBox" / L"文档");
    RuleSet rules;
    auto outside = make_file(d, L"outside.txt");
    auto inside = make_file(d / L"LumenBox" / L"文档", L"inside.txt");
    auto bad = make_file(d, L"LumenBox");  // 桌面与 box 同名文件（OrganizeAll 也跳过）
    std::wstring box = (d / L"LumenBox").wstring();
    auto landings = suggest({outside, inside, bad}, rules, box);
    REQUIRE(landings.size() == 1);
    REQUIRE(landings[0].path == outside);
    fs::remove_all(d);
}

TEST_CASE("suggest: 未知扩展归为 Other；不存在路径跳过", "[watcher]") {
    auto d = temp_root() / L"ws-misc";
    fs::create_directories(d);
    RuleSet rules;
    auto weird = make_file(d, L"blob.zzz");
    auto gone = (d / L"gone.xyz").wstring();
    auto landings = suggest({weird, gone}, rules, L"");
    REQUIRE(landings.size() == 1);
    REQUIRE(landings[0].cat == Category::Other);
    fs::remove_all(d);
}

TEST_CASE("suggest: 空输入返回空", "[watcher]") {
    RuleSet rules;
    REQUIRE(suggest({}, rules, L"").empty());
}
