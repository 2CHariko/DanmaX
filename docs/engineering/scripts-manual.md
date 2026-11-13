# PowerShell 开发与构建脚本手册

DanmaX 使用 `scripts/` 目录下的自动化 PowerShell 7（`pwsh`）脚本驱动环境准备、诊断、编译、运行、打包发布、专项验证与历史调研。

> [!IMPORTANT]
> 所有脚本头部均声明 `#requires -Version 7.0`，请使用 **PowerShell 7（`pwsh.exe`）** 执行，不支持 Windows PowerShell 5.1。脚本通过 `common.ps1` 在子进程设置项目内临时目录与缓存路径，不修改系统永久 `PATH`，配置与构建阶段禁止自动联网。

## 1. 脚本全量索引（42 个脚本）

### 1.1 环境准备、依赖与诊断

| 脚本 | 常用参数 | 用途说明 |
|---|---|---|
| `bootstrap.ps1` | `[-Offline]` | 下载并 SHA-256 校验项目内 CMake、Ninja 与动态 Qt 6.11 SDK |
| `doctor.ps1` | — | 只读诊断 MSVC、Windows SDK、Qt、CMake、Ninja 及锁定清单状态 |
| `clean.ps1` | `[-Preset <Preset\|all>]` | 安全清理校验过的构建目录（不删除 `.local/` 与 `out/run/`） |
| `prepare-anitomy.ps1` | `[-Offline]` | 准备并校验 `toolchain/anitomy.lock.json` 锁定的 Anitomy 源码 |
| `prepare-qwindowkit.ps1` | `[-Offline]` | 准备并校验 `toolchain/qwindowkit.lock.json` 锁定的 QWindowKit 源码 |
| `prepare-fluent-assets.ps1` | `[-Offline]` | 生成并更新 Fluent System Icons 深色改色 SVG 派生稿 |
| `verify-fluent-assets.ps1` | — | 离线校验 `resources/fluent/manifest.json` 中原稿与派生稿 SHA-256 |
| `generate-icon.ps1` | `-OutputDirectory <path>` | 构建期基于系统 `System.Drawing` 生成多尺寸 `.ico` 与 `.png` |
| `install-git-hooks.ps1` | — | 为当前仓库启用 `.githooks/commit-msg` 与 `.gitmessage` 提交模板 |

### 1.2 构建、运行与静态 Qt 工具链

| 脚本 | 常用参数 | 用途说明 |
|---|---|---|
| `build.ps1` | `[-Preset <Preset>] [-Test]` | 初始化 MSVC 环境并执行 CMake/Ninja 编译及可选 CTest |
| `run.ps1` | `[-Preset <Preset>] [-SmokeTest] [-BuildDirectory <dir>] [-NoBuild]` | 关闭同路径旧实例、增量构建并部署运行 `out/run/<预设>/` |
| `prepare-qt-static.ps1` | `[-Offline]` | 下载并校验 `toolchain/qt-static.lock.json` 锁定的静态 Qt 官方源码 |
| `build-qt-static.ps1` | `[-Jobs <1-32>] [-Reconfigure]` | 使用 MSVC `/MT` 编译项目专用静态 Qt SDK |
| `prepare-libvlc.ps1` | `[-Offline]` | 准备 `toolchain/libvlc.lock.json` 锁定的 libVLC 3.0.23 开发运行库 |

### 1.3 打包、发布与共用库

| 脚本 | 常用参数 | 用途说明 |
|---|---|---|
| `release.ps1` | `[-BuildNumber <int>]` | 正式发布总控：递增构建号、生成静态单 EXE、动态 ZIP 与 `checksums.txt` |
| `package.ps1` | `[-Preset <Preset>] [-Format <static-exe\|directory\|single-exe>]` | 按指定格式重新配置、构建并打包，同时输出合规材料目录 |
| `common.ps1` | *(库文件)* | 沙盒环境、Toolchain 初始化、安全路径解析与锁清单校验 |
| `deployment.ps1` | *(库文件)* | 开发运行与动态打包共用的 Qt/QWindowKit 部署与受管文件检查 |
| `portable.ps1` | *(库文件)* | 旧版 CAB 自解压单文件打包辅助函数 |

### 1.4 自动化验证与性能基准

