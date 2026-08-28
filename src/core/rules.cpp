#include "core/rules.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include "core/strutil.hpp"

namespace lb {
namespace json = nlohmann;

std::wstring CategoryName(Category c) {
    switch (c) {
    case Category::Doc: return L"文档";
    case Category::Image: return L"图片";
    case Category::Video: return L"视频";
    case Category::Audio: return L"音频";
    case Category::Archive: return L"压缩包";
    case Category::Installer: return L"安装包";
    case Category::Other: return L"其他";
    default: return L"";
    }
}

std::wstring CategoryKey(Category c) {
    switch (c) {
    case Category::Doc: return L"doc";
    case Category::Image: return L"image";
    case Category::Video: return L"video";
    case Category::Audio: return L"audio";
    case Category::Archive: return L"archive";
    case Category::Installer: return L"installer";
    case Category::Other: return L"other";
    default: return L"none";
    }
}

Category CategoryFromKey(std::wstring_view key) {
    if (key == L"doc") return Category::Doc;
    if (key == L"image") return Category::Image;
    if (key == L"video") return Category::Video;
    if (key == L"audio") return Category::Audio;
    if (key == L"archive") return Category::Archive;
    if (key == L"installer") return Category::Installer;
    if (key == L"other") return Category::Other;
    return Category::None;
}

RuleSet::RuleSet() {
    auto add = [this](Category c, std::initializer_list<std::wstring_view> exts) {
        for (auto e : exts) default_.emplace(std::wstring(e), c);
    };
    add(Category::Doc, {L"doc", L"docx", L"ppt", L"pptx", L"xls", L"xlsx", L"pdf",
                        L"txt", L"md", L"rtf", L"csv", L"epub", L"log", L"xmind",
                        L"one"});
    add(Category::Image, {L"png", L"jpg", L"jpeg", L"gif", L"bmp", L"webp", L"heic",
                          L"svg", L"ico", L"tif", L"tiff"});
    add(Category::Video, {L"mp4", L"mkv", L"avi", L"mov", L"wmv", L"flv", L"ts",
                          L"webm", L"m4v", L"rmvb"});
    add(Category::Audio, {L"mp3", L"wav", L"flac", L"aac", L"ogg", L"m4a", L"wma",
                          L"ape"});
    add(Category::Archive, {L"zip", L"rar", L"7z", L"tar", L"gz", L"bz2", L"xz",
                            L"iso"});
    add(Category::Installer, {L"exe", L"msi", L"msix", L"appx", L"msp"});
}

Category RuleSet::Match(std::wstring_view extLower) const {
    std::wstring e(extLower);
    if (auto it = custom_.find(e); it != custom_.end()) return it->second;
    if (auto it = default_.find(e); it != default_.end()) return it->second;
    return Category::None;
}

void RuleSet::SetCustom(std::wstring extLower, Category c) {
    custom_[std::move(extLower)] = c;
}

void RuleSet::Save(const std::filesystem::path& file) const {
    json::json custom = json::json::object();
    for (auto& [e, c] : custom_) custom[ToUtf8(e)] = ToUtf8(CategoryKey(c));
    json::json j{{"custom", custom}};
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::binary);
    f << j.dump(2);
}

RuleSet RuleSet::Load(const std::filesystem::path& file) {
    RuleSet rs;
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return rs;
    try {
        json::json j = json::json::parse(std::ifstream(file, std::ios::binary));
        if (j.contains("custom")) {
            for (auto it = j["custom"].begin(); it != j["custom"].end(); ++it) {
                Category c = CategoryFromKey(ToWide(it.value().get<std::string>()));
                if (c != Category::None) rs.SetCustom(ToWide(it.key()), c);
            }
        }
    } catch (...) {
        // 损坏即回默认
    }
    return rs;
}

}  // namespace lb
