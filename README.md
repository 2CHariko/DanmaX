<p align="center">
  <img src="assets/icon-rounded.png" alt="DanmaX Logo" width="128" height="128" />
</p>

<h1 align="center">DanmaX</h1>

<p align="center">
  <strong>Windows平台下的原生高性能外挂弹幕工具</strong><br>
  基于 C++20 与 Qt 6 Quick 构建，专为本地播放器与在线流媒体打造的轻量级、低开销桌面悬浮弹幕系统
</p>

<p align="center">
  <a href="https://github.com/2CHariko/DanmaX/releases"><img src="https://img.shields.io/badge/Release-v0.2.4-blue.svg?style=flat-square" alt="Release: v0.2.4" /></a>
  <img src="https://img.shields.io/badge/Language-C%2B%2B20-blue.svg?style=flat-square" alt="C++20" />
  <img src="https://img.shields.io/badge/GUI-Qt%206.11%20Quick-41CD52.svg?style=flat-square" alt="Qt 6.11" />
  <img src="https://img.shields.io/badge/Platform-Windows%20x64-0078D6.svg?style=flat-square" alt="Windows x64" />
  <img src="https://img.shields.io/badge/Style-Qt_FluentWinUI3-005FB8.svg?style=flat-square" alt="Qt 官方 FluentWinUI3" />
  <img src="https://img.shields.io/badge/Package-Static%20Single--EXE-orange.svg?style=flat-square" alt="Static EXE" />
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPL--3.0-blue.svg?style=flat-square" alt="License: GPL-3.0" /></a>
</p>

---

## 项目简介

**DanmaX** 是一款专为 Windows 平台打造的原生高性能外挂弹幕工具。它能够将弹幕以完全鼠标穿透、自适应刷新率、低系统资源开销的方式，悬浮置顶在任意本地视频播放器（如 PotPlayer、MPC-HC、VLC、mpv 等）或网页浏览器之上。

### 解决的核心痛点

1. **本地观影缺少弹幕互动**：当用户在本地观看收藏的高画质动漫、电影或剧集（如 BDRip）时，传统本地播放器通常缺乏原生弹幕支持。DanmaX 提供免重压制、免内嵌字幕的独立外挂弹幕解决方案。
2. **传统外挂弹幕工具开销大、易掉帧**：基于 Electron 或网页套壳的弹幕工具常伴随较高的 CPU 与内存消耗，在高刷新率（165Hz/240Hz）或 4K 屏幕上容易出现卡顿或撕裂。DanmaX 底层采用纯 C++20 物理调度与 Qt Quick 场景图硬件加速，兼具丝滑流畅度与极低能耗。
3. **播放状态同步繁琐**：无需在播放器中安装侵入式插件或注入 DLL。DanmaX 原生接入 Windows SMTC（系统媒体传输控制）接口，以纯只读非侵入方式自动捕获外部播放器的播放、暂停与时间进度跳转。
4. **窗口遮挡与鼠标交互冲突**：全屏弹幕覆盖层原生应用 `WS_EX_TRANSPARENT`，保持 100% 物理点击穿透；控制工具栏则采用物理双窗口架构与吸顶自动隐藏设计，既保障全屏操作无干扰，又能在需要时随时调节时间轴偏移、透明度与字号。

---

## 核心特性

