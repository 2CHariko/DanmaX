# 第三方组件与开源分发合规说明

本项目使用 C++20 与 Qt 6 Quick 构建。为满足开源合规、可复现构建及 LGPLv3 静态分发义务，本说明详述项目涉及的第三方组件、许可证适用范围、分发材料配套策略及构建依赖隔离边界。

工具版本与下载校验见 `toolchain/dependencies.lock.json`，静态 Qt 源码清单与模块特性见 `toolchain/qt-static.lock.json`。

---

## 1. 第三方组件与许可证清单

| 组件名称 | 版本 | 许可证 | 引入方式 / 作用 | 分发形态 |
|---|---|---|---|---|
| **Qt 6 (Base, Declarative, Shader Tools)** | 6.11.0 | LGPL-3.0-only / GPL-3.0 / Commercial | 核心 GUI、场景图渲染、QML 引擎、流式 XML 与网络 | 静态编入单 EXE 或动态 DLL 依赖 |
| **QWindowKit** | 1.5.0 | Apache-2.0 | Windows 11 沉浸式标题栏、Snap Layouts 贴靠与无边框支持 | 静态编入单 EXE |
| **Microsoft Windows SDK** | 10.0.26100.0 | Microsoft Software License Terms | C++/WinRT SMTC 媒体会话、系统窗口 API | 动态调用系统运行库，不随包分发 |
| **Microsoft MSVC Runtime** | 14.44.35207 | Visual Studio License | C++ 标准库与运行时支撑 | 静态 `/MT` 编入单 EXE，动态模式需 VC++ Redist |
| **CMake** | 3.31.6 | BSD-3-Clause | 项目构建配置生成器 | 仅开发构建使用，不随包分发 |
| **Ninja** | 1.12.1 | Apache-2.0 | 高性能构建底层工具 | 仅开发构建使用，不随包分发 |
| **Windows Cabinet / BCrypt** | 系统自带 | Windows 系统组件 | 旧版自解压启动器解包支持 | 调用系统 API，无外置第三方依赖 |

---

## 2. 静态单 EXE 与 LGPLv3 合规体系

DanmaX 的默认发布格式为 **静态单 EXE**（`static-exe`），主程序直接嵌入编译好的 Qt 6.11 模块与 C++ 运行库。为严格履行 LGPLv3 针对静态链接衍生作品的相关条款（保障最终用户修改 LGPL 库并重新构建、重新链接的权利），项目设计了自动化的配套分发材料流水线：

### 2.1 配套材料包（Materials Directory）
每次执行 `pwsh -NoProfile -File scripts/package.ps1`，打包流水线都会在生成 `out/packages/DanmaX-static-<时间戳>/DanmaX.exe` 的同时，在其同级目录自动创建配套分发材料目录 `out/packages/materials-static-<时间戳>/`：
- **项目许可与版权**：包含本项目的 `LICENSE` (GNU General Public License v3.0) 与 `DISCLAIMER.md`。
- **Qt 官方许可集**：包含静态编译所使用的 Qt 源码中完整的 `LICENSES/` 目录与各模块 REUSE 元数据。
- **匹配的 Qt 源码归档**：包含经过 SHA-256 校验的官方 `qtbase`, `qtshadertools`, `qtdeclarative` 源码包。
- **应用源码快照**：包含生成该 EXE 时当前工作区的完整源码与配置清单（自动排除 `.local/` 用户私有配置与历史归档）。
- **静态链接符号表 (Map File)**：生成主程序的链接 Map 文件（如 `danmaku_app.map`），便于符号核验与重定位。
- **离线重建指引与脚本**：提供脱机重新编译 Qt 与重新链接主程序的清晰指令与说明。

### 2.2 重新链接与修改权利说明
- 静态主程序未引入防篡改哈希或阻碍重新链接的保护机制。
- 最终用户可基于随包提供的源码材料、CMake 预设以及锁定工具链清单，自行替换或修改 Qt 模块代码，并重新编译链接出功能等价的可执行文件。
- 公开发行二进制前，应连同对应时间戳的 `materials-static-<时间戳>` 材料目录（或提供可长期下载该材料的公开有效途径）一并向用户提供。

---

## 3. 三种发布格式的依赖与分发差异

| 发布格式 | 参数 | 运行库状态 | 外部依赖要求 | 合规材料重点 |
|---|---|---|---|---|
| **静态单 EXE** (推荐) | `-Format static-exe` | Qt、QML 与 CRT 全部以 `/MT` 编入 EXE | 仅依赖 Windows 系统 DLL 与显卡驱动 | 必须保留对应的 `materials-static/` 源码与重编译说明 |
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

## Fluent System Icons（2026-10-08）

少量官方图标来自 Microsoft Fluent System Icons，MIT 许可。锁定提交、原始 SVG 下载地址及 SVG/PNG SHA-256 位于 `resources/fluent/manifest.json`，完整版权与许可见 `resources/fluent/LICENSE`。发布材料包含 `Fluent-System-Icons-LICENSE.txt`。

静态 Qt 没有 QtSvg 模块，因此保留官方 SVG 原稿并嵌入离线生成的 3× PNG（含浅深色版本），公开 `icon.source` 继续由官方控件着色；不增加运行库。sharp 0.35.5 仅在本次资源制作时使用，构建与运行不依赖它、不联网。

## 历史 QML FluentUI 试用

2026-10-08 用户授权迁回官方 FluentWinUI3。第三方 FluentUI 已从正式构建、运行初始化和发布材料依赖中移除；toolchain/fluentui.lock.json 及调研脚本仅保留验证复现用途。现有静态 Qt SDK 包含曾为调研准备的 qt5compat，但当前应用不导入 Qt5Compat.GraphicalEffects。Qt 发布材料按实际 SDK 锁定版本保留，Microsoft Fluent System Icons 的独立许可证继续随包提供。
