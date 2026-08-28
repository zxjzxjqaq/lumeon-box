#pragma once
#include <string>
#include "core/rules.hpp"

namespace lb {

// 取扩展名 → 规则表；未收录/无扩展名 → 其他（文件必有归属，目录由调用方排除）
Category ClassifyFile(const std::wstring& path, const RuleSet& rules);

}  // namespace lb
