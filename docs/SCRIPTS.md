# DanmaX 开发与构建脚本手册 (Developer Scripts Manual)

本项目采用高度标准化、自动化的 PowerShell 脚本体系驱动整个开发、调试、测试、静态 SDK 构建、打包发布与性能基准评测。

---

## 1. 运行规范与设计原则

所有脚本位于 `scripts/` 目录，遵循以下底层设计原则（详见 `AGENTS.md`）：

1. **统一运行时**：
   - 严格要求 **PowerShell 7 (`pwsh`)**。脚本头部声明 `#requires -Version 7.0`，不支持 Windows PowerShell 5.1。
2. **零系统侵入与沙盒隔离**：
   - 所有脚本通过 `common.ps1` 中的 `Invoke-ProjectEnvironment` 在子进程执行，临时设置 `TEMP`/`TMP`、`QML_DISK_CACHE_PATH` 等。
   - 依赖与开发工具落在项目内 `.tools/` 与 `.deps/`，产物落在 `out/`，缓存落在 `.cache/`，配置落在 `.local/`。
   - **禁止修改系统永久 PATH**，禁止依赖全局包管理器（vcpkg / Conan）。
3. **自解析项目根路径**：
   - 脚本以自身物理位置反推项目根目录，支持从任意当前工作目录安全调用。
4. **编译与配置分离**：
   - 配置与构建过程严格禁止自动联网；所有依赖下载、哈希校验均为独立显式步骤。

---

## 2. 脚本全量速查索引

项目共包含 **24 个脚本**，按生命周期与职责分类如下：

