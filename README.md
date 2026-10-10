<p align="center">
  <img src="assets/icon.png" alt="DanmaX Logo" width="128" height="128" />
</p>

<h1 align="center">DanmaX</h1>

<p align="center">
  <strong>基于 C++20 与 Qt 6 Quick 的 Windows 高性能桌面弹幕覆盖层</strong><br>
  支持 Windows SMTC 媒体会话只读同步、本地 Bilibili XML 容错解析与弹弹play 在线弹幕检索
</p>

<p align="center">
  <a href="https://github.com/2CHariko/DanmaX/releases"><img src="https://img.shields.io/badge/Release-v0.2.3-blue.svg?style=flat-square" alt="Release: v0.2.3" /></a>
  <img src="https://img.shields.io/badge/Language-C%2B%2B20-blue.svg?style=flat-square" alt="C++20" />
  <img src="https://img.shields.io/badge/GUI-Qt%206.11%20Quick-41CD52.svg?style=flat-square" alt="Qt 6.11" />
  <img src="https://img.shields.io/badge/Platform-Windows%20x64-0078D6.svg?style=flat-square" alt="Windows x64" />
  <img src="https://img.shields.io/badge/Style-Qt_FluentWinUI3-005FB8.svg?style=flat-square" alt="Qt 官方 FluentWinUI3" />
  <img src="https://img.shields.io/badge/Package-Static%20Single--EXE-orange.svg?style=flat-square" alt="Static EXE" />
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPL--3.0-blue.svg?style=flat-square" alt="License: GPL-3.0" /></a>
</p>

---

## 💡 项目简介

**DanmaX**（`DanmaX.exe`）是一款专为 Windows 平台打造的轻量级、低开销桌面弹幕覆盖层工具。无论您是通过浏览器观看在线流媒体、还是使用 PotPlayer 等本地播放器，DanmaX 都能将弹幕以完全鼠标穿透、置顶且平滑的方式悬浮在视频上方。

应用底层采用纯 **C++20 领域核心** 调度与 **Qt Quick 现代场景图** 渲染，搭载自适应显存预算（Texture Budget）管理，兼顾超高刷新率（165Hz/240Hz）与低 CPU/GPU 占用。

---

## ✨ 核心特性

| 特性板块 | 核心能力与技术亮点 |
|---|---|
| ⚡ **极致性能与自适应渲染** | 纯 C++20 时间轴、轨道分配与有界对象池，单一帧驱动；搭载自适应文字纹理缓存（**Texture Budget**），动态扩缩显存（64 ~ 512 MiB），超限平滑回退矢量渲染，2K/4K 高刷丝滑无卡顿。 |
| 🎬 **SMTC 双模播放控制** | 深度集成 Windows SMTC（系统媒体传输控制），实时只读跟随 Chrome、Edge、PotPlayer 等播放器的进度、暂停与快进；亦支持纯脱机本地独立播放模式。 |
| 🌐 **本地与在线双弹幕源** | 异步流式解析 Bilibili XML，支持原始 C0 非法控制字符清洗与缺失颜色自动回退；深度集成弹弹play 兼容在线搜索，支持剧集分P检索、下载、多节点自动回退与长期隔离本地缓存。 |
| 🎨 **Fluent 界面设计** | Qt 官方 FluentWinUI3 标准控件和简单分组，保留系统标题栏和 Mica；响应式布局、系统/浅色/深色主题与字体前缀补全。完整高对比度及 Narrator 验收仍待完成。 |
| 📦 **真正的绿色单 EXE** | 默认采用 `/MT` 静态编译，Qt、QML 插件与 C++ 运行库完整编入单个 34 MiB 可执行文件；无运行时解压步骤，同级保存配置、日志与缓存，随拷随走。 |
| 📝 **透明的 UTF-8 INI 配置** | 首次运行自动生成带全中文注释的 `settings.ini`（共 21 项可调参数）；即时热生效，200 ms 防抖原子写盘，异常破损自动安全备份。 |

---

## 🏛️ 架构与数据流

DanmaX 遵循高内聚、低耦合的分层架构设计：

```text
┌────────────────────────────────────────────────────────┐
│               QML UI (FluentWinUI3 Controls)           │
│       [PlayerPage]      [SettingsPage]      [LogsPage] │
└───────────────────────────┬────────────────────────────┘
                            │ 交互操作 / 状态绑定
┌───────────────────────────▼────────────────────────────┐
│         Application Layer (AppController / Library)    │
│  - 状态编排 (Idle/Playing/Paused)   - 多服务顺序回退   │
│  - INI 原子存储 (SettingsStore)     - 异步 XML/在线流  │
└──────────────┬────────────────────────────┬────────────┘
               │ 媒体快照 / 采样推进        │ 时间轴调度
┌──────────────▼─────────────┐ ┌────────────▼────────────┐
│   Platform Adapter (WinRT) │ │   C++20 Core (danmaku)  │
│  - SMTC 会话发现与时间戳采样│ │  - PlaybackClock (时钟)│
│  - 窗口前台智能关联与置顶  │ │  - 物理轨道与防重叠避让 │
│  - 鼠标穿透 (WS_EX_TRANSP) │ │  - 有界活跃对象池       │
└────────────────────────────┘ └────────────┬────────────┘
                                            │ 紧凑快照移交
┌───────────────────────────────────────────▼────────────┐
│             Renderer (Qt Quick Scene Graph)            │
│  - 整条文字图片栅格化与图集缓存 (QSGSimpleTextureNode) │
│  - 自适应显存预算管控 (TextureBudget: 64 ~ 512 MiB)    │
│  - 超限平滑回退文字矢量节点 (QSGTextNode)              │
└────────────────────────────────────────────────────────┘
```

