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
    Organizer org(d.dir.wstring(), (d.dir / L"LumenBox").wstring(), rules, jr);

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
    Organizer org(d.dir.wstring(), (d.dir / L"LumenBox").wstring(), rules, jr);

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
    Organizer org(d.dir.wstring(), (d.dir / L"LumenBox").wstring(), rules, jr);

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
    Organizer org(d.dir.wstring(), (d.dir / L"LumenBox").wstring(), rules, jr);

    std::ofstream(d.dir / L"x.txt") << 'x';
    REQUIRE(org.MoveToCategory(d.dir / L"x.txt", Category::Doc));
    REQUIRE(fs::exists(d.dir / L"LumenBox" / L"文档" / L"x.txt"));

    std::ofstream(d.dir / L"x.txt") << 'y';  // 原位重建同名文件
    REQUIRE(org.UndoLast());                 // 原位被占 → 放回为唯一名，不覆盖
    REQUIRE(fs::exists(d.dir / L"x.txt"));
    REQUIRE(fs::exists(d.dir / L"x (2).txt"));
}

TEST_CASE("拖到自身所在盒 → 不动、不产生副本", "[organizer]") {
    Desk d;
    Journal jr(d.jrPath.wstring());
    RuleSet rules;
    Organizer org(d.dir.wstring(), (d.dir / L"LumenBox").wstring(), rules, jr);

    std::ofstream(d.dir / L"x.txt") << 'x';
    REQUIRE(org.MoveToCategory(d.dir / L"x.txt", Category::Doc));
    fs::path in = d.dir / L"LumenBox" / L"文档" / L"x.txt";
    REQUIRE(fs::exists(in));
    REQUIRE_FALSE(fs::exists(d.dir / L"LumenBox" / L"文档" / L"x (2).txt"));

    // 拖到同一个盒（已是该目录）→ 应被拒绝，无副本、原文件还在
    REQUIRE_FALSE(org.MoveToCategory(in.wstring(), Category::Doc));
    REQUIRE(fs::exists(in));
    REQUIRE_FALSE(fs::exists(d.dir / L"LumenBox" / L"文档" / L"x (2).txt"));
    REQUIRE(org.FilesIn(Category::Doc).size() == 1);

    // 拖到另一个盒 → 正常移动
    REQUIRE(org.MoveToCategory(in.wstring(), Category::Image));
    REQUIRE(fs::exists(d.dir / L"LumenBox" / L"图片" / L"x.txt"));
    REQUIRE_FALSE(fs::exists(in));
}

TEST_CASE("放回桌面：从格子移回桌面根，冲突唯一名，可撤销", "[organizer]") {
    Desk d;
    Journal jr(d.jrPath.wstring());
    RuleSet rules;
    Organizer org(d.dir.wstring(), (d.dir / L"LumenBox").wstring(), rules, jr);

    std::ofstream(d.dir / L"y.pdf") << 'x';
    org.OrganizeAll();
    fs::path inbox = d.dir / L"LumenBox" / L"文档" / L"y.pdf";
    REQUIRE(fs::exists(inbox));

    REQUIRE(org.MoveBackToDesktop(inbox.wstring()));
    REQUIRE(fs::exists(d.dir / L"y.pdf"));
    REQUIRE_FALSE(fs::exists(inbox));

    // 再放回一次且桌面已有同名 → 唯一名，永不覆盖
    std::ofstream(d.dir / L"y.pdf") << "again";
    REQUIRE_FALSE(fs::exists(d.dir / L"LumenBox" / L"文档" / L"y.pdf"));
    std::ofstream(d.dir / L"LumenBox" / L"文档" / L"y.pdf") << 'x';
    REQUIRE(org.MoveBackToDesktop((d.dir / L"LumenBox" / L"文档" / L"y.pdf").wstring()));
    REQUIRE(fs::exists(d.dir / L"y (2).pdf"));

    // 放回动作在日志里成批，可撤销
    REQUIRE(org.UndoLast());
    REQUIRE(fs::exists(d.dir / L"LumenBox" / L"文档" / L"y.pdf"));
    REQUIRE_FALSE(fs::exists(d.dir / L"y (2).pdf"));

    // 非格子内文件拒绝放回
    std::ofstream(d.dir / L"z.txt") << 'x';
    REQUIRE_FALSE(org.MoveBackToDesktop((d.dir / L"z.txt").wstring()));
    REQUIRE_FALSE(org.MoveBackToDesktop(L"C:\\不存在的路径\\a.pdf"));
}

TEST_CASE("隐藏文件不整理", "[organizer]") {
    Desk d;
    Journal jr(d.jrPath.wstring());
    RuleSet rules;
    Organizer org(d.dir.wstring(), (d.dir / L"LumenBox").wstring(), rules, jr);

    fs::path hidden = d.dir / L"hid.pdf";
    { std::ofstream o(hidden); o << 'x'; }
    SetFileAttributesW(hidden.c_str(), FILE_ATTRIBUTE_HIDDEN);

    auto summary = org.OrganizeAll();
    REQUIRE(summary.moved == 0);
    REQUIRE(fs::exists(hidden));

    SetFileAttributesW(hidden.c_str(), FILE_ATTRIBUTE_NORMAL);  // 便于清理
}
