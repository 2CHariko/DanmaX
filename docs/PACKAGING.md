# Windows 便携分发

默认格式为 `static-exe`：Qt 6.11.0、C++ 运行库、QML、Qt 静态插件和界面资源编入主程序，分发入口为一个 `LocalDanmaku.exe`。没有启动器、子主程序或运行库解压步骤。用户允许在同级目录保存配置、日志、缓存和临时文件，因此保留正常的原子配置保存、QML/图形缓存，不承诺运行时完全无文件写入。

常规 Debug/Release 继续使用动态 Qt SDK；静态 SDK、构建预设与验收目录独立。静态模式是本次用户明确授权的分发优化，不是对日常开发默认链接方式的改变。

## 准备、构建和打包

```powershell
# 常规工具与显式 MSVC/SDK 声明按 README 准备
pwsh -NoProfile -File scripts/bootstrap.ps1

# 联网准备：固定版本的 Qt 官方源码，SHA-256 校验后才解压
pwsh -NoProfile -File scripts/prepare-qt-static.ps1
# 下载包齐备时改为 prepare-qt-static.ps1 -Offline

# 以下步骤不联网；第一次编译 Qt 耗时较长
pwsh -NoProfile -File scripts/build-qt-static.ps1 -Jobs 8
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-static-release -Test
pwsh -NoProfile -File scripts/package.ps1

# 从任意工作目录调用仍根据脚本位置定位项目
pwsh -NoProfile -File scripts/validate-static.ps1 -Executable out/packages/LocalDanmaku-static-<时间戳>/LocalDanmaku.exe
```

`toolchain/qt-static.lock.json` 固定 Qt 源码 URL、SHA-256、模块、许可证定位及特性。仅编译 qtbase、qtshadertools、qtdeclarative；开发 SDK 位于 `.tools/qt/6.11.0/msvc2022_64-static`，源码位于 `.cache/sources/qt-6.11.0-static`，qtbase/qtshadertools 构建位于 `out/build/qt-6.11.0-static/`，Quick/QML 为避免 MSVC 长路径限制使用 `out/build/qs/qml/`。工具版本沿用 `dependencies.lock.json`，编译器/Windows SDK 仅取自 `.local/toolchain.local.json` 明确声明的系统安装；不搜索或静默回退到其他 Qt。

构建记录源码、安装路径和编译器身份。移动项目、改变工具链或更换 SDK 后必须重新配置到新的构建目录，不能搬用含旧绝对路径的 CMake 缓存。`-Reconfigure` 仅允许同路径、同编译器的显式特性重配置；不会自动删除旧产物或用户配置。

默认输出 `out/packages/LocalDanmaku-static-<时间戳>/LocalDanmaku.exe`，应用源码、匹配的 Qt 源码归档、许可证、重新编译说明、实际 DLL 导入表和包摘要放在独立的 `out/packages/materials-static-<时间戳>/`。每次创建新目录，不覆盖已有包。只运行程序时可单独移动 EXE；公开分发时还须按实际组件许可证提供对应材料。

## 静态包运行目录

```text
LocalDanmaku.exe        主程序，Qt/QML/插件嵌入其中
settings.ini          带详细中文注释的用户配置，首次运行创建
settings.ini.backup-*  损坏配置的保留备份，仅需要时出现
app.log                应用日志
app.log.1              日志轮转备份，仅需要时出现
cache/                 QML、图形管线等运行缓存
  tmp/                 本进程 TEMP/TMP；临时保存文件可自动删除
```

在创建 QGuiApplication 前设置进程自己的 TEMP/TMP、QML 缓存位置；QSaveFile 的原子保存临时文件位于配置文件所在目录。配置与日志默认直接在 EXE 旁；开发 `run.ps1` 显式指定 `.local/` 数据和项目缓存。`--data-dir`、`--cache-dir` 可分别覆盖，相对路径按调用者工作目录解释，相对 XML 路径也保留这一语义。数据位置不可写时失败，不静默退回 AppData，不修改永久 PATH，不安装 Qt，不释放 DLL。

携带配置时同时移动 `settings.ini`；需要保留日志可一起移动 `app.log*`。`cache/` 可在全部实例退出后自行删除并重新生成。多实例共享默认设置位置，尚未增加跨实例配置写入协调。

`settings.ini` 使用 UTF-8，每个字段自带中文用途、默认值、范围和单位说明。此版本不读取或迁移旧 JSON/INI；手动编辑前退出所有实例，重新启动后读取。程序保存时重建固定说明，不保留额外注释和未知字段。详见 [配置说明](CONFIGURATION.md) 与 [完整默认模板](settings.example.ini)。

## 依赖精简

Qt 与主程序使用 MSVC `/MT`；Qt 平台插件、实际 QML 模块、FluentWinUI3 和官方 Fusion/Basic 回退静态链接。QML 目标在 `qml/` 下创建，让 Qt 的导入扫描只遍历应用界面，避免收集 `.cache/` 中 Qt 自身的示例或测试依赖。其他 Controls 风格在 Qt 源码配置阶段关闭，保留标准文件对话框依赖。

