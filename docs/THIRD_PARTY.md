# 第三方组件、上游开源项目与合规说明

DanmaX 是一款 Windows 平台下的原生高性能外挂弹幕工具，基于 C++20 与 Qt 6 Quick 构建，主程序采用 GNU General Public License v3.0 (GPL-3.0) 授权开源。

为满足开源合规、可复现构建及 LGPLv3 静态链接分发义务，本说明详述项目涉及的直接代码依赖、设计资产、网络服务生态、构建工具链及其各自的许可证适用范围、分发材料配套策略与边界隔离规则。

工具版本与下载校验见 `toolchain/dependencies.lock.json`，静态 Qt 源码清单与模块特性见 `toolchain/qt-static.lock.json`，QWindowKit 锁定见 `toolchain/qwindowkit.lock.json`，图标清单见 `resources/fluent/manifest.json`。

---

## 1. 上游开源项目与第三方组件全览

### 1.1 直接依赖库与框架

| 组件名称 | 版本 / Commit | 许可证 | 官方仓库 / 主页 | 引入方式 / 作用 | 分发形态 |
|---|---|---|---|---|---|
| **Qt 6 (Base, Declarative, Shader Tools)** | 6.11.0 | LGPL-3.0-only / GPL-3.0 | https://code.qt.io/cgit/qt/ | 核心 GUI、场景图硬件加速渲染、QML 引擎、流式 XML 解析与异步网络通信 | 静态编译内置于单 EXE，或动态 DLL 依赖 |
| **QWindowKit** | 1.5.0 (`267a002`) | Apache-2.0 | https://github.com/stdware/qwindowkit | Windows 11 沉浸式自绘标题栏、Snap Layouts 贴靠布局、系统阴影与 Mica 背景集成 | 静态编译内置于单 EXE |

### 1.2 视觉资产与图标

| 资产名称 | 锁定 Commit | 许可证 | 官方仓库 / 主页 | 作用说明 | 分发形态 |
|---|---|---|---|---|---|
| **Fluent System Icons** | `cd332ec` | MIT License | https://github.com/microsoft/fluentui-system-icons | 控制面板导航、设置项及吸顶快捷悬浮工具栏所使用的标准矢量图标 | 离线转换为高质量 PNG 资产并打包入二进制，随包提供原稿与许可证副本 |

### 1.3 网络服务、开放协议与弹幕生态

| 项目 / 规范名称 | 属性 / 作者 | 协议 / 许可证 | 项目主页 / 接口地址 | 关联方式与说明 |
|---|---|---|---|---|
| **uosc_danmaku** | 开源项目<br>作者：Tony15246 | GPL-3.0-or-later | https://github.com/Tony15246/uosc_danmaku<br>服务：`https://danmaku-api.152468.xyz` | **DanmaX 默认在线弹幕服务器由该项目提供支撑**。该服务兼容弹弹play 开放协议，为 DanmaX 客户端提供了开箱即用的动画弹幕检索与分P下载后端支持。DanmaX 不包含其后端源码，作为公开 API 客户端进行交互并致谢。 |
| **弹弹play 开放平台协议** | 开放 API 规范 | 平台公开规范 | https://api.dandanplay.net<br>https://www.dandanplay.com | DanmaX 客户端在线弹幕功能遵循并兼容弹弹play 接口协议定义，用于剧集名称搜索与弹幕分P下载。 |
| **Bilibili XML 弹幕格式** | 通用文件格式 | 开放数据格式 | - | 本地弹幕交换标准格式，DanmaX 针对其提供流式高性能容错解析支持。 |

### 1.4 开发与构建基础设施

| 工具 / 运行时 | 版本 | 许可证 | 作用说明 | 分发形态 |
|---|---|---|---|---|
| **Microsoft Windows SDK** | 10.0.26100.0 | Microsoft Software License | Windows C++/WinRT SMTC 媒体会话只读捕获、物理鼠标穿透窗口样式（`WS_EX_TRANSPARENT`）与高精度时钟 | 动态调用系统运行库，不随包分发 |
| **Microsoft MSVC Runtime** | 14.44.35207 | Visual Studio License | C++20 标准库与运行时底层支撑 | 静态 `/MT` 编入单 EXE，动态模式需 VC++ Redist |
| **CMake** | 3.31.6 | BSD-3-Clause | 跨平台项目构建配置生成器与 Presets 编排 | 仅本地与 CI 构建阶段使用，不随包分发 |
| **Ninja** | 1.12.1 | Apache-2.0 | 高性能底层并行构建工具 | 仅本地与 CI 构建阶段使用，不随包分发 |

---

## 2. 静态单 EXE 与开源合规体系

DanmaX 的默认发布格式为 **静态单 EXE**（`static-exe`），主程序直接嵌入编译好的 Qt 6.11 模块、QWindowKit 与 MSVC 运行库。

