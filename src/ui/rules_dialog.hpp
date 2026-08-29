#pragma once
#include <windows.h>
#include <string>
#include "core/rules.hpp"

namespace lb {

// 分类规则设置窗口（无模式，极简）：一行输入（扩展名/文件名）+ 目标下拉
// （7 格子或「永不整理」）+ 自定义规则列表（可删）。默认规则不展示（隐式生效）。
class RulesDialog {
public:
    void Toggle(RuleSet& rules, const std::wstring& rulesFile);
    bool Visible() const { return hwnd_ != nullptr; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void BuildControls(HWND hwnd);
    void RebuildList();
    void Save();

    HWND hwnd_ = nullptr, input_ = nullptr, combo_ = nullptr, list_ = nullptr;
    RuleSet* rules_ = nullptr;
    std::wstring file_;
    std::vector<std::pair<bool, std::wstring>> items_;  // <是否白名单, 键>
    HBRUSH bg_ = nullptr;
};

}  // namespace lb
