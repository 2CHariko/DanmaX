# Local Danmaku

C++20 / Qt Quick 本地弹幕覆盖层，Windows x64 优先。

当前是可构建的开发框架：控制窗口、透明置顶预览窗口、滚动示例文字、暂停/继续、项目内数据与缓存路径。**尚未实现 XML 加载、SMTC 会话同步、多轨调度和完整设置。** 单条预览使用 Qt Quick Text，不代表最终批量弹幕渲染架构或性能结论。

## 项目结构

```text
src/core/                  纯 C++ 模型与动画时钟
src/application/           应用编排与 UI 状态
src/infrastructure/        数据目录与基础设施
src/platform/windows/      Windows SDK 适配边界
src/renderer/              统一动画驱动
src/app/                   程序入口和 Qt 对象装配
qml/                       控制界面与透明覆盖窗
tests/                     CTest 测试
scripts/                   准备、诊断、构建、运行、部署、清理
toolchain/                 固定工具版本、下载地址与哈希
docs/                      架构和开发说明
archive/python-qt/         原 Python 项目完整归档
archive/plans/             历史 Flutter 方案
```

Qt、CMake、Ninja 位于 `.tools/`，下载和临时文件位于 `.cache/`，开发数据位于 `.local/`，构建和部署位于 `out/`。这些目录不提交 Git。旧版归档不参与新版本构建。

## 准备环境

需要 Windows x64、PowerShell 7、Windows 自带 tar、MSVC 2022 和 Windows SDK。工具精确版本见 `toolchain/dependencies.lock.json`。

```powershell
pwsh -NoProfile -File scripts/bootstrap.ps1
```

准备本机编译器声明：将 `toolchain/local.example.json` 复制到 `.local/toolchain.local.json`，将 `visualStudioPath` 修改为本机 Build Tools 安装目录。该文件不提交；脚本不会自动搜索其他项目或静默使用 PATH 中的编译器。

MSVC/Windows SDK 为显式系统安装例外，部分共享组件无法全部放进项目目录。Qt/CMake/Ninja 独立安装到本项目，不需要 Qt Creator、Python、Flutter、vcpkg 或 Conan。

```powershell
pwsh -NoProfile -File scripts/doctor.ps1
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-debug -Test
pwsh -NoProfile -File scripts/run.ps1 -Preset windows-debug
```

脚本可以从任意工作目录通过完整脚本路径调用。只在执行期间改变进程环境，结束后恢复；不修改永久 PATH。常规构建不下载依赖。已有全部下载包时可使用 `bootstrap.ps1 -Offline`。

## 验证与开发部署

```powershell
# 不依赖 Qt 的领域核心测试
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-core -Test

# Release 构建和窗口加载测试
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-release -Test

# 原生窗口短时启动，然后自动退出
pwsh -NoProfile -File scripts/run.ps1 -Preset windows-release -SmokeTest

# 生成新的开发部署目录，不覆盖既有包
pwsh -NoProfile -File scripts/package.ps1

# 先预览清理范围，再清理指定构建目录
pwsh -NoProfile -File scripts/clean.ps1 -Preset windows-debug -WhatIf
```

部署目录需要官方 Microsoft Visual C++ 2015–2022 x64 运行库，脚本不自动安装。公开发布前还需完成干净机器验证及第三方分发材料，见 [第三方组件说明](docs/THIRD_PARTY.md)。

CTest 包含动画时钟边界测试和 offscreen 窗口加载测试；它不能证明系统级鼠标穿透、真实透明合成、多显示器或媒体同步正确。后续按 [架构方案](docs/CPP_QT_ARCHITECTURE.md) 逐项验证。

## 历史版本

旧代码和样本见 [归档说明](archive/README.md)。旧虚拟环境已保留，但移动后入口可能失效，需要运行时请在归档目录重建，不要把 Python 依赖加回新项目根目录。
