#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include "core/organizer.hpp"
#include "core/strutil.hpp"

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

TEST_CASE("容器可收纳文件夹：拖入/枚举/放回", "[organizer]") {
    Desk d;
    Journal jr(d.jrPath.wstring());
    RuleSet rules;
    Organizer org(d.dir.wstring(), (d.dir / L"LumenBox").wstring(), rules, jr);

    // 桌面上一个待收纳的文件夹（内含文件）
    fs::path folder = d.dir / L"项目资料";
    fs::create_directory(folder);
    { std::ofstream o(folder / L"a.txt"); o << 'x'; }

    // 拖进容器 box_b1 → 整个文件夹移入
    REQUIRE(org.MoveToFolder(folder.wstring(), L"box_b1"));
    fs::path inbox = d.dir / L"LumenBox" / L"box_b1" / L"项目资料";
    REQUIRE(fs::exists(inbox));
    REQUIRE(fs::exists(inbox / L"a.txt"));
    REQUIRE_FALSE(fs::exists(folder));  // 原位已移走

    // FilesInFolder 现在包含该文件夹
    auto list = org.FilesInFolder(L"box_b1");
    bool hasFolder = false;
    for (auto& p : list) if (fs::path(p).filename() == L"项目资料") { hasFolder = true; break; }
    REQUIRE(hasFolder);

    // 放回桌面（文件夹整体还原）
    REQUIRE(org.MoveBackToDesktop(inbox.wstring()));
    REQUIRE(fs::exists(d.dir / L"项目资料"));
    REQUIRE(fs::exists(d.dir / L"项目资料" / L"a.txt"));
    REQUIRE_FALSE(fs::exists(inbox));
}

// 找出与 temp 所在卷不同的另一个卷根；没有则返回空（测试跳过）
static fs::path alternate_volume() {
    fs::path tmp = fs::temp_directory_path();
    std::wstring tr = tmp.root_path().wstring();
    wchar_t tmpRoot = tr.empty() ? 0 : tr[0];
    for (wchar_t d = L'C'; d <= L'Z'; ++d) {
        if (d == tmpRoot) continue;
        fs::path root = fs::path(std::wstring(1, d) + L":\\");
        std::error_code ec;
        if (fs::is_directory(root, ec)) return root;
    }
    return {};
}

TEST_CASE("MovePath: 目录跨卷递归复制+删除源", "[organizer]") {
    fs::path other = alternate_volume();
    if (other.empty()) { SUCCEED("无第二卷，跳过跨卷测试"); return; }
    fs::path srcRoot = other / L"lumenbox_xvol_src";
    fs::path dst = fs::temp_directory_path() / L"lumenbox_xvol_dst";
    std::error_code ec;
    fs::remove_all(srcRoot, ec);
    fs::remove_all(dst, ec);
    if (!fs::create_directories(srcRoot / L"sub", ec)) { SUCCEED("第二卷根不可写，跳过"); return; }
    { std::ofstream o(srcRoot / L"root.txt"); o << 'r'; }
    { std::ofstream o(srcRoot / L"sub" / L"nested.txt"); o << 'n'; }

    REQUIRE(MovePath(srcRoot.wstring(), dst.wstring()));
    REQUIRE(fs::exists(dst / L"root.txt"));
    REQUIRE(fs::exists(dst / L"sub" / L"nested.txt"));
    REQUIRE_FALSE(fs::exists(srcRoot));  // 源已删除

    fs::remove_all(dst, ec);
}

TEST_CASE("DisplayFileLess: 文件夹优先→按格式→同格式按名称", "[strutil]") {
    auto d = fs::temp_directory_path() / L"lumenbox_sort";
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    fs::create_directories(d / L"Z文件夹", ec);
    fs::create_directories(d / L"A文件夹", ec);
    // 文件路径：is_directory 对不存在的路径返回 false（视为文件），仍可按扩展名排序
    std::wstring zdir = (d / L"Z文件夹").wstring();
    std::wstring adir = (d / L"A文件夹").wstring();
    std::wstring apple_txt = (d / L"apple.txt").wstring();
    std::wstring cherry_txt = (d / L"cherry.txt").wstring();
    std::wstring alpha_pdf = (d / L"alpha.pdf").wstring();
    std::wstring banana_pdf = (d / L"banana.pdf").wstring();

    // 文件夹优先于文件
    REQUIRE(DisplayFileLess(zdir, apple_txt));
    REQUIRE_FALSE(DisplayFileLess(apple_txt, zdir));
    // 文件夹之间按名称（不区分大小写）
    REQUIRE(DisplayFileLess(adir, zdir));
    REQUIRE_FALSE(DisplayFileLess(zdir, adir));
    // 文件：按格式(扩展名) → pdf 组在 txt 组之前（p < t）
    REQUIRE(DisplayFileLess(banana_pdf, apple_txt));
    REQUIRE_FALSE(DisplayFileLess(apple_txt, banana_pdf));
    // 同格式按名称
    REQUIRE(DisplayFileLess(alpha_pdf, banana_pdf));   // alpha < banana
    REQUIRE(DisplayFileLess(apple_txt, cherry_txt));   // apple < cherry
    REQUIRE_FALSE(DisplayFileLess(cherry_txt, apple_txt));

    // 整组排序断言
    std::vector<std::wstring> v = {apple_txt, zdir, banana_pdf, cherry_txt, alpha_pdf, adir};
    std::sort(v.begin(), v.end(), DisplayFileLess);
    REQUIRE(v[0] == adir);       // 文件夹 A 先
    REQUIRE(v[1] == zdir);       // 文件夹 Z 次
    REQUIRE(v[2] == alpha_pdf); // pdf 组按名称
    REQUIRE(v[3] == banana_pdf);
    REQUIRE(v[4] == apple_txt); // txt 组按名称
    REQUIRE(v[5] == cherry_txt);

    fs::remove_all(d, ec);
}

