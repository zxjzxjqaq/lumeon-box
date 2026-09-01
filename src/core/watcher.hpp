#pragma once
#include <string>
#include <vector>
#include "core/rules.hpp"

namespace lb {

// 一条「文件落桌面 → 建议归位」记录
struct FileLanding {
    std::wstring path;   // 宽字符绝对路径
    Category cat;        // 建议归类（永不为 None）
};

// 给定一批候选路径，返回建议归位的落地记录。
// 与 Organizer::OrganizeAll 的跳过语义保持一致：
//   跳过：不存在 / 目录 / 隐藏 / 系统 / ~$ 临时 / LumenBox 目录本身的同名文件 /
//         白名单(IsNever) / .lnk(默认不整理) / boxDir(<桌面>\LumenBox) 之内。
//   归类：ClassifyFile；None 归一为 Other（文件必有归属）。
std::vector<FileLanding> SuggestLandings(const std::vector<std::wstring>& paths,
                                         const RuleSet& rules,
                                         const std::wstring& boxDir);

}  // namespace lb
