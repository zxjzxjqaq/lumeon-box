#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include "core/autostart.hpp"

using namespace lb;

// 用临时值名测试，避免污染真实开机启动项
namespace {
const wchar_t* kTestValue = L"LumenBoxTest";
}

TEST_CASE("自启开关：设置/查询/移除往返", "[autostart]") {
    Autostart a(kTestValue);
    a.Disable();  // 清理上次运行的残留（失败中断时可能留下）

    REQUIRE_FALSE(a.IsEnabled());

    REQUIRE(a.Enable());
    REQUIRE(a.IsEnabled());
    REQUIRE_FALSE(a.Path().empty());  // 有路径内容

    REQUIRE(a.Disable());
    REQUIRE_FALSE(a.IsEnabled());
    REQUIRE_FALSE(a.Disable());  // 再删 = false（幂等无害）
}

TEST_CASE("自启路径指向当前 exe", "[autostart]") {
    Autostart a(kTestValue);
    a.Disable();  // 清理上次运行的残留
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    REQUIRE(a.Enable());
    Autostart b(kTestValue);  // 新实例读取注册表
    REQUIRE(b.Path() == (L"\"" + std::wstring(exe) + L"\""));  // 带引号注册
    REQUIRE(b.Disable());
    REQUIRE(b.Path().empty());
}
