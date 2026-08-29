#pragma once
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lb {

enum class Category { Doc, Image, Video, Audio, Archive, Installer, Other, None };

std::wstring CategoryName(Category c);                  // 文档/图片/.../其他
std::wstring CategoryKey(Category c);                   // doc/image/.../other
Category CategoryFromKey(std::wstring_view key);

struct RuleEntry {
    std::wstring ext;   // 小写扩展名（不带点）
    Category cat;
    bool custom;        // true=用户自定义（覆盖默认）
};

class RuleSet {
public:
    RuleSet();                                          // 内置默认规则表
    Category Match(std::wstring_view extLower) const;   // 未收录 → None
    void SetCustom(std::wstring extLower, Category c);  // 用户规则覆盖默认
    bool RemoveCustom(std::wstring extLower);           // 回落默认表；非自定义返回 false

    // 白名单：命中的文件永不整理。key = 精确文件名 或 "*.ext"
    void AddNever(std::wstring key);
    bool RemoveNever(const std::wstring& key);
    bool IsNever(const std::wstring& filename) const;
    const std::set<std::wstring>& NeverList() const { return never_; }

    // 生效规则合并视图（custom 覆盖默认），按扩展名排序
    std::vector<RuleEntry> Entries() const;

    bool Save(const std::filesystem::path& file) const; // rules.json
    static RuleSet Load(const std::filesystem::path& file);

private:
    std::unordered_map<std::wstring, Category> default_;
    std::unordered_map<std::wstring, Category> custom_;
    std::set<std::wstring> never_;  // 精确文件名 或 "*.ext"
};

}  // namespace lb
