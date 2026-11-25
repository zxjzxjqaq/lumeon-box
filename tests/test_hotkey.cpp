#include <catch2/catch_test_macros.hpp>
#include "core/hotkey.hpp"

using namespace lb;

TEST_CASE("hotkey parse basic", "[hotkey]") {
    UINT mods = 0, vk = 0;
    REQUIRE(ParseHotkey("Ctrl+B", mods, vk));
    REQUIRE(mods == MOD_CONTROL);
    REQUIRE(vk == 'B');
    REQUIRE(ParseHotkey("ctrl+alt+X", mods, vk));
    REQUIRE(mods == (MOD_CONTROL | MOD_ALT));
    REQUIRE(vk == 'X');
    REQUIRE(ParseHotkey("Shift+F5", mods, vk));
    REQUIRE(mods == MOD_SHIFT);
    REQUIRE(vk == VK_F5);
    REQUIRE(ParseHotkey("Ctrl+Space", mods, vk));
    REQUIRE(vk == VK_SPACE);
}

TEST_CASE("hotkey parse guards", "[hotkey]") {
    UINT mods = 0, vk = 0;
    REQUIRE_FALSE(ParseHotkey("", mods, vk));
    REQUIRE_FALSE(ParseHotkey("Ctrl+", mods, vk));
    REQUIRE_FALSE(ParseHotkey("Ctrl+F13", mods, vk));
    REQUIRE_FALSE(ParseHotkey("Ctrl+Nope", mods, vk));
    REQUIRE(ParseHotkey("B", mods, vk));  // 裸键补 Ctrl
    REQUIRE(mods == MOD_CONTROL);
}

TEST_CASE("hotkey format roundtrip", "[hotkey]") {
    UINT mods = 0, vk = 0;
    REQUIRE(ParseHotkey("Ctrl+Alt+K", mods, vk));
    REQUIRE(FormatHotkey(mods, vk) == "Ctrl+Alt+K");
    REQUIRE(FormatHotkey(MOD_CONTROL | MOD_SHIFT, VK_F12) == "Ctrl+Shift+F12");
    REQUIRE(FormatHotkey(MOD_CONTROL, VK_SPACE) == "Ctrl+Space");
    REQUIRE(FormatHotkey(MOD_CONTROL, 0x07).empty());  // 未支持键 → 空串
}