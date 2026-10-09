# DanmaX 贡献指南 (Contributing Guide)

感谢您对 DanmaX 项目的关注！我们非常欢迎社区提出 Issue、反馈 Bug 或提交 Pull Request。为了确保代码库的整洁性、可复现构建与严格的工程质量，请在提交代码前仔细阅读本指南。

---

## 1. 核心约束与工作边界

在开始开发前，请务必熟悉项目根目录下的 [AGENTS.md](AGENTS.md) 及 [docs/CPP_QT_ARCHITECTURE.md](docs/CPP_QT_ARCHITECTURE.md)：
1. **单一开发入口**：根目录 C++/Qt 项目是唯一的主线代码入口。历史 Python 原型及计划文档独立存放在 `archive` 分支的 `archive/` 目录中，不参与任何构建。
2. **零侵入式项目环境**：
   - 工具（Qt, CMake, Ninja）位于 `.tools/`；依赖位于 `.deps/`；缓存位于 `.cache/`；构建输出位于 `out/`；本地配置位于 `.local/`。
   - 禁止修改系统的永久 PATH 环境变量，禁止依赖全局包管理器（vcpkg / Conan）或隐式全局环境。
   - 所有脚本以其自身所在目录为基准推导项目根，支持从任意当前目录调用。
3. **分层架构规范**：
   - `src/core/` 领域核心**仅依赖 C++20 标准库**，严禁引入 Qt、QML 或 Windows API 依赖。
   - QML 仅负责交互展示与布局，严禁在 QML 中进行高频数据解析或运行逐条弹幕 Timer。
   - 渲染层只能通过公开场景图 API 在 Qt Quick 渲染生命周期阶段创建与销毁 GPU 资源。

---

## 2. 开发环境准备

- **操作系统**：Windows 10/11 x64
- **必要系统工具**：PowerShell 7 (`pwsh`), Windows 自带 `tar.exe`, MSVC 2022 (Visual Studio / Build Tools 14.4x), Windows SDK (10.0.26100.0+)
- **项目自动化依赖**：运行以下命令自动下载并准备项目内专用的 Qt 6.11、CMake 3.31 与 Ninja 1.12：

```powershell
# 1. 自动准备项目内开发工具链
pwsh -NoProfile -File scripts/bootstrap.ps1

# 2. 声明本机 MSVC 安装路径（复制示例并根据本机实际路径修改）
cp toolchain/local.example.json .local/toolchain.local.json

# 3. 运行环境诊断脚本，确认所有工具路径正确解析
pwsh -NoProfile -File scripts/doctor.ps1
```

---

## 3. 本地构建与自动化测试

项目使用 CMake Presets 与 Ninja 进行构建。日常开发与回归测试使用统一脚本完成：

```powershell
# 1. 纯核心单元测试（Windows-core，不依赖 Qt）
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-core -Test

# 2. 动态 Debug 构建并运行全部 13 项 CTest 测试
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-debug -Test

# 3. 运行开发模式应用
pwsh -NoProfile -File scripts/run.ps1 -Preset windows-debug

# 4. Release 构建与测试
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-release -Test
```

> **测试质量保证**：任何涉及时间轴、轨道分配、XML 解析或在线缓存的修改，提交 PR 前必须确保 `build.ps1 -Preset windows-debug -Test` 全量测试通过。

### 专项回归与验证
除了基础 CTest 单元测试，若改动涉及界面或分发打包，需执行以下专项验证：
```powershell
# UI 自动化视觉回归（浅深主题、长下拉菜单、滚动到底）
pwsh -NoProfile -File scripts/validate-ui.ps1 -Executable out/build/windows-release/bin/DanmaX.exe -All

# 静态单 EXE 便携版无释放与白名单验收（若涉及打包/发布）
pwsh -NoProfile -File scripts/validate-static.ps1 -Executable out/release/v0.2.2/DanmaX-v0.2.2-windows-x64-portable.exe
```

> 💡 **完整脚本手册**：全部 24 个 PowerShell 脚本的详细参数字典、环境清理与高级工作流请查阅 **[开发与构建脚本手册 (docs/SCRIPTS.md)](docs/SCRIPTS.md)**。

---

## 4. 代码风格与规范

- **C++ 格式化**：遵循项目根目录的 `.clang-format` 配置文件规范。
- **命名规范**：
  - 核心类使用 PascalCase（如 `DanmakuEngine`, `PlaybackClock`）。
  - 函数使用 camelCase（如 `reconfigure`, `allocateTrack`）。
  - 私有成员变量使用下划线后缀（如 `position_`, `tracks_`）。
- **QML 风格**：
  - 严格保持 `import QtQuick.Controls.FluentWinUI3`，禁止混搭 Basic 或 Material 皮肤。
  - 自建组件布局使用 4 单位递增节奏（4/8/12/16/24/32）。
  - 必须支持系统深浅色主题自适应与基本键盘交互。

---

## 5. Pull Request 提交流程

1. **Fork 与分支**：基于 `main` 分支拉取新的特性分支（如 `feat/my-awesome-feature` 或 `fix/xml-parser-bug`）。
2. **细粒度 Commit**：提交说明遵循 Conventional Commits 规范（如 `feat: ...`, `fix: ...`, `docs: ...`）。
3. **本地完整验证**：确保本地运行 `scripts/build.ps1 -Test` 成功，无编译告警与测试断言失败。
4. **提交 PR**：描述清晰说明改动的动机、受影响模块、已执行的验证手段与任何潜在边界。
