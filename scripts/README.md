# DanmaX 脚本目录

本目录包含 DanmaX 项目的全部构建、运行、测试、打包、发布与辅助工具脚本（共 42 个 PowerShell 脚本）。

> [!IMPORTANT]
> 所有脚本均要求使用 **PowerShell 7（`pwsh`）** 运行。完整参数说明与端到端工作流请参阅 [PowerShell 开发与构建脚本手册](../docs/engineering/scripts-manual.md)。

## 常用核心脚本

- `bootstrap.ps1`：下载并准备项目内 CMake、Ninja 与 Qt 6.11 工具链。
- `doctor.ps1`：只读诊断 MSVC、Windows SDK、Ninja、CMake、Qt 与锁文件状态。
- `build.ps1`：主构建入口（`-Preset windows-debug` / `windows-release` / `windows-static-release` / `windows-core`，可选 `-Test`）。
- `run.ps1`：关闭同路径旧实例、增量构建并部署至 `out/run/<预设>/` 启动。
- `deployment.ps1`：开发运行与动态打包共用的 Qt 部署规则库。
- `release.ps1`：官方正式发布总控（自动递增构建号、生成静态单 EXE、动态 ZIP 与校验和）。
- `validate-static.ps1`：静态单 EXE 便携版无释放与白名单严格验收。
- `clean.ps1`：安全清理指定构建缓存（保留 `.local/` 用户配置与 `out/run/`）。
- `install-git-hooks.ps1`：为当前仓库启用 `.githooks/commit-msg` 与 `.gitmessage` 中文 Commit 规范。

## 开发版内置播放器与调研探针

- `prepare-libvlc.ps1 [-Offline]`：准备共享的 libVLC 3.0.23 开发运行库（兼容旧 `prepare-libvlc-probe.ps1` 入口）。`windows-libvlc-debug/release` 仅用于开发验证，`validate-libvlc-playback.ps1` 用于实播与全屏回归；详见 [内置播放页接入](../docs/playback/libvlc-integration.md)。
- `prepare-winui-island.ps1` / `probe-winui-island.ps1` / `measure-winui-island.ps1`：WinUI 3 XAML Islands 原型评估工具，不参与正式构建；详见 [WinUI 3 XAML Islands 评估](../docs/research/winui-xaml-islands.md)。
