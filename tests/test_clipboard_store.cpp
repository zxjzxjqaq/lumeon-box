#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include "core/clipboard_store.hpp"

using namespace lb;
namespace fs = std::filesystem;

// 每个用例独立子目录，避免互相污染
static std::wstring Dir(const char* tag) {
    auto d = fs::temp_directory_path() / L"lumenbox_tests" / L"clipstore" / tag;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d.wstring();
}

static ClipEntry MakeText(const std::string& id, const std::wstring& body,
                          long long ts = 100) {
    ClipEntry e;
    e.id = id;
    e.ts = ts;
    e.type = "text";
    e.text = body;
    e.textInline = true;
    ClipPayload p;
    p.fmt = "text";
    p.text = "x";
    p.size = 4;
    e.payloads.push_back(p);
    e.hash = ClipHash(body.empty() ? id : std::string("h:") + id);
    return e;
}

TEST_CASE("FNV 哈希稳定且区分内容", "[clipstore]") {
    uint64_t a = ClipHash(std::string("hello"));
    uint64_t b = ClipHash(std::string("hello"));
    uint64_t c = ClipHash(std::string("hellp"));
    REQUIRE(a == b);
    REQUIRE(a != c);
    REQUIRE(ClipHash(std::string("")) == 1469598103934665603ull);  // 空串 = FNV 偏移基
}

TEST_CASE("格式优先级：files > image > html > text", "[clipstore]") {
    REQUIRE(ClipTypeFromFormats({"text"}) == "text");
    REQUIRE(ClipTypeFromFormats({"html", "text"}) == "text");  // html 归一为文本
    REQUIRE(ClipTypeFromFormats({"png", "text"}) == "image");
    REQUIRE(ClipTypeFromFormats({"hdrop", "text"}) == "text");  // 不再产生 files 类型
    REQUIRE(ClipTypeFromFormats({"png", "html", "text"}) == "image");
    REQUIRE(ClipTypeFromFormats({}) == "text");
    REQUIRE(ClipTypeFromFormats({"weird"}) == "text");
}

TEST_CASE("预览生成：两行、截断、空安全", "[clipstore]") {
    REQUIRE(MakePreview(L"") == L"");
    REQUIRE(MakePreview(L"第一行\n第二行\n第三行") == L"第一行 第二行");
    std::wstring long1(100, L'A');
    REQUIRE(MakePreview(long1).size() == 100);  // ≤120 不截断
    std::wstring many(200, L'B');
    REQUIRE(MakePreview(many).size() == 121);   // 120 + …
    std::wstring two = std::wstring(80, L'A') + L"\n" + std::wstring(80, L'B');
    REQUIRE(MakePreview(two).size() == 121);    // 两行拼接超限 → 120 + …
}

TEST_CASE("去重：同哈希移顶刷新，不产生重复", "[clipstore]") {
    ClipStore s(Dir("dedupe"));
    auto a = MakeText("a", L"alpha", 100);
    auto b = MakeText("b", L"beta", 101);
    REQUIRE(s.Prepend(a, 100, 1ull << 30));
    REQUIRE(s.Prepend(b, 100, 1ull << 30));
    REQUIRE(s.Count() == 2);
    REQUIRE(s.Snapshot()[0].id == "b");  // 新的在前

    auto b2 = MakeText("b2", L"beta", 200);  // 同内容（同哈希）新事件
    b2.hash = b.hash;
    REQUIRE_FALSE(s.Prepend(b2, 100, 1ull << 30));  // 未新增
    REQUIRE(s.Count() == 2);
    REQUIRE(s.Snapshot()[0].id == "b");       // 原条目被移顶/刷新
    REQUIRE(s.Snapshot()[0].ts == 200);       // 时间戳已刷新
}

TEST_CASE("条数淘汰：LRU 从最旧非置顶开始，置顶豁免", "[clipstore]") {
    ClipStore s(Dir("count"));
    for (int i = 0; i < 5; ++i)
        s.Prepend(MakeText("e" + std::to_string(i), L"x", 100 + i), 3, 1ull << 30);
    REQUIRE(s.Count() == 3);
    REQUIRE(s.Snapshot()[0].id == "e4");  // 最旧的 e0/e1 被淘汰
    REQUIRE(s.Snapshot()[2].id == "e2");

    // 置顶不淘汰
    REQUIRE(s.SetPinned("e2", true));
    s.Prepend(MakeText("e5", L"x", 200), 3, 1ull << 30);
    s.Prepend(MakeText("e6", L"x", 201), 3, 1ull << 30);
    REQUIRE(s.Count() == 3);  // 3 条上限，e2 因置顶豁免而挤掉的是 e4/e3
    bool hasE2 = false;
    for (auto& e : s.Snapshot())
        if (e.id == "e2") hasE2 = true;
    REQUIRE(hasE2);
}

