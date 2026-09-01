#pragma once
#include <string>
#include <vector>

namespace lb {

// 一条应用索引条目
struct AppItem {
    std::wstring name;   // 显示名（开始菜单去掉 .lnk / App Paths 子键名）
    std::wstring path;   // 可执行目标路径
};

// 纯函数：按查询过滤 + 排序（大小写不敏感子串；前缀命中优先）。
// 空查询返回全部（按显示名排序）。供 Search 与单元测试共用。
std::vector<AppItem> SearchItems(const std::vector<AppItem>& items,
                                 const std::wstring& q);

// 纯函数：桌面文件搜索。files 为绝对路径；返回匹配的 AppItem{文件名, 路径}。
// 同样前缀优先排序。供聚光灯与单元测试共用。
std::vector<AppItem> SearchFiles(const std::vector<std::wstring>& files,
                                 const std::wstring& q);

// 纯函数：按路径去重（首个优先），空路径忽略。供 Build 与单元测试共用。
void AddUniqueItem(std::vector<AppItem>& items, AppItem it);

// 应用索引：启动时读一次（开始菜单 .lnk + 注册表 App Paths），供快速搜索过滤。
// 不建全盘索引、不后台扫描（§3.2「极低占用」承诺）。
class AppIndex {
public:
    void Build();                                    // 枚举开始菜单 + App Paths（含 COM/注册表）
    const std::vector<AppItem>& Items() const { return items_; }
    std::vector<AppItem> Search(const std::wstring& q) const { return SearchItems(items_, q); }

private:
    std::vector<AppItem> items_;
};

}  // namespace lb
