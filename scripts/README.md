# DanmaX 脚本目录 (Scripts)

本目录包含 DanmaX 项目的全部构建、运行、测试、打包与发布 PowerShell 脚本。

所有脚本均要求 **PowerShell 7 (`pwsh`)**。

完整的使用手册、各脚本参数详解与端到端工作流请参阅：
👉 **[开发与构建脚本手册 (docs/SCRIPTS.md)](../docs/SCRIPTS.md)**

---

### 常用核心脚本速览

- `bootstrap.ps1`：下载并准备项目内 CMake、Ninja 与 Qt 6.11 工具链。
- `doctor.ps1`：环境健康诊断（检查 MSVC、Windows SDK、Ninja、CMake、Qt）。
- `build.ps1`：主构建入口（`-Preset windows-debug`、`-Preset windows-release`，可选 `-Test`）。
- `run.ps1`：关闭旧实例、增量构建并更新 `out/run/<预设>/` 独立运行目录；配置、日志、缓存与 EXE 同级，Debug/Release 互相隔离。支持 `-SmokeTest`、`-BuildDirectory`、`-NoBuild`。
- `deployment.ps1`：开发运行与动态打包共用的 Qt 部署规则，保留运行数据并检查受管文件。
- `release.ps1`：官方正式打包与发布总控脚本（构建号自动递增，生成便携单 EXE、绿色 ZIP 与校验和）。
- `validate-static.ps1`：静态单 EXE 便携版无释放与白名单严格验收。
- `clean.ps1`：安全清理构建缓存（保留用户配置）。
- `prepare-libvlc-probe.ps1` / `probe-libvlc.ps1`：libVLC 内置播放探针的显式依赖准备与离线构建／运行，不参与生产构建；见 [最小验证报告](../docs/LIBVLC_EMBEDDING_PROBE.md)。
- `prepare-winui-island.ps1` / `probe-winui-island.ps1` / `measure-winui-island.ps1`：可选 WinUI 3 XAML Islands 原型的显式依赖准备、构建/部署/交互验证及初步开销采样；不参与生产构建，见 [接入评估](../docs/WINUI_XAML_ISLANDS_EVALUATION.md)。

### 内置播放器开发版

`prepare-libvlc.ps1 [-Offline]` 显式准备共享依赖（旧 prepare-libvlc-probe 入口兼容）。`build.ps1 -Preset windows-libvlc-release -Test` 与 `run.ps1 -Preset windows-libvlc-release` 接入真实播放页；Debug 对应 windows-libvlc-debug。依赖未准备时构建失败，不联网。

`validate-libvlc-playback.ps1 -Video <本地视频> -Seconds 1800 -RunName <新名称> -NoBuild` 创建隔离运行目录及状态／画面证据，正常等待结束，不强杀进程。`-Scale 1.5` 是模拟缩放。全部结果和限制见 docs/LIBVLC_INTEGRATION.md。
