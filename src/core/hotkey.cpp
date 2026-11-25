#include "core/hotkey.hpp"
#include <algorithm>
#include <cctype>
#include <vector>

namespace lb {
namespace {

std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t b = 0;
    for (;;) {
        size_t e = s.find(sep, b);
        out.push_back(s.substr(b, e == std::string::npos ? e : e - b));
        if (e == std::string::npos) break;
        b = e + 1;
    }
    return out;
}

std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

}  // namespace

bool ParseHotkey(const std::string& text, UINT& mods, UINT& vk) {
    mods = 0;
    vk = 0;
    auto parts = Split(text, '+');
    std::string key;
    for (auto& p : parts) {
        std::string t = Lower(Trim(p));
        if (t.empty()) continue;
        if (t == "ctrl" || t == "control") mods |= MOD_CONTROL;
        else if (t == "alt") mods |= MOD_ALT;
        else if (t == "shift") mods |= MOD_SHIFT;
        else if (t == "win") mods |= MOD_WIN;
        else key = t;
    }
    if (key.empty()) return false;
    if (key == "space") vk = VK_SPACE;
    else if (key == "tab") vk = VK_TAB;
    else if (key.size() == 1 && ((key[0] >= 'a' && key[0] <= 'z') ||
                                 (key[0] >= '0' && key[0] <= '9')))
        vk = (UINT)(unsigned char)std::toupper((unsigned char)key[0]);
    else if (key.size() >= 2 && key[0] == 'f' &&
             std::all_of(key.begin() + 1, key.end(),
                         [](char c) { return c >= '0' && c <= '9'; })) {
        int n = std::atoi(key.c_str() + 1);
        if (n < 1 || n > 12) return false;
        vk = VK_F1 + (n - 1);
    } else return false;
    if (mods == 0) mods = MOD_CONTROL;  // 裸键不允许：至少一个修饰键，避免抢占普通输入
    return true;
}

std::string FormatHotkey(UINT mods, UINT vk) {
    std::string s;
    if (mods & MOD_CONTROL) s += "Ctrl+";
    if (mods & MOD_ALT) s += "Alt+";
    if (mods & MOD_SHIFT) s += "Shift+";
    if (mods & MOD_WIN) s += "Win+";
    if (vk == VK_SPACE) s += "Space";
    else if (vk == VK_TAB) s += "Tab";
    else if (vk >= VK_F1 && vk <= VK_F12) s += "F" + std::to_string(vk - VK_F1 + 1);
    else if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) s += (char)vk;
    else return "";
    return s;
}

}  // namespace lb