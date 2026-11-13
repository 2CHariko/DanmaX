# 参与 DanmaX 开发与贡献

感谢你对 DanmaX 项目的关注与贡献。在提交代码或文档变更前，请阅读以下环境准备、规范与提交流程说明。

> [!IMPORTANT]
> 本项目第一文档与提交语言为**简体中文**。请确保你的文档符合 [DanmaX 文档撰写规范](docs/engineering/documentation-style.md)，提交信息符合 [Git 提交规范](docs/engineering/git-commit-convention.md)。

## 1. 核心架构与开发原则

开始编码前，请先查阅 [AGENTS.md](AGENTS.md) 与 [文档中心](docs/README.md)，并严格遵守以下原则：

1. **纯 C++20 领域核心**：`src/core/` 仅依赖 C++20 标准库，禁止引入 Qt、QML、Win32 或 WinRT 头文件。
2. **项目内环境隔离**：开发依赖、工具链缓存与构建产物统一落在 `.tools/`、`.deps/`、`.cache/`、`out/` 与 `.local/`，禁止修改永久系统 `PATH`。
3. **单一帧驱动与线程安全**：遵守 Qt Quick 场景图生命周期，禁止跨线程修改 QML 对象或为单条弹幕创建 `Timer`。

## 2. 准备本地开发环境

- **操作系统**：Windows 10 / 11 x64
- **系统依赖**：PowerShell 7（`pwsh`）、Windows 自带 `tar.exe`、MSVC 2022（Visual Studio / Build Tools 14.4x）、Windows SDK（`10.0.26100.0+`）

运行以下命令准备项目内工具链并启用本地 Git 提交规范校验：

```powershell
# 1. 准备项目内 CMake、Ninja 与动态 Qt 6.11 SDK
pwsh -NoProfile -File scripts/bootstrap.ps1
pwsh -NoProfile -File scripts/prepare-anitomy.ps1
pwsh -NoProfile -File scripts/prepare-qwindowkit.ps1

# 2. 声明本机 MSVC 与 Windows SDK 路径（按实际安装位置修改）
Copy-Item toolchain/local.example.json .local/toolchain.local.json

# 3. 验证环境与工具链路径
pwsh -NoProfile -File scripts/doctor.ps1

# 4. 启用本地 Commit 规范模板与校验 Hook
pwsh -NoProfile -File scripts/install-git-hooks.ps1
```

## 3. 本地构建与测试

使用统一的 PowerShell 脚本驱动 CMake Presets 与 Ninja：

```powershell
# 纯 C++ 核心单元测试
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-core -Test

# Debug 构建并运行全量 CTest
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-debug -Test

# 启动开发运行实例
pwsh -NoProfile -File scripts/run.ps1 -Preset windows-debug

# Release 构建与测试
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-release -Test
```

如修改涉及界面交互或静态便携打包，请额外执行专项验证：

```powershell
pwsh -NoProfile -File scripts/validate-ui.ps1 -Executable out/build/windows-release/bin/DanmaX.exe
pwsh -NoProfile -File scripts/validate-static.ps1 -Executable out/release/v<version>/DanmaX-v<version>-windows-x64-portable.exe
```

全量 42 个脚本的使用参数请参阅 [PowerShell 开发与构建脚本手册](docs/engineering/scripts-manual.md)。

## 4. 代码与界面风格

- **C++ 格式**：遵循根目录 `.clang-format` 规范。
- **命名规范**：类使用 `PascalCase`，函数使用 `camelCase`，私有成员变量使用下划线后缀（如 `position_`）。
- **QML 风格**：仅使用 `import QtQuick.Controls.FluentWinUI3`，布局遵循 4 逻辑单位节奏，图标统一调用 `resources/fluent/` 中的微软官方 Fluent System Icons SVG，本地文件/目录选择须使用 Windows 原生对话框。详见 [FluentWinUI3 界面设计系统](docs/ui/ui-design-system.md)。

## 5. Git 提交与 Pull Request 流程

1. **分支命名**：基于 `main` 创建特性或修复分支（如 `feat/seek-recovery`、`fix/xml-color-fallback`）。
2. **提交规范**：遵循 **Conventional Commits 中文格式**（`<type>(<scope>): <简体中文主题>`），例如：
   - `feat(player): 支持弹幕匹配失败时暂停播放器并弹出手动匹配窗`
   - `fix(seek): 拦截延迟媒体采样导致的进度条回弹`
   - `docs(ui): 按 Microsoft 文档规范整理界面设计文档`
   详细规则与校验方式见 [Git 提交规范与 Hook 指南](docs/engineering/git-commit-convention.md)。
3. **验证通过**：提交前确保受影响范围的 CTest 与专项脚本验证通过，并在 PR 说明中清楚区分**已验证**、**已实现未验证**与**影响边界**。