| 脚本 | 常用参数 | 用途说明 |
|---|---|---|
| `validate.ps1` | `[-Benchmark]` | 动态版页面主题烟雾测试与 500/2000 弹幕负载基准采样 |
| `validate-static.ps1` | `-Executable <path>` | 静态单 EXE 验收：无 DLL 释放审计、冷热启动、中文路径与加载模块检查 |
| `validate-ui.ps1` | `-Executable <path> [-ScrollPopup] [-SettingsGroups]` | FluentWinUI3 界面回归：滚动条通道、长下拉展开与浅深主题 |
| `validate-portable.ps1` | `-Executable <path>` | 旧版 CAB 自解压单 EXE 完整性检查 |
| `validate-libvlc-playback.ps1` | `-Video <path> [-Seconds <int>] [-Fullscreen] [-RunName <name>] [-NoBuild]` | 内置 libVLC 实播、全屏往返与连接身份验证 |
| `compare-renderers.ps1` | `[-Seconds <int>] [-Preset <Preset>]` | 对比 `QSGTextNode` 与 `QImage` 纹理渲染开销 |
| `compare-texture-budgets.ps1` | `-Xml <path> [-Count <int>] [-Scales <double[]>]` | 对比固定 64 MiB 与自适应 512 MiB 文字纹理预算表现 |

### 1.5 调研、探针与专项测试脚本（不参与正式发布构建）

| 脚本组 | 包含脚本 | 关联文档 |
|---|---|---|
| Anitomy 验证 | `test-anitomy-library.ps1`、`test-anitomy-probe.ps1` | [Anitomy 接入评估](../research/anitomy-evaluation.md) |
| libVLC 探针 | `prepare-libvlc-probe.ps1`、`probe-libvlc.ps1` | [libVLC 嵌入探针](../research/probes/libvlc-embedding-probe.md) |
| PotPlayer 探针 | `probe-potplayer-lifecycle.ps1` | [PotPlayer 联动调研](../research/potplayer-drop-open.md) |
| 浮层 Acrylic 探针 | `run-acrylic-popup-probe.ps1` | [Acrylic 浮层探针](../research/probes/acrylic-popup-probe.md) |
| 第三方 UI 调研 | `prepare-ui-library-probe.ps1`、`probe-ui-libraries.ps1`、`deploy-fluent-probe.ps1`、`prepare-fluentui.ps1`、`fluentui.ps1` | [UI 控件库评估](../research/ui-library-evaluation.md) |
| XAML Islands 原型 | `prepare-winui-island.ps1`、`probe-winui-island.ps1`、`measure-winui-island.ps1` | [WinUI 3 XAML Islands 评估](../research/winui-xaml-islands.md) |

## 2. 常见端到端工作流

### 2.1 新环境首次配置与调试构建

```powershell
pwsh -NoProfile -File scripts/bootstrap.ps1
pwsh -NoProfile -File scripts/prepare-anitomy.ps1
pwsh -NoProfile -File scripts/prepare-qwindowkit.ps1
Copy-Item toolchain/local.example.json .local/toolchain.local.json
# 编辑 .local/toolchain.local.json 填入本机 MSVC 路径后执行诊断
pwsh -NoProfile -File scripts/doctor.ps1
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-debug -Test
pwsh -NoProfile -File scripts/run.ps1 -Preset windows-debug
```

### 2.2 静态单 EXE 打包与发布验收

```powershell
pwsh -NoProfile -File scripts/prepare-qt-static.ps1
pwsh -NoProfile -File scripts/build-qt-static.ps1 -Jobs 8
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-static-release -Test
pwsh -NoProfile -File scripts/release.ps1
pwsh -NoProfile -File scripts/validate-static.ps1 -Executable out/release/v<version>/DanmaX-v<version>-windows-x64-portable.exe
```

## 3. 常见问题排查

- **提示需要 PowerShell 7.0**：请改用 `pwsh` 运行，勿使用系统自带 `powershell.exe`（5.1）。
- **提示缺少 `VsDevCmd.bat`**：复制 `toolchain/local.example.json` 到 `.local/toolchain.local.json`，设置正确的 `visualStudioPath` 与 `allowExternalSystemToolchain: true`，再运行 `scripts/doctor.ps1`。
- **`release.ps1` 报 `Static package imports disallowed runtime DLLs`**：确认未使用动态 Qt 或 `/MD` 依赖，应使用 `windows-static-release` 预设与项目内静态 Qt SDK。
- **`build-qt-static.ps1` 内存不足**：通过 `-Jobs 4` 降低并行编译核心数。

## 相关内容

- [Windows 便携分发](../release/packaging.md)
- [第三方组件与合规说明](../release/third-party.md)
- [Git 提交规范与 Hook 指南](git-commit-convention.md)
- [脚本目录说明](../../scripts/README.md)
