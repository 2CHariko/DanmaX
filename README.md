# Local Danmaku

C++20 / Qt Quick 本地弹幕覆盖层，Windows x64 优先。

当前为 **0.2 C++ 重构版**，主工作流已落地：异步加载 Bilibili XML、滚动/顶部/底部弹幕、Windows SMTC 会话选择与同步、独立播放/暂停/跳转、设置持久化与旧 INI 导入、日志查看/过滤/导出。

控制界面使用 **Qt 官方 FluentWinUI3**，仅组合导航、设置分组和设置行；弹幕由 C++ 调度和 Qt Quick 公开场景图渲染，没有逐条 QML Timer。无新增第三方依赖，Python 归档不参与构建。

已完成构建、核心/集成测试、原生主题/DPI 截图、真实 Chrome 媒体会话读取及本机部署检查。高对比度、Narrator、独占全屏、真实鼠标穿透和长时间压力仍需验收。性能数据与限制见 [交付状态](docs/FRAMEWORK_STATUS.md)，设计基线见 [UI 约束](docs/UI_DESIGN_SYSTEM.md)。

## 项目结构

```text
src/core/                  纯 C++ 时间轴、轨道与对象池
src/application/           应用编排与 UI 状态
src/infrastructure/        XML、JSON 设置、日志和数据目录
src/platform/windows/      Windows SDK 适配边界
src/renderer/              文字布局缓存与场景图节点
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

CTest 包含动画时钟、轨道/对象池、XML/设置/播放编排集成，以及 offscreen QML smoke 四项测试。测试快照注入不能替代播放器实测。

原生截图与短时负载可通过 `pwsh -NoProfile -File scripts/validate.ps1 -Benchmark` 复现，输出在 `out/validation/<时间戳>`。可选 `-SessionId Chrome` 只读跟随已有媒体会话，不控制外部播放器；报告含媒体标题，不应直接公开。没有对应会话时该项失败。

使用：浏览并加载 XML，选择媒体会话后点击“同步播放”；不依赖播放器时选择“独立播放”。设置即时生效，数值编辑合并 200 ms 后原子写盘。旧 INI 在设置页手动导入，原文件保留；不自动沿用旧机器的 XML 路径。

默认支持 XML 模式 1/4/5；高级脚本/定位等模式过滤计数。文件上限 512 MiB、100 万条、每条 512 字符。跳转清空在屏弹幕，从新位置继续，不补发历史。最大在屏数量是资源上限，不是流畅度保证。

## 历史版本

旧代码和样本见 [归档说明](archive/README.md)。旧虚拟环境已保留，但移动后入口可能失效，需要运行时请在归档目录重建，不要把 Python 依赖加回新项目根目录。
