# AGENTS.md

## 项目方向与开发红线

- **唯一主线**：根目录 C++20 / Qt 6 Quick / Windows x64 项目为唯一开发入口；`archive` 分支的 `archive/python-qt/` 与 `archive/plans/` 仅作只读归档，不参与当前构建或重构。
- **项目内隔离边界**：开发工具与依赖须落入 `.tools/`、`.deps/`、`.cache/`、`out/` 与 `.local/`（均不入库）；严禁修改永久系统 `PATH`、默认依赖外部 Qt 或引入 Boost/spdlog/vcpkg/Conan/第三方测试或 UI 库。本机 MSVC 与 Windows SDK 须在 `.local/toolchain.local.json` 显式声明，禁止静默回退；配置与构建阶段严禁自动联网；清理动作不得删除 `.local/` 或 `out/run/`。
- **核心架构与渲染**：`src/core/` 仅依赖 C++20 标准库；采用单一单调时钟帧驱动，暂停冻结位置与寿命，跳转与换片重置状态，严禁为每条弹幕创建 `Timer`、独立线程或复杂 QML 树，严禁强制 `terminate` 工作线程。
- **播放器拥有权与定位**：视频同步与独立弹幕为互斥任务，共用唯一引擎、覆盖层与 `SeekController`；仅控制本应用启动并核实 PID、创建时间与路径的 PotPlayer（`/new`）或 VLC 实例，严禁强杀或按进程名/标题接管用户原有播放器，严禁使用进程注入、内存扫描或剪贴板副作用。
- **UI 与平台契约**：控制界面必须使用官方 `FluentWinUI3` 与 `Ui.qml` 语义层，所有功能图标须通过 `resources/fluent/` 使用微软官方 SVG（锁定 `manifest.json` 哈希）；本地文件/目录选择必须使用 Windows 原生对话框，严禁 `DontUseNativeDialog` 或回退 Qt 非原生界面。
- **验证与事实汇报**：核心逻辑使用 CTest / Qt Test 回归；透明穿透、焦点、多屏混合 DPI 与真实播放器须实机验证，汇报严格区分**已验证**、**已实现未验证**与**计划中**，不得将理论目标或模拟缩放写成实机验收结果。
- **文档与 Git 提交**：所有文档以**简体中文**为第一语言并遵循 Microsoft 技术文档规范；Git 提交信息必须遵循 **Conventional Commits 中文格式**（`<type>(<scope>): <简体中文主题>`），可使用 `scripts/install-git-hooks.ps1` 启用本地校验。

## 权威文档索引

开发或修改前，先通过 **[文档中心 (docs/README.md)](docs/README.md)** 定位对应专题规范，不在本文件重复堆砌细节：

| 领域 | 权威文档 |
|---|---|
| 架构、核心与构建边界 | [C++ / Qt 核心架构](docs/architecture/cpp-qt-architecture.md) · [双任务工作流](docs/architecture/dual-workflows.md) · [播放中心](docs/architecture/now-playing.md) · [公共定位事务](docs/architecture/seek-transactions.md) |
| 播放器与覆盖层 | [外部播放器预设与契约](docs/playback/external-players.md) · [内置播放页接入](docs/playback/libvlc-integration.md) · [观看布局](docs/playback/playback-presentation.md) · [覆盖层原生跟随](docs/playback/overlay-follow.md) |
| 界面设计系统与图标 | [FluentWinUI3 界面设计系统](docs/ui/ui-design-system.md) |
| 交付状态与已知限制 | [当前交付状态](docs/status/current-status.md) |
| 文档规范、脚本与提交 | [文档撰写规范](docs/engineering/documentation-style.md) · [Git 提交规范](docs/engineering/git-commit-convention.md) · [脚本手册](docs/engineering/scripts-manual.md) |
| 打包与开源合规 | [Windows 便携分发](docs/release/packaging.md) · [第三方组件与合规](docs/release/third-party.md) |