---

## 🚀 快速开始

### 最终用户 (End Users)
1. 从 [Releases 页面](https://github.com/2CHariko/DanmaX/releases) 下载最新版本的发布文件：
   - **`DanmaX-v0.2.3-windows-x64.zip`**：带外壳文件夹的免安装静态单文件便携压缩包，解压后直接运行 `DanmaX.exe`。
   - **`DanmaX-v0.2.3-windows-x64-portable.exe`**：无需安装、无需解压、纯静态单 EXE 便携版，直接双击运行。
2. 运行后，程序将在同级目录生成全中文注释的 `settings.ini` 配置文件与日志。
3. **加载弹幕**：
   - *本地文件*：在“播放”页切换到“本地 XML”，点击“浏览文件...”选择弹幕后点击“加载弹幕”。
   - *在线搜索*：切换到“在线搜索”，输入作品名称检索，选中对应分P后点击“下载并载入”。
4. **开始播放**：
   - 配合浏览器或播放器使用时，选择“跟随系统播放器”，下拉框选择你的播放器后点击“开始同步”。
   - 单独播放时，选择“本地独立播放”并点击“开始播放”。
5. 详细实操手册与常见问题请查阅 **[用户指南 (docs/USER_GUIDE.md)](docs/USER_GUIDE.md)**。

### 开发者 (Developers)
编译与开发需使用 Windows 10/11 x64、PowerShell 7 (`pwsh`)、MSVC 2022 及 Windows SDK。

```powershell
# 1. 自动准备项目内专用的 Qt 6.11、CMake 与 Ninja（仅首次需执行联网下载）
pwsh -NoProfile -File scripts/bootstrap.ps1

# 2. 声明本机 MSVC 工具链路径（复制示例配置并调整实际目录）
cp toolchain/local.example.json .local/toolchain.local.json

# 3. 诊断工具链就绪情况
pwsh -NoProfile -File scripts/doctor.ps1

# 4. 构建 Debug 版本并运行全部 13 项自动化测试
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-debug -Test

# 5. 启动应用
pwsh -NoProfile -File scripts/run.ps1 -Preset windows-debug
```

打包与静态分发详见 **[贡献指南 (CONTRIBUTING.md)](CONTRIBUTING.md)** 与 **[打包说明 (docs/PACKAGING.md)](docs/PACKAGING.md)**。

---

## 📚 完整文档矩阵

项目文档均收录在 [`docs/`](docs/) 目录中，可直接查阅各专题文档：

| 文档名称 | 核心内容概要 |
|---|---|
| 📘 **[用户实操指南](docs/USER_GUIDE.md)** | 新手上路、便携目录结构、双模播放技巧、外观微调与常见问题 FAQ |
| ⚙️ **[配置文件手册](docs/CONFIGURATION.md)** | `settings.ini` 21 项全量参数速查字典、字段类型、合法区间与手工编辑规范 |
| 📄 **[默认配置模板](docs/settings.example.ini)** | 首次启动生成的完整 UTF-8 INI 配置文件模板与中文注释 |
| 🏗️ **[C++ / Qt 架构设计](docs/CPP_QT_ARCHITECTURE.md)** | 系统设计原则、分层边界、线程安全、帧时钟模型与系统隔离规则 |
| 🎨 **[UI 设计系统规范](docs/UI_DESIGN_SYSTEM.md)** | Qt 官方 FluentWinUI3 默认控件与平台适配约束、4 单位布局节奏、无障碍与下拉弹出层边界 |
| 📦 **[Windows 便携分发](docs/PACKAGING.md)** | 静态单 EXE 架构、依赖精简、配套重编译材料及三种分发格式区别 |
| 🛠️ **[开发与构建脚本手册](docs/SCRIPTS.md)** | 全量 24 个 PowerShell 脚本参数手册、环境准备、静态 SDK 编译、发布打包与基准测试 |
| ⚖️ **[第三方组件与合规说明](docs/THIRD_PARTY.md)** | 第三方许可证清单、LGPLv3 静态分发合规流水线与重新链接保障 |
| 📊 **[交付状态与实机证据](docs/FRAMEWORK_STATUS.md)** | 历史里程碑演进记录、全量 CTest 覆盖率与真机实测数据链 |
| 📈 **[文字显存预算优化记录](docs/TEXTURE_BUDGET_PERFORMANCE_20261008.md)** | 2K/4K 高 DPI 下固定 64 MiB 与自适应 512 MiB 预算的基准对照 |
| 📈 **[渲染引擎优化记录](docs/RENDERER_PERFORMANCE_20261007.md)** | 矢量渲染与场景图纹理缓存的帧率与 CPU 负载实测报告 |
| 🎨 **[应用图标与替换指南](assets/README.md)** | 应用多尺寸 Windows ICO 与窗口图标自动化生成说明 |

---

## ⚖️ 开源协议与声明

- 本项目采用 **[MIT License](LICENSE)** 授权开源。
- 使用本项目前请仔细阅读 **[免责声明与使用条款 (DISCLAIMER.md)](DISCLAIMER.md)**。
- 本项目严格履行 LGPLv3 静态分发合规义务，分发包配套输出源码、SBOM 与符号表，详见 **[第三方组件说明 (docs/THIRD_PARTY.md)](docs/THIRD_PARTY.md)**。
- 历史 Python 原型及早期计划已完整归档至独立的 **[archive 分支](https://github.com/2CHariko/DanmaX/tree/archive/archive)**，不参与当前主线构建。
