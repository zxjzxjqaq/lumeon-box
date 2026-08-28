#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include "core/organizer.hpp"

using namespace lb;
namespace fs = std::filesystem;

struct Desk {
    fs::path dir;
    fs::path jrPath;
    Desk() {
        dir = fs::temp_directory_path() / L"lumenbox_desk_test";
        jrPath = fs::temp_directory_path() / L"lumenbox_tests" / L"organizer.jsonl";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::remove(jrPath, ec);
        fs::create_directories(dir);
    }
    ~Desk() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

TEST_CASE("一键整理：分类移动 + 跳过项", "[organizer]") {
    Desk d;
    Journal jr(d.jrPath.wstring());
    RuleSet rules;
    Organizer org(d.dir.wstring(), rules, jr);

    std::ofstream(d.dir / L"a.pdf") << 'x';
    std::ofstream(d.dir / L"b.PNG") << 'x';
    std::ofstream(d.dir / L"c.txt") << 'x';
    std::ofstream(d.dir / L"weird.xyz") << 'x';
    std::ofstream(d.dir / L"link.lnk") << 'x';
    std::ofstream(d.dir / L"~$tmp.docx") << 'x';
    fs::create_directory(d.dir / L"subdir");

    auto summary = org.OrganizeAll();
    REQUIRE(summary.moved == 4);  // a.pdf b.PNG c.txt weird.xyz
    REQUIRE(fs::exists(d.dir / L"LumenBox" / L"文档" / L"a.pdf"));
    REQUIRE(fs::exists(d.dir / L"LumenBox" / L"图片" / L"b.PNG"));
    REQUIRE(fs::exists(d.dir / L"LumenBox" / L"文档" / L"c.txt"));
    REQUIRE(fs::exists(d.dir / L"LumenBox" / L"其他" / L"weird.xyz"));
    REQUIRE(fs::exists(d.dir / L"link.lnk"));     // 快捷方式不动
    REQUIRE(fs::exists(d.dir / L"~$tmp.docx"));   // 临时文件不动
    REQUIRE(fs::exists(d.dir / L"subdir"));       // 目录不动
    REQUIRE(org.FilesIn(Category::Doc).size() == 2);
}

TEST_CASE("目标重名自动改名，永不覆盖", "[organizer]") {
    Desk d;
    Journal jr(d.jrPath.wstring());
    RuleSet rules;
    Organizer org(d.dir.wstring(), rules, jr);

    std::ofstream(d.dir / L"a.pdf") << '1';
    org.OrganizeAll();
    std::ofstream(d.dir / L"a.pdf") << '2';  // 同名文件再落桌
    org.OrganizeAll();

    REQUIRE(fs::exists(d.dir / L"LumenBox" / L"文档" / L"a.pdf"));
    REQUIRE(fs::exists(d.dir / L"LumenBox" / L"文档" / L"a (2).pdf"));
    REQUIRE_FALSE(fs::exists(d.dir / L"a.pdf"));
}

TEST_CASE("撤销最后一批恢复原状", "[organizer]") {
    Desk d;
    Journal jr(d.jrPath.wstring());
    RuleSet rules;
    Organizer org(d.dir.wstring(), rules, jr);

    std::ofstream(d.dir / L"a.pdf") << 'x';
    std::ofstream(d.dir / L"b.png") << 'x';
    org.OrganizeAll();
    REQUIRE(org.FilesIn(Category::Doc).size() == 1);

    REQUIRE(org.UndoLast());
    REQUIRE(fs::exists(d.dir / L"a.pdf"));
    REQUIRE(fs::exists(d.dir / L"b.png"));
    REQUIRE(org.FilesIn(Category::Doc).empty());
    REQUIRE_FALSE(org.UndoLast());  // 空日志再撤 = false
}

TEST_CASE("拖拽归位与原位被占的撤销", "[organizer]") {
    Desk d;
    Journal jr(d.jrPath.wstring());
    RuleSet rules;
    Organizer org(d.dir.wstring(), rules, jr);

    std::ofstream(d.dir / L"x.txt") << 'x';
    REQUIRE(org.MoveToCategory(d.dir / L"x.txt", Category::Doc));
    REQUIRE(fs::exists(d.dir / L"LumenBox" / L"文档" / L"x.txt"));

    std::ofstream(d.dir / L"x.txt") << 'y';  // 原位重建同名文件
    REQUIRE(org.UndoLast());                 // 原位被占 → 放回为唯一名，不覆盖
    REQUIRE(fs::exists(d.dir / L"x.txt"));
    REQUIRE(fs::exists(d.dir / L"x (2).txt"));
}

TEST_CASE("隐藏文件不整理", "[organizer]") {
    Desk d;
    Journal jr(d.jrPath.wstring());
    RuleSet rules;
    Organizer org(d.dir.wstring(), rules, jr);

    fs::path hidden = d.dir / L"hid.pdf";
    { std::ofstream o(hidden); o << 'x'; }
    SetFileAttributesW(hidden.c_str(), FILE_ATTRIBUTE_HIDDEN);

    auto summary = org.OrganizeAll();
    REQUIRE(summary.moved == 0);
    REQUIRE(fs::exists(hidden));

    SetFileAttributesW(hidden.c_str(), FILE_ATTRIBUTE_NORMAL);  // 便于清理
}