### 2.1 主程序开源协议 (GPL-3.0) 与组合衍生作品说明
- DanmaX 项目本身在 **GNU General Public License v3.0 (GPL-3.0)** 下开源。
- 当主程序以静态方式链接 LGPL-3.0 许可的 Qt 库和 Apache-2.0 许可的 QWindowKit 时，根据 GPL 与 LGPL 许可条款，分发的组合二进制作品受 GPL-3.0 条款管辖。
- 同时，本项目严格遵循 LGPLv3 针对静态链接衍生作品的相关规定，充分保障最终用户修改所使用的 LGPL 库（Qt 模块）并重新构建、重新链接的法定权利。

### 2.2 配套分发材料包（Materials Directory）
每次执行打包脚本 `pwsh -NoProfile -File scripts/package.ps1`，自动化流水线在生成主程序 `DanmaX.exe` 的同时，均会在同级目录自动创建配套分发材料目录 `out/packages/materials-static-<时间戳>/`：
1. **项目许可与版权**：包含本项目的 `LICENSE` (GPL-3.0) 与 `DISCLAIMER.md`。
2. **Qt 官方许可集**：包含静态编译所使用的 Qt 源码中完整的 `LICENSES/` 目录与各模块 REUSE 元数据。
3. **匹配的 Qt 源码归档**：包含经过 SHA-256 校验的官方 `qtbase`, `qtshadertools`, `qtdeclarative` 源码包。
4. **应用源码快照与 SBOM**：包含生成该 EXE 时当前工作区的完整源码与配置清单（自动排除 `.local/` 用户私有配置与历史归档）。
5. **静态链接符号表 (Map File)**：生成主程序的链接 Map 文件（如 `danmaku_app.map`），便于符号核验与重定位。
6. **离线重建指引与脚本**：提供脱机重新编译 Qt 与重新链接主程序的清晰指令与说明。

### 2.3 重新链接与修改权利保障
- 静态主程序未引入防篡改哈希、加壳或阻碍重新链接的二进制保护机制。
- 最终用户可基于随包提供的源码材料、CMake 预设以及锁定工具链清单，自行替换或修改 Qt 模块代码，并重新编译链接出功能等价的可执行文件。
- 公开发行二进制时，应连同对应时间戳的 `materials-static-<时间戳>` 材料目录（或提供可长期下载该材料的公开有效途径）一并向用户提供。

---

## 3. 三种发布格式的依赖与分发差异

| 发布格式 | 打包参数 | 运行库状态 | 外部系统要求 | 合规材料重点 |
|---|---|---|---|---|
| **静态单 EXE** (推荐) | `-Format static-exe` | Qt、QML 与 CRT 全部以 `/MT` 编入 EXE | 仅依赖 Windows 10/11 系统 DLL 与显卡驱动 | 必须保留对应的 `materials-static/` 源码与重编译说明 |
| **动态运行目录** | `-Format directory` | Qt 与 QML 作为独立 DLL/Plugins 输出在目录中 | 目标机需安装微软官方 VC++ x64 运行时 | 用户可直接物理替换目录中的 Qt DLL，保留许可说明 |
| **自解压单 EXE** (归档) | `-Format single-exe` | 启动器为原生 `/MT`，动态 Qt 主程序嵌入 CAB 资源包 | 目标机需安装微软官方 VC++ x64 运行时 | 运行时解压校验，若替换动态 DLL 会被启动器检测 |

---

## 4. 依赖获取与离线安全

1. **显式准备，零静默下载**：
   - 所有依赖的下载与校验均隔离在 `scripts/bootstrap.ps1` 与 `scripts/prepare-qt-static.ps1` 脚本中。
   - 配置（`CMake`）、构建（`Ninja`）和打包（`package.ps1`）阶段**严禁联网**。
2. **多重哈希锁定**：
   - 依赖下载后在解压前必须经过 SHA-256 校验；若哈希不匹配则立即中止并报错。
   - 官方 Qt 源码归档、CMake 二进制、Ninja 二进制均与上游官方校验清单逐一锚定。
3. **系统开发工具边界**：
   - MSVC 与 Windows SDK 作为明确声明的外部开发环境例外，仅从 `.local/toolchain.local.json` 显式指定的真实路径加载，严禁静默扫描其他项目目录或修改永久系统 PATH。

---

## 5. 资产与历史组件演进

### 5.1 Fluent System Icons（2026-10-08）
项目采用来自微软官方开源库 Microsoft Fluent System Icons 的矢量图标，协议为 MIT License。
- 锁定提交、原始 SVG 下载地址及 SVG/PNG SHA-256 位于 `resources/fluent/manifest.json`，完整版权与许可见 `resources/fluent/LICENSE`。
- 发布材料中完整包含 `Fluent-System-Icons-LICENSE.txt`。
- 为避免引入庞大的运行时 SVG 解析库（QtSvg），所有图标在构建前由离线脚本光栅化为 3× 高清 PNG（含浅深色差分），并在 QML 中统一使用官方 FluentWinUI3 语义着色。

### 5.2 历史 QML FluentUI 试用与清理
2026-10-08 项目经过评估，全面迁回编译期锁定的 Qt 官方 `QtQuick.Controls.FluentWinUI3` 样式。第三方 FluentUI 库已从正式构建、运行初始化和发布依赖中彻底移除；相关调研脚本与 lock 文件仅作为历史复现参考保留在 tools 归档中，不再参与任何产品构建。
