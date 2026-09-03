#pragma once
#include <map>
#include <string>
#include <vector>
#include "core/journal.hpp"
#include "core/rules.hpp"

namespace lb {

// 跨卷安全移动：文件用 MoveFileEx(MOVEFILE_COPY_ALLOWED)；目录同卷改名，异卷递归复制+删除源。
// 说明：MoveFileEx 的 MOVEFILE_COPY_ALLOWED 只对「文件」支持跨卷 copy+delete，对「目录」跨卷会失败。
bool MovePath(const std::wstring& src, const std::wstring& dst);

// 整理执行器：桌面文件 → <容器根>\<分类名>\（容器根默认 %APPDATA%\LumenBox）
// 只移动文件不移动目录；.lnk/隐藏/系统/~$ 临时文件跳过；永不覆盖（冲突加 (2)）。
class Organizer {
public:
    struct Summary {
        int moved = 0;
        std::map<std::wstring, int> byCategory;
    };

    // desktopDir 桌面根；containerDir 容器根（<...>\LumenBox），格子=其下 <类目>\ 子文件夹
    Organizer(std::wstring desktopDir, std::wstring containerDir, const RuleSet& rules,
              Journal& jr);

    Summary OrganizeAll();                                    // 一键整理
    bool MoveToCategory(const std::wstring& path, Category c); // 拖拽归位
    bool MoveBackToDesktop(const std::wstring& path);          // 格子文件放回桌面根
    std::vector<std::wstring> FilesIn(Category c) const;       // 格子内容（绝对路径）
    bool UndoLast();                                           // 撤销最后一个批次

    std::wstring BoxDir(Category c) const;

    // —— 自定义容器（用户自建，每个盒子=容器内一个子文件夹）——
    std::wstring FolderDir(const std::wstring& folder) const;              // 容器\folder
    std::vector<std::wstring> FilesInFolder(const std::wstring& folder) const;
    bool MoveToFolder(const std::wstring& path, const std::wstring& folder);
    // 在容器内新建一个不冲突的盒子子目录（base 如 box_b1，同名自动加序号）
    std::wstring MakeFolder(const std::wstring& base);

private:
    bool MoveOne(const std::wstring& src, Category c);
    std::wstring UniqueTarget(const std::wstring& dst) const;

    std::wstring desktop_;
    std::wstring container_;
    const RuleSet& rules_;
    Journal& jr_;
};

}  // namespace lb
