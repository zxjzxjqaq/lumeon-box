#pragma once
#include <map>
#include <string>
#include <vector>
#include "core/journal.hpp"
#include "core/rules.hpp"

namespace lb {

// 整理执行器：桌面文件 → <Desktop>\LumenBox\<分类名>\
// 只移动文件不移动目录；.lnk/隐藏/系统/~$ 临时文件跳过；永不覆盖（冲突加 (2)）。
class Organizer {
public:
    struct Summary {
        int moved = 0;
        std::map<std::wstring, int> byCategory;
    };

    Organizer(std::wstring desktopDir, const RuleSet& rules, Journal& jr);

    Summary OrganizeAll();                                    // 一键整理
    bool MoveToCategory(const std::wstring& path, Category c); // 拖拽归位
    bool MoveBackToDesktop(const std::wstring& path);          // 格子文件放回桌面根
    std::vector<std::wstring> FilesIn(Category c) const;       // 格子内容（绝对路径）
    bool UndoLast();                                           // 撤销最后一个批次

    std::wstring BoxDir(Category c) const;

private:
    bool MoveOne(const std::wstring& src, Category c);
    std::wstring UniqueTarget(const std::wstring& dst) const;

    std::wstring desktop_;
    const RuleSet& rules_;
    Journal& jr_;
};

}  // namespace lb
