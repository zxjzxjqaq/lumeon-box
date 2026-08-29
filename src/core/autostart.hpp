#pragma once
#include <string>

namespace lb {

// HKCU Run 键开机自启开关（用户级，无需管理员；卸载即删键）
class Autostart {
public:
    // runValueName: Run 键下的值名（默认 LumenBox）
    explicit Autostart(const wchar_t* runValueName = L"LumenBox");

    bool IsEnabled() const;
    bool Enable();    // 写入当前 exe 完整路径
    bool Disable();   // 移除；不存在也返回 false
    std::wstring Path() const;  // 当前注册的命令行（未启用为空）

private:
    std::wstring value_;
};

}  // namespace lb
