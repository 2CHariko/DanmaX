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
- `run.ps1`：运行应用调试实例（支持 `-SmokeTest`）。
- `release.ps1`：官方正式打包与发布总控脚本（构建号自动递增，生成便携单 EXE、绿色 ZIP 与校验和）。
- `validate-static.ps1`：静态单 EXE 便携版无释放与白名单严格验收。
- `clean.ps1`：安全清理构建缓存（保留用户配置）。