| 类别 | 脚本文件 | 核心参数摘要 | 功能简述 |
| :--- | :--- | :--- | :--- |
| **环境准备与诊断** | [`bootstrap.ps1`](#31-环境准备与诊断) | `[-Offline]` | 下载并校验项目内 CMake、Ninja 及动态 Qt 6.11 SDK |
| | [`doctor.ps1`](#31-环境准备与诊断) | *(无)* | 诊断本机 MSVC、Windows SDK、Ninja、CMake、Qt 及锁文件状态 |
| | [`clean.ps1`](#31-环境准备与诊断) | `[-Preset <Preset\|all>]` | 安全清理指定或全部构建预设目录（保留 `.local/` 用户配置） |
| **日常编译与调试** | [`build.ps1`](#32-日常编译调试与运行) | `[-Preset <Preset>] [-Clean] [-Test]` | 驱动 CMake/Ninja 编译主工程各预设，可选执行全量 CTest |
| | [`run.ps1`](#32-日常编译调试与运行) | `[-Preset <Preset>] [-SmokeTest] [-BuildDirectory <dir>]` | 启动开发调试实例，注入独立的进程临时路径与本地数据目录 |
| **静态 Qt 工具链** | [`prepare-qt-static.ps1`](#33-静态-qt-sdk-工具链构建) | `[-Offline]` | 联网下载并 SHA-256 校验 Qt 官方源码归档至 `.cache/sources/` |
| | [`build-qt-static.ps1`](#33-静态-qt-sdk-工具链构建) | `[-Jobs <1-32>] [-Reconfigure]` | 纯静态 MSVC `/MT` 编译项目专用静态 Qt SDK（耗时较长） |
| **打包与正式发布** | [`release.ps1`](#34-制品打包与正式发布) | `[-BuildNumber <int>]` | **官方发布总控**：自增构建号，打包静态单 EXE 与动态 ZIP，生成哈希 |
| | [`package.ps1`](#34-制品打包与正式发布) | `[-Preset <Preset>] [-Format <Format>]` | 单格式打包（支持 `static-exe`、`directory` 与 `single-exe`） |
| **自动化质量验证** | [`validate.ps1`](#35-自动化质量验证与基准测试) | `[-Benchmark] [-SessionId <id>]` | 动态版各页面主题烟雾测试与 500/2000 活动弹幕基准性能采样 |
| | [`validate-static.ps1`](#35-自动化质量验证与基准测试) | `-Executable <path> [-SessionId <id>]` | **静态便携版专项目标验收**：无 DLL 释放审计、冷热启动与路径容忍 |
| | [`validate-ui.ps1`](#35-自动化质量验证与基准测试) | `-Executable <path> [-All] [-Pages] ...` | 官方 FluentWinUI3 界面回归：滚动条、长下拉菜单、浅深主题截图 |
| | [`validate-portable.ps1`](#35-自动化质量验证与基准测试) | `-Executable <path>` | 旧版 CAB 自解压启动器专用验证 |
| **性能横向评测** | [`compare-renderers.ps1`](#35-自动化质量验证与基准测试) | `[-Seconds <int>]` | 文本渲染后端对比：公开 Qt Quick 文本项 vs QImage 图片后端 |
| | [`compare-texture-budgets.ps1`](#35-自动化质量验证与基准测试) | `-Xml <path> [-Count <int>]` | 文字纹理显存预算评测：固定 64 MiB 预算 vs 自适应 512 MiB 预算 |
| **底层库与资产** | [`common.ps1`](#36-底层支持库与专用资产工具) | *(库文件，供点源调用)* | 提供环境沙盒、编译器加载、工具定位、路径校验等核心逻辑 |
| | [`portable.ps1`](#36-底层支持库与专用资产工具) | *(库文件，供点源调用)* | 便携构建底层支持（如 MSVC `/showIncludes` 前缀解析） |
| | [`generate-icon.ps1`](#36-底层支持库与专用资产工具) | `-OutputDirectory <path>` | 基于系统 `System.Drawing` 动态合成各尺寸 `DanmaX.ico` 与 PNG |
| | [`verify-fluent-assets.ps1`](#36-底层支持库与专用资产工具) | *(无)* | 校验 `resources/fluent/` 内图标资产与清单的一致性 |
| **历史探测工具群** | `probe-ui-libraries.ps1` 等 (共 5 个) | *(专有参数)* | 用于历史调研第三方 Fluent 库与官方控件对比的探测部署工具 |

---

## 3. 脚本详细使用说明

### 3.1 环境准备与诊断

#### `bootstrap.ps1`
- **用途**：首次克隆项目后的第一步。按 `toolchain/dependencies.lock.json` 下载 CMake 3.31.6、Ninja 1.12.1 及 Qt 6.11.0 官方压缩包，校验 SHA-256 并解压至 `.tools/`。
- **示例**：
  ```powershell
  # 联网下载并准备开发工具链
  pwsh -NoProfile -File scripts/bootstrap.ps1

  # 若下载包已存放在 .cache/downloads/，离线解压准备
  pwsh -NoProfile -File scripts/bootstrap.ps1 -Offline
  ```

#### `doctor.ps1`
- **用途**：全面检查开发环境。验证 `.local/toolchain.local.json` 所指定的 Visual Studio / MSVC 与 Windows SDK 是否存在，检查 CMake/Ninja/Qt 路径及版本。
- **示例**：
  ```powershell
  pwsh -NoProfile -File scripts/doctor.ps1
  ```

#### `clean.ps1`
- **用途**：安全清理 `out/build/` 下的 CMake 构建产物。**绝不会删除 `.local/` 用户配置或系统缓存**。
- **参数**：
  - `-Preset <name>`：清理指定预设，支持 `windows-debug`、`windows-release`、`windows-static-release`、`windows-core`，或默认 `all`（全部清理）。
- **示例**：
  ```powershell
  pwsh -NoProfile -File scripts/clean.ps1 -Preset windows-debug
  pwsh -NoProfile -File scripts/clean.ps1 -Preset all
  ```

---

### 3.2 日常编译、调试与运行

#### `build.ps1`
- **用途**：主编译命令。调用项目内置 CMake 与 Ninja 编译目标，支持 CTest 回归。
- **参数**：
  - `-Preset <name>`：构建预设，可选 `windows-debug`（默认）、`windows-release`、`windows-static-release`、`windows-core`。
  - `-Clean`：编译前先清理当前预设目录。
  - `-Test`：编译成功后自动执行 CTest 测试集。
- **示例**：
  ```powershell
  # 日常 Debug 开发编译
  pwsh -NoProfile -File scripts/build.ps1 -Preset windows-debug

  # 运行全量 13 项单元与回归测试
  pwsh -NoProfile -File scripts/build.ps1 -Preset windows-release -Test
  ```

#### `run.ps1`
- **用途**：启动编译出的 `DanmaX.exe`。自动注入专用的环境变量沙盒，将配置重定向至 `.local/data`，缓存定向至项目内 QML 缓存目录。
- **参数**：
  - `-Preset <name>`：选择运行的预设目标（默认 `windows-debug`）。
  - `-SmokeTest`：自动化烟雾模式，同时创建控制面板与覆盖窗，加载后自动安全退出。
- **示例**：
  ```powershell
  pwsh -NoProfile -File scripts/run.ps1 -Preset windows-debug
  pwsh -NoProfile -File scripts/run.ps1 -Preset windows-release -SmokeTest
  ```

---

### 3.3 静态 Qt SDK 工具链构建

用于编译便携版所需的纯静态 Qt SDK（只需在首次构建静态包前执行一次）。

#### `prepare-qt-static.ps1`
- **用途**：依据 `toolchain/qt-static.lock.json` 下载 Qt 官方源码归档（qtbase, qtshadertools, qtdeclarative），校验 SHA-256 后解压至 `.cache/sources/qt-6.11.0-static`。
- **示例**：
  ```powershell
  pwsh -NoProfile -File scripts/prepare-qt-static.ps1
  pwsh -NoProfile -File scripts/prepare-qt-static.ps1 -Offline
  ```

#### `build-qt-static.ps1`
- **用途**：从已校验源码通过 MSVC `/MT` 编译项目专属的裁剪版静态 Qt 6.11.0 SDK。
- **参数**：
  - `-Jobs <int>`：并行编译核心数（1-32，默认 8）。
  - `-Reconfigure`：清理旧的 CMake 缓存并重新配置。
- **示例**：
  ```powershell
  pwsh -NoProfile -File scripts/build-qt-static.ps1 -Jobs 8
  ```

---

### 3.4 制品打包与正式发布

#### `release.ps1`
- **用途**：**一键全自动化正式发布总控脚本**。
- **执行内容**：
  1. 自动从 `CMakeLists.txt` 读取对外产品语义版本号（如 `0.2.2`）；
  2. 读取并自增 `.local/build_number` 内部构建号（例如 Build 5 -> Build 6）；
  3. 编译静态单 EXE 便携版，使用 `dumpbin /DEPENDENTS` 严格执行二进制白名单依赖审计；
  4. 编译动态 Release 构件，调用 `windeployqt` 精简收集依赖并打包为 zip；
  5. 自动计算所有发布构件的 SHA-256，生成 `checksums.txt`。
- **参数**：
  - `-BuildNumber <int>`：可选。手动指定本次构建的构建编号（默认自增）。
- **示例**：
  ```powershell
  # 默认自动递增构建号并打包发布
  pwsh -NoProfile -File scripts/release.ps1

  # 指定构建编号为 42 进行打包
  pwsh -NoProfile -File scripts/release.ps1 -BuildNumber 42
  ```
- **输出产物**（位于 `out/release/v<version>/`）：
  - `DanmaX-v<version>-windows-x64-portable.exe`
  - `DanmaX-v<version>-windows-x64.zip`
  - `checksums.txt`

#### `package.ps1`
- **用途**：单格式专用打包脚本。
- **参数**：
  - `-Format <static-exe|directory|single-exe>`：
    - `static-exe`：输出静态 EXE 及附带的完整重编译物料包（`out/packages/materials-static-*`，含匹配源码与许可证）；
    - `directory`：输出动态绿色安装目录；
    - `single-exe`：输出历史 CAB 自解压单文件。
- **示例**：
  ```powershell
  pwsh -NoProfile -File scripts/package.ps1 -Format static-exe
  pwsh -NoProfile -File scripts/package.ps1 -Format directory
  ```

---

### 3.5 自动化质量验证与基准测试

#### `validate-static.ps1`
- **用途**：**静态单 EXE 便携版的核心验收脚本**。
- **验收机制**：
  - 在隔离目录复制 EXE，以仅包含 Windows 系统目录的极简 PATH 启动；
  - 覆盖 cold（冷启动）、warm（热启动）、relocated-xml（中文路径移动后加载）、native-dark / native-light（系统深浅主题切换）、image-backend（图片渲染后端切换）、native-module-audit（模块审计）、blocked-cache（缓存受阻边界）等 **8 项核心测试**；
  - 严厉断言：启动及运行全过程**严禁向磁盘释放任何 `.dll`、`.qml`、`.rcc` 或可执行文件**。
- **示例**：
  ```powershell
  pwsh -NoProfile -File scripts/validate-static.ps1 -Executable out/release/v0.2.2/DanmaX-v0.2.2-windows-x64-portable.exe
  ```

#### `validate-ui.ps1`
- **用途**：FluentWinUI3 官方规范 UI 自动化回归验证。
- **参数**：
  - `-Executable <path>`：待测 EXE 路径。
  - `-All`：执行全部页面的全量视觉与交互回归。
  - `-Pages`：仅测试基础三大主页面（播放、日志、设置）。
  - `-ScrollPopup`：深度验证长下拉菜单弹出、页面实际滚动到底等动态交互。
- **示例**：
  ```powershell
  pwsh -NoProfile -File scripts/validate-ui.ps1 -Executable out/build/windows-release/bin/DanmaX.exe -All
  ```

#### `validate.ps1`
- **用途**：Release 动态版本的全功能烟雾与压力测试。
- **参数**：
  - `-Benchmark`：自动生成 500 与 2000 活动弹幕的合成压测负载，采样运行指标。
  - `-SessionId <id>`：可选。指定一个活动的 Windows SMTC 会话 ID，实机只读同步验证。
- **示例**：
  ```powershell
  # 基础页面与 XML 覆盖窗渲染烟雾测试
  pwsh -NoProfile -File scripts/validate.ps1

  # 附带弹幕渲染性能压测
  pwsh -NoProfile -File scripts/validate.ps1 -Benchmark
  ```

#### `validate-portable.ps1`
- **用途**：旧版 CAB 自解压启动器（`DanmaX.Portable.exe`）专项完整性验收。
- **示例**：
  ```powershell
  pwsh -NoProfile -File scripts/validate-portable.ps1 -Executable out/packages/DanmaX-single-<timestamp>/DanmaX.exe
  ```

#### `compare-renderers.ps1` 与 `compare-texture-budgets.ps1`
- **用途**：图形管线基准性能对比工具。
  - `compare-renderers.ps1`：横向比较 QSGTextNode 原生文本与 QImage 预光栅化图片渲染器在不同弹幕数量下的 CPU/GPU 开销；
  - `compare-texture-budgets.ps1`：评测 2K/4K 高 DPI 下文字纹理缓存命中率与显存预算（详见 `docs/TEXTURE_BUDGET_PERFORMANCE_20261008.md`）。
- **示例**：
  ```powershell
  pwsh -NoProfile -File scripts/compare-renderers.ps1 -Seconds 8
  pwsh -NoProfile -File scripts/compare-texture-budgets.ps1 -Xml tests/fixtures/sample.xml -Count 1000
  ```

---

### 3.6 底层支持库与专用资产工具

- **`common.ps1`**：项目最基础的公共逻辑库。提供 `Invoke-ProjectEnvironment`（临时沙盒）、`Initialize-Toolchain`（通过 `VsDevCmd.bat` 建立 MSVC 编译环境）、`Get-ToolPaths`（精确定位工具路径）及 `Get-ProjectPath`（项目边界安全路径解析）。
- **`generate-icon.ps1`**：构建期由 CMake 自动调用，利用 Windows 原生 `System.Drawing` 库将矢量图形光栅化为 16/32/48/256 像素的多层 `DanmaX.ico` 和应用启动图标 `DanmaX.png`。
- **`verify-fluent-assets.ps1`**：校验 `resources/fluent/` 内所用 Fluent 矢量图标与光栅切片的哈希与尺寸规范。

#### 历史选型评估与对比探测脚本群 (UI Library Probe Scripts)
位于 `tools/ui-library-probe/` 的配套调研脚本。在项目初期用于横向评估第三方 Rin、FluentUI-QML 与 Qt 官方 FluentWinUI3（详见 `docs/UI_LIBRARY_EVALUATION.md`）：
- **`prepare-ui-library-probe.ps1`**：下载并校验第三方探测所需源码与资产。
- **`probe-ui-libraries.ps1`**：编译并运行独立探测窗口，对比各第三方库在 Windows 上的阴影合成、内存与样式表现。
- **`deploy-fluent-probe.ps1`**：部署第三方库 probe 运行环境。
- **`prepare-fluentui.ps1`** / **`fluentui.ps1`**：第三方 FluentUI 源码准备与构建辅助。
> **注意**：当前正式产品已全面锁定 Qt 官方 FluentWinUI3 体系，正式构建与打包**无需运行任何 probe 脚本**。

---

## 4. 典型端到端工作流速查

### 工作流 A：新开发者从零上手
```powershell
# 1. 准备项目内依赖
pwsh -NoProfile -File scripts/bootstrap.ps1

# 2. 声明本机 MSVC 路径并诊断
cp toolchain/local.example.json .local/toolchain.local.json
# 编辑 .local/toolchain.local.json 中的 Visual Studio 安装路径后运行：
pwsh -NoProfile -File scripts/doctor.ps1

# 3. 编译并跑测试
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-debug -Test

# 4. 启动调试
pwsh -NoProfile -File scripts/run.ps1 -Preset windows-debug
```

### 工作流 B：官方版本正式打包与发布验收
```powershell
# 1. 执行全量发布（构建号自动 +1，并输出 EXE、ZIP 与哈希）
pwsh -NoProfile -File scripts/release.ps1

# 2. 对输出的静态便携版执行严格无释放与白名单验收
pwsh -NoProfile -File scripts/validate-static.ps1 -Executable out/release/v0.2.2/DanmaX-v0.2.2-windows-x64-portable.exe

# 3. 对界面执行多分辨率与浅深主题视觉自动化验收
pwsh -NoProfile -File scripts/validate-ui.ps1 -Executable out/release/v0.2.2/DanmaX-v0.2.2-windows-x64-portable.exe -All
```

---

## 5. 常见构建与运行排错指引 (Troubleshooting)

### Q1: 提示 `无法运行脚本，因为其包含适用于 Windows PowerShell 7.0 的 #requires 语句`
- **原因**：当前使用的是系统自带的 Windows PowerShell 5.1（`powershell.exe`）。
- **解决**：安装并使用 **PowerShell 7 (`pwsh.exe`)**。在终端中执行 `pwsh` 或在命令前加上 `pwsh -NoProfile -File ...`。

### Q2: 提示 `Missing VsDevCmd.bat` 或 `Configure the system MSVC/SDK exception in .local/toolchain.local.json`
- **原因**：项目采用零隐式 PATH 约束，必须显式声明系统 MSVC 实际安装位置。
- **解决**：
  1. 复制模板：`cp toolchain/local.example.json .local/toolchain.local.json`
  2. 编辑文件，将 `visualStudioPath` 指向本机实际的 Visual Studio 或 Build Tools 安装根目录（如 `C:/Program Files/Microsoft Visual Studio/2022/Community`）；
  3. 确认 `allowExternalSystemToolchain` 设为 `true`；
  4. 重新运行 `pwsh scripts/doctor.ps1` 校验。

### Q3: 运行 `release.ps1` 报错 `Static package imports disallowed runtime DLLs`
- **原因**：静态便携版强制执行 `dumpbin /DEPENDENTS` 依赖白名单审计，检测到了未授权的外部运行库 DLL（如 `Qt6Core.dll` 或 `MSVCP140.dll`）。
- **解决**：检查是否在配置中误用了动态 Qt，或链接了以 `/MD` 编译的第三方 C++ 库。便携版必须使用 `windows-static-release` 预设和内置的静态 Qt SDK（`/MT`）。

### Q4: 运行 `build-qt-static.ps1` 时内存不足或编译卡死
- **原因**：Qt 源码规模较大，并发核心数过多可能耗尽系统 RAM。
- **解决**：使用 `-Jobs` 参数降低并行任务数，例如：`pwsh scripts/build-qt-static.ps1 -Jobs 4`。