TEST_CASE("字节淘汰：载荷总量超限触发", "[clipstore]") {
    ClipStore s(Dir("bytes"));
    for (int i = 0; i < 4; ++i) {
        ClipEntry e = MakeText("z" + std::to_string(i), L"x", 100 + i);
        e.payloads[0].size = 100;  // 每条 100 字节
        s.Prepend(e, 50, 250);     // 上限 250 字节 → 只能留 2 条
    }
    REQUIRE(s.Count() == 2);
    REQUIRE(s.Snapshot()[0].id == "z3");
    REQUIRE(s.Snapshot()[1].id == "z2");
}

TEST_CASE("置顶上限 10", "[clipstore]") {
    ClipStore s(Dir("pincap"));
    for (int i = 0; i < 12; ++i)
        s.Prepend(MakeText("p" + std::to_string(i), L"x", 100 + i), 50, 1ull << 30);
    for (int i = 0; i < 10; ++i)
        REQUIRE(s.SetPinned("p" + std::to_string(i), true));
    REQUIRE_FALSE(s.SetPinned("p10", true));  // 第 11 个置顶被拒
    REQUIRE(s.SetPinned("p0", false));        // 取消后可再置顶
    REQUIRE(s.SetPinned("p10", true));
}

TEST_CASE("Save/Load 回读；损坏行跳过", "[clipstore]") {
    auto dir = Dir("roundtrip");
    {
        ClipStore s(dir);
        auto a = MakeText("a", L"中文内容\n第二行", 100);
        a.source = L"WINWORD.EXE";
        ClipPayload html;
        html.fmt = "html";
        html.file = L"a.html";
        html.size = 20;
        a.payloads.push_back(html);
        s.Prepend(a, 50, 1ull << 30);
        s.Prepend(MakeText("b", L"beta", 101), 50, 1ull << 30);
        REQUIRE(s.SetPinned("a", true));
        REQUIRE(s.Save());
    }
    // 手工塞一行坏数据
    {
        std::ofstream f(dir + L"\\index.jsonl", std::ios::app | std::ios::binary);
        f << "{broken-json\n";
    }
    ClipStore s2(dir);
    s2.Load();
    REQUIRE(s2.Count() == 2);
    auto items = s2.Snapshot();
    REQUIRE(items[0].id == "a");  // 置顶在前
    REQUIRE(items[0].pinned);
    REQUIRE(items[0].text == L"中文内容\n第二行");
    REQUIRE(items[0].source == L"WINWORD.EXE");
    REQUIRE(items[0].payloads.size() == 2);
    REQUIRE(items[0].payloads[1].fmt == "html");
    REQUIRE(items[1].id == "b");
}

TEST_CASE("blob 写入/读取；缺失容错", "[clipstore]") {
    auto dir = Dir("blob");
    ClipStore s(dir);
    auto e = MakeText("k", L"key", 100);
    ClipPayload p;
    p.fmt = "png";
    s.Prepend(e, 50, 1ull << 30);
    auto items = s.Snapshot();
    REQUIRE(items.size() == 1);
    ClipPayload w = p;
    REQUIRE(s.WriteBlob(items[0], w, std::string("PNGDATA")));
    REQUIRE(w.file == L"k.png");

    // 带 blob 引用的条目可读回
    ClipEntry withBlob = items[0];
    withBlob.payloads.clear();
    ClipPayload ref;
    ref.fmt = "png";
    ref.file = L"k.png";
    ref.size = 7;
    withBlob.payloads.push_back(ref);
    std::string out;
    REQUIRE(s.LoadPayload(withBlob, "png", out));
    REQUIRE(out == "PNGDATA");

    // blob 引用指向不存在的文件 → 静默失败（缺失容错）
    ClipEntry missing = withBlob;
    missing.payloads[0].file = L"ghost.png";
    std::string out2;
    REQUIRE_FALSE(s.LoadPayload(missing, "png", out2));

    // 内联载荷直接返回 text
    ClipEntry inl = items[0];
    REQUIRE(s.LoadPayload(inl, "text", out));
    REQUIRE(out == "x");
    REQUIRE_FALSE(s.LoadPayload(inl, "html", out));  // 无此格式
}

TEST_CASE("Remove / Clear：删文件、保留置顶", "[clipstore]") {
    auto dir = Dir("remove");
    ClipStore s(dir);
    auto a = MakeText("a", L"alpha", 100);
    ClipPayload p;
    p.fmt = "text";
    s.Prepend(a, 50, 1ull << 30);
    auto items = s.Snapshot();
    ClipPayload w = p;
    REQUIRE(s.WriteBlob(items[0], w, std::string("DATA")));
    REQUIRE(s.Remove("a"));
    REQUIRE_FALSE(s.Remove("nope"));
    std::error_code ec;
    REQUIRE_FALSE(fs::exists(dir + L"\\blobs\\a.txt", ec));  // blob 已删

    s.Prepend(MakeText("x", L"x", 101), 50, 1ull << 30);
    s.Prepend(MakeText("y", L"y", 102), 50, 1ull << 30);
    REQUIRE(s.SetPinned("x", true));
    REQUIRE(s.Clear());
    REQUIRE(s.Count() == 1);
    REQUIRE(s.Snapshot()[0].id == "x");  // 置顶保留
    REQUIRE_FALSE(s.Clear());            // 只剩置顶 = 无可清（返回 false）
}
