#pragma once
#include <windows.h>
#include <string>
#include "core/rules.hpp"

namespace lb {

// 分类规则设置窗口（无模式）：展示生效规则（默认/自定义）与永不整理白名单，
// 支持增删。规则变更即时落盘 rules.json 并即时生效。
class RulesDialog {
public:
    void Toggle(RuleSet& rules, const std::wstring& rulesFile);
    bool Visible() const { return hwnd_ != nullptr; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void BuildControls(HWND hwnd);
    void RebuildLists();
    void Save();

    HWND hwnd_ = nullptr;
    HWND list_ = nullptr, extEdit_ = nullptr, catCombo_ = nullptr;
    HWND neverEdit_ = nullptr, neverList_ = nullptr;
    RuleSet* rules_ = nullptr;
    std::wstring file_;
    HBRUSH bg_ = nullptr;
};

}  // namespace lb