| 特性模块 | 技术实现与功能亮点 |
|---|---|
| 原生高性能与自适应显存预算 | 纯 C++20 领域核心调度，单一单调时钟帧驱动；采用自适应文字纹理缓存（Texture Budget），动态管理 64 ~ 512 MiB 显存，超限平滑回退矢量渲染，轻松支持高密度同屏弹幕与高刷新率屏幕。 |
| Windows SMTC 智能媒体跟随 | 深度接入 Windows 原生 SMTC 媒体会话，实时只读捕获 Edge、Chrome、PotPlayer、VLC 等主流应用的播放状态；同时支持完全脱机的本地独立时间轴播放模式。 |
| 本地与在线双弹幕生态 | 支持流式加载 Bilibili XML 本地弹幕文件，自带非法控制字符清洗与缺色容错；开箱即用支持在线弹幕检索与选集下载，并持久化隔离在本地便携缓存目录中。 |
| 双窗口解耦与 Fluent 沉浸交互 | 弹幕覆盖层与快捷控制栏物理分离，保障全屏 100% 物理点击穿透；主控制面板采用 Qt 官方 FluentWinUI3 原生控件，支持 Windows 11 Mica 材质、系统深浅色自适应与原生 Snap Layouts 贴靠布局。 |
| 吸顶快捷悬浮工具栏 | 采用独立原生无激活悬浮窗口，平时自动收起无感知；鼠标移至屏幕顶部中央平滑展开，支持实时进度拖拽、微调时间轴偏移、字号缩放、不透明度调节与一键清屏。 |
| 真正的绿色便携单 EXE | 基于 MSVC `/MT` 静态编译，Qt、QML 插件与 C++ 运行库完整内置于约 34 MiB 的单个可执行文件中；运行时无解压临时目录，同级自动创建并维护配置文件、日志与弹幕缓存。 |
| 透明的 UTF-8 INI 配置管理 | 首次启动自动生成全中文注释的 `settings.ini` 配置文件（包含 21 项详尽可调参数）；支持运行时防抖原子持久化与内存热生效，配置文件破损自动安全备份。 |

---

## 系统架构与数据流

DanmaX 遵循严格的高内聚、低耦合分层架构设计，各层职责边界清晰明确：

```text
+--------------------------------------------------------+
|               QML UI (FluentWinUI3 Controls)           |
|       [PlayerPage]      [SettingsPage]      [LogsPage] |
+---------------------------+----------------------------+
                            | 状态绑定 / 用户操作
+---------------------------v----------------------------+
|         Application Layer (AppController / Library)    |
|  - 状态编排 (Idle/Playing/Paused)   - 多服务顺序回退   |
|  - INI 原子存储 (SettingsStore)     - 异步 XML/在线流  |
+---------------------+----------------------+-----------+
                      | 媒体快照 / 采样推进  | 紧凑时钟帧
+---------------------v-------+  +-----------v-----------+
|   Platform Adapter (WinRT)  |  |  C++20 Core (danmaku) |
|  - SMTC 会话只读发现与时间戳|  |  - PlaybackClock      |
|  - 前台窗口匹配与层级协调   |  |  - 物理轨道防重叠算法 |
|  - 物理鼠标穿透/透明属性    |  |  - 有界活跃对象池     |
+-----------------------------+  +-----------+-----------+
                                             | 活跃弹幕快照移交
+--------------------------------------------v-----------+
|             Renderer (Qt Quick Scene Graph)            |
|  - 整条文字栅格化与图集缓存 (QSGSimpleTextureNode)     |
|  - 自适应显存预算管控 (TextureBudget: 64 ~ 512 MiB)    |
|  - 超限平滑回退文字矢量节点 (QSGTextNode)              |
+--------------------------------------------------------+
```

### 架构设计原则

1. **领域核心纯粹性**：`src/core/` 领域核心逻辑仅依赖 C++20 标准库，不引入任何 Qt、QML 或 Windows API 依赖，具备完整的平台独立性与确定性单元测试能力。
2. **生命周期与线程安全**：GUI 与 Qt Quick 渲染生命周期严格解耦，图形硬件资源仅在场景图更新阶段由渲染线程管理，严禁跨线程操作 QML 对象。
3. **零外部污染**：所有开发依赖、本地配置、运行缓存与构建产物均严格隔离在项目与便携目录之内，不修改系统注册表或永久系统环境变量。

---

## 典型使用场景

### 场景一：配合本地播放器观看动漫/电影

1. 打开 PotPlayer、MPC-HC、mpv 等播放器并载入视频。
2. 启动 `DanmaX.exe`，在主面板“播放”页中：
   - 若已有本地弹幕，选择“本地 XML”并载入文件；
   - 若无本地弹幕，选择“在线搜索”，输入作品名称后在分P列表中点击“下载并载入”。
