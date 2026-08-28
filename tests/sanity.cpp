// tests/sanity.cpp —— 工具链自检：能跑即测试底座可用
#include <catch2/catch_test_macros.hpp>

TEST_CASE("测试底座自检", "[sanity]") {
    REQUIRE(1 + 1 == 2);
}