Qt 默认 C++17 的 Windows 适配文件通过项目 CMake 钩子使用 MSVC `/await:strict`，与应用 C++20 的 C++/WinRT 标准协程 ABI 对齐；相关文件跳过不兼容的 C++17 预编译头。Qt 官方源码不打补丁，不压制 ABI 检查。依据为 [MSVC 协程支持](https://learn.microsoft.com/en-us/cpp/build/reference/await-enable-coroutine-support?view=msvc-170)。应用静态插件导入排除未使用的 generic、networkinformation 和 TLS，与动态部署的精简范围一致。

关闭 OpenGL、Vulkan、ICU、OpenSSL、DBus、Widgets、SQL 和 PrintSupport，使用 Qt 公开 D3D11 渲染后端。Qt 6.11 在 Windows 的 Gui 库仍编译 D3D12 相关代码，本项目选择 D3D11，不声称 D3D12 源码已移除。Qt D3D11 使用 Windows 系统的 D3DCompiler，静态包不携带 DXC/DXIL、Qt DLL 或 VC++ DLL。

打包用 MSVC `dumpbin /DEPENDENTS` 检查实际产物，拒绝 Qt、MSVCP、VCRUNTIME、CONCRT、DXC/DXIL 的外部导入。Windows 系统 DLL、系统字体和图形驱动仍属于平台依赖。静态链接不意味着程序摆脱操作系统，也不意味着驱动和 Windows 自身的缓存都能由应用控制。

本机实际导入包含 Windows 自带的 `icu.dll` 和 `d3d12.dll`；Qt 的外部 ICU 功能关闭并不等于 Windows API 不使用系统 ICU，选择 D3D11 也不等于 Qt Gui 的 D3D12 系统导入消失。系统库不复制或从包解压。原生模块清单可能出现输入法、性能叠加层等本机注入 DLL，不能据此认定它们是应用需要分发的依赖。

## 其他格式

```powershell
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-release -Test
pwsh -NoProfile -File scripts/package.ps1 -Format directory
pwsh -NoProfile -File scripts/package.ps1 -Format single-exe
```

`directory` 使用锁定动态 Qt 的 `windeployqt`，输出 `out/stage/LocalDanmaku-<时间戳>/`，适合调试或替换 Qt DLL。`single-exe` 是保留的旧 CAB 自解压格式：启动器静态链接自己的 CRT，动态 Qt 主程序解压至同级 `data/runtime/<包摘要前24位>/`；配置、日志和缓存在 `data/`。它逐文件校验并复用/修复运行库，损坏目录保留为 `.invalid-*`，不删除用户配置。开发阶段两种动态格式仍需要官方 VC++ x64 运行库，脚本不自动安装。

动态部署移除未使用的 Controls 风格、QML 调试、TUIO、网络信息及 TLS 插件，保留 Fluent/Fusion/Basic、Windows/offscreen 和必要图像、图形依赖。自解压使用明确定位的 Windows makecab/Cabinet/BCrypt，不引入第三方压缩库。`validate-portable.ps1` 用于旧自解压格式，不能替代静态包验收。

## 验证与分发边界

`validate-static.ps1` 在新建的项目内目录复制唯一 EXE，使用仅系统目录的 PATH 和故意无效的 Qt/QML 路径启动。检查冷/热启动、配置保留、中文空格路径搬迁、相对 XML 加载、原生浅/深 Fluent 与覆盖窗、实验图片渲染、原生进程已加载模块，以及缓存路径受阻时的失败行为。记录启动期间文件创建和退出后目录清单，禁止 DLL/EXE/QML/RCC/CAB/QSB 运行库释放。报告、导入表和截图位于 `out/validation/static-<时间戳>/`；可选 `-SessionId` 只读验证已有真实 SMTC 会话。

实测结果见 [交付状态](FRAMEWORK_STATUS.md)。干净 Windows 机器、真实穿透/混合 DPI/高对比度/Narrator 和长时间压力的未验收范围保持；本机测试不扩大这些结论。

静态 Qt 的公开发行仍需逐项核对实际模块与第三方代码许可、通知、对应源码和允许修改 Qt 后重新编译/链接的材料。包旁独立材料已经保留匹配 Qt 源码和本次应用源码、锁定清单及重建脚本，静态 EXE 没有阻止重新链接的 Qt 哈希检查；这些文件不等同于完成全部许可审查。详见 [第三方组件](THIRD_PARTY.md)。

依据：[Qt Windows 部署](https://doc.qt.io/qt-6/windows-deployment.html)、[Qt QML 静态插件](https://doc.qt.io/qt-6/qt-import-qml-plugins.html)、[Qt 官方许可说明](https://www.qt.io/licensing/open-source-lgpl-obligations)。