3. 播放模式选择“跟随系统播放器”，在会话下拉列表中选中当前播放器并点击“开始同步”。
4. 将播放器全屏播放即可享受丝滑弹幕体验；需要微调弹幕同步时，可将鼠标移至屏幕顶部唤出快捷工具栏调整“时间偏移”。

### 场景二：配合网页或在线流媒体

1. 使用 Edge、Chrome、Firefox 等浏览器播放流媒体视频。
2. 启动 `DanmaX.exe`，通过“在线搜索”下载对应剧集弹幕。
3. 播放模式选择“跟随系统播放器”并选中对应浏览器标签页会话，点击“开始同步”，弹幕即自动贴合网页播放进度。

### 场景三：脱机独立演示或无 SMTC 环境

1. 在主面板中载入本地或在线弹幕。
2. 播放模式选择“本地独立播放”，点击“开始播放”，由 DanmaX 自身高精度时钟独立驱动弹幕漫游。

---

## 快速上手与便携运行

### 获取发布包

前往 [Releases 页面](https://github.com/2CHariko/DanmaX/releases) 下载最新发行文件：
- **`DanmaX-v0.2.4-windows-x64-portable.exe`**：推荐使用。无需解压、无需安装的纯静态单 EXE 便携版，直接双击运行。
- **`DanmaX-v0.2.4-windows-x64.zip`**：外壳便携压缩包，解压后直接运行目录中的 `DanmaX.exe`。

### 便携目录结构

运行后，程序将在同级目录下维护所有配置与数据，不向系统 AppData 或注册表写入数据：

```text
DanmaX/
├── DanmaX.exe          # 主程序（静态单文件，约 34 MiB）
├── settings.ini         # 配置文件（纯 UTF-8，自带完整中文说明）
├── app.log              # 运行日志（单文件 2 MiB 自动轮转）
└── cache/              # 运行缓存目录
    ├── danmaku/        # 在线搜索下载的弹幕缓存（离线可重复使用）
    └── tmp/            # 临时渲染与解析缓存
```

---

## 上游开源项目与致谢

DanmaX 的构建与良好体验离不开以下优秀开源项目、平台服务及标准的支撑，在此表达诚挚的敬意与感谢：

### 核心框架与底层引擎

- **[Qt 6](https://www.qt.io/) (The Qt Company / Qt Project)**
  - 许可证：LGPL-3.0-only / GPL-3.0
  - 作用：为 DanmaX 提供跨组件 GUI 基础、Qt Quick 场景图硬件加速渲染体系（QSGSimpleTextureNode / QSGTextNode）、流式 XML 容错解析引擎与异步网络通讯底层。

### 原生窗口与系统适配

- **[QWindowKit](https://github.com/stdware/qwindowkit) (stdware)**
  - 许可证：Apache-2.0
  - 作用：跨平台无边框窗口解决方案，在 Windows 11 下实现原生沉浸式自绘标题栏、系统阴影、Snap Layouts 贴靠布局及 Mica 背景材质集成。

### 视觉设计与图标资产

- **[Fluent System Icons](https://github.com/microsoft/fluentui-system-icons) (Microsoft Corporation)**
  - 许可证：MIT License
  - 作用：控制面板、导航栏与快捷悬浮工具栏所采用的统一现代 Fluent 风格矢量图标，经离线转换为高质量 PNG 资产并保留原始 SVG 与 SHA-256 锁定清单。

### 网络服务与开放协议生态

- **[uosc_danmaku](https://github.com/Tony15246/uosc_danmaku) (Tony15246)**
  - 项目作者：Tony15246
  - 许可证：GPL-3.0-or-later
  - 作用与致谢：**DanmaX 默认在线弹幕服务器（`https://danmaku-api.152468.xyz`）由该项目提供支撑**。该服务兼容弹弹play 协议，为 DanmaX 提供了开箱即用的在线弹幕查询与分P下载后端支持。在此对作者 Tony15246 及其开源项目的公共服务支持表示由衷感谢。
- **[弹弹play (Dandanplay)](https://www.dandanplay.com/) 开放平台协议**
  - 作用：DanmaX 在线弹幕检索与下载功能遵循并兼容弹弹play 公开 API 规范，实现动画作品关联与分集弹幕检索。
- **Bilibili XML 弹幕格式**
  - 作用：业界通用的弹幕数据交换格式，DanmaX 对其提供流式高性能容错解析支持。

### 构建与系统开发基础设施

- **[CMake](https://cmake.org/) (Kitware, Inc.)**：BSD-3-Clause，现代 C++ 构建配置与 CMake Presets 管理。
- **[Ninja](https://ninja-build.org/) (ninja-build contributors)**：Apache-2.0，高效并行构建底层工具。
- **Microsoft Windows SDK & C++/WinRT**：Windows 原生 SMTC 媒体会话只读监听、高精度时钟与扩展窗口样式支持。

详细第三方许可证说明与合规体系请参阅 **[第三方组件说明 (docs/THIRD_PARTY.md)](docs/THIRD_PARTY.md)**。

---

## 完整文档导航

更多进阶用法、架构细节与工程规范请查阅 [`docs/`](docs/) 目录下的专题文档：

| 文档名称 | 核心内容概要 |
|---|---|
| [用户实操指南 (docs/USER_GUIDE.md)](docs/USER_GUIDE.md) | 新手入门、便携目录结构、双模播放技巧、外观微调与常见问题 FAQ |
| [配置文件手册 (docs/CONFIGURATION.md)](docs/CONFIGURATION.md) | `settings.ini` 21 项全量参数速查字典、字段类型、合法区间与手工编辑规范 |
| [默认配置模板 (docs/settings.example.ini)](docs/settings.example.ini) | 首次启动生成的完整 UTF-8 INI 配置文件模板与中文注释 |
| [C++ / Qt 架构设计 (docs/CPP_QT_ARCHITECTURE.md)](docs/CPP_QT_ARCHITECTURE.md) | 系统设计原则、分层边界、线程安全、帧时钟模型与系统隔离规则 |
| [UI 设计系统规范 (docs/UI_DESIGN_SYSTEM.md)](docs/UI_DESIGN_SYSTEM.md) | Qt 官方 FluentWinUI3 默认控件与平台适配约束、4 单位布局节奏与下拉弹出层边界 |
| [Windows 便携分发 (docs/PACKAGING.md)](docs/PACKAGING.md) | 静态单 EXE 架构、依赖精简、配套重编译材料及三种分发格式区别 |
| [第三方组件与合规说明 (docs/THIRD_PARTY.md)](docs/THIRD_PARTY.md) | 第三方许可证清单、LGPLv3 静态分发合规流水线与重新链接保障 |
| [开发与构建脚本手册 (docs/SCRIPTS.md)](docs/SCRIPTS.md) | 全量 24 个 PowerShell 脚本参数手册、环境准备、静态 SDK 编译与基准测试 |
| [交付状态与实机证据 (docs/FRAMEWORK_STATUS.md)](docs/FRAMEWORK_STATUS.md) | 历史里程碑演进记录、全量 CTest 覆盖率与真机实测数据链 |
| [开发贡献指南 (CONTRIBUTING.md)](CONTRIBUTING.md) | 源码编译环境搭建、本地调试、自动化测试与 PR 提交流程 |

---

## 开源协议与声明

- 本项目主程序及源代码采用 **[GNU General Public License v3.0 (GPL-3.0)](LICENSE)** 授权开源。
- 使用本项目前请仔细阅读 **[免责声明与使用条款 (DISCLAIMER.md)](DISCLAIMER.md)**。
- 本项目严格履行 LGPLv3 针对静态分发的合规义务，静态发行包配套提供源码快照、SBOM、符号表与脱机重新链接指引，详见 **[第三方组件说明 (docs/THIRD_PARTY.md)](docs/THIRD_PARTY.md)**。
- 历史 Python 原型及早期计划文档已完整归档至独立的 **[archive 分支](https://github.com/2CHariko/DanmaX/tree/archive/archive)**，不参与主线代码构建。
