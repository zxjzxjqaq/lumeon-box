# LumenBox — 桌面收纳格

一个跑在桌面上的「玻璃收纳格」应用。把常用文件/文件夹、待办事项整理成**桌面容器网格**，直接拖放收纳、右键调出资源管理器菜单、快速搜索，视觉上融合到桌面背景。

![LumenBox](assets/icon_512.png)

## 功能
- **容器网格**：最多 4 列 × 多行的玻璃收纳格，末行右对齐；每列独立纵向堆叠。
- **拖放收纳**：把文件拖进格子即整理；格子间可拖拽重排；重叠区拖拽换位。
- **资源管理器菜单**：右键文件/桌面背景调用系统原生菜单（复制、剪切、粘贴、新建、打开终端等）。
- **快速搜索(Spotlight)**：仅检索容器内文件，高亮跟随鼠标。
- **待办(Tasks)**：按日期记录待办，重启保留。
- **右键桌面背景**：桌面上下文菜单；**左键仍可整组拖动**所有容器。
- **回收站**：删除进回收站（可还原），文件夹变更实时刷新。
- **单实例**：重复启动会唤醒已开窗口，不产生多开。

## 安装
- 直接运行 `installer\LumenBox-Setup-1.0.0.exe`（或在 GitHub [Releases](https://github.com/zxjzxjqaq/lumeon-box/releases) 下载）。
- 安装后自动创建桌面/开始菜单快捷方式，并支持卸载。

## 从源码构建
环境：Windows 10/11、VS2022 BuildTools（x64）、CMake≥3.25。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --target lumenbox --config Release
```

构建产物：`build\Release\lumenbox.exe`。

重新生成应用图标：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_icon.ps1
```

## 技术栈
C++23 · Win32 · Direct2D · DirectComposition · CMake

## 数据
配置与数据存放在 `%APPDATA%\LumenBox`（每个容器为 `box_b<idx>` 子文件夹）。
