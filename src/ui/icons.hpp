#pragma once
#include <map>
#include <string>
#include "ui/d2d_interop.hpp"

namespace lb {

// 文件类型图标 → D2D 位图（进程内内存缓存）。
// exe 按完整路径缓存（每个 exe 有自己的图标），其余按扩展名缓存。
// 取不到时返回 nullptr，调用方跳过绘制即可；不持有 shell 句柄。
class IconCache {
public:
    ID2D1Bitmap* Get(ID2D1DeviceContext* ctx, const std::wstring& path);
    // 设备重建（Explorer 重启）后必须清空：缓存位图属于已销毁的旧 D2D 设备，复用会失效/报错
    void Clear() { cache_.clear(); }

private:
    std::map<std::wstring, ComPtr<ID2D1Bitmap>> cache_;
};

}  // namespace lb
