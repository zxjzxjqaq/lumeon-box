#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace lb {

enum class Category { Doc, Image, Video, Audio, Archive, Installer, Other, None };

std::wstring CategoryName(Category c);                  // 文档/图片/.../其他
std::wstring CategoryKey(Category c);                   // doc/image/.../other
Category CategoryFromKey(std::wstring_view key);

class RuleSet {
public:
    RuleSet();                                          // 内置默认规则表
    Category Match(std::wstring_view extLower) const;   // 未收录 → None
    void SetCustom(std::wstring extLower, Category c);  // 用户规则覆盖默认
    void Save(const std::filesystem::path& file) const; // rules.json
    static RuleSet Load(const std::filesystem::path& file);

private:
    std::unordered_map<std::wstring, Category> default_;
    std::unordered_map<std::wstring, Category> custom_;
};

}  // namespace lb
