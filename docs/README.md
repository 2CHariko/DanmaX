# DanmaX 核心文档中心 (Documentation Hub)

欢迎查阅 DanmaX 项目技术与用户文档。本文档中心旨在梳理项目的架构决策、配置体系、界面规范、打包发布及测试基准，方便开发者与用户快速定位所需资料。

---

## 1. 文档导航矩阵

```text
docs/
├── 📘 USER_GUIDE.md                           # 最终用户实操教程与常见问题 (FAQ)
├── ⚙️ CONFIGURATION.md                        # 21 项配置参数全量字典速查与编辑规范
├── 📄 settings.example.ini                    # 首次生成之完整 UTF-8 INI 模板
├── 🏗️ CPP_QT_ARCHITECTURE.md                 # C++20 / Qt 6 核心架构设计与数据流
├── 🎨 UI_DESIGN_SYSTEM.md                    # Qt 官方 FluentWinUI3 规范与控件约束
├── 📦 PACKAGING.md                           # 真正静态单 EXE 便携打包与运行模型
├── 🛠️ SCRIPTS.md                             # 全量 24 个 PowerShell 脚本参数手册与工作流
├── ⚖️ THIRD_PARTY.md                         # 第三方组件、源码重构材料与 LGPLv3 合规
├── 📊 FRAMEWORK_STATUS.md                     # 里程碑交付状态与实机验证证据链
├── 📈 TEXTURE_BUDGET_PERFORMANCE_20261008.md # 自适应文字图片显存预算基准报告
├── 📈 RENDERER_PERFORMANCE_20261007.md       # 场景图文字图片缓存渲染优化记录
├── 🎀 MASCOT_DESIGN.md                         # 官方二次元看板娘设计规范与立绘工程设定
└── 🔬 DANMAKU_REFACTOR_FEASIBILITY.md        # 弹幕引擎架构选型与重构可行性分析
```

---

## 2. 分类导读

### 2.1 用户实操与配置
- **[最终用户指南 (USER_GUIDE.md)](USER_GUIDE.md)**：包含便携目录说明、本地 XML 加载、在线弹幕搜索/下载/缓存、SMTC 跟随与独立双模播放及常见问题排查。
- **[配置手册 (CONFIGURATION.md)](CONFIGURATION.md)**：详解 21 项 INI 参数的类型、合法区间、默认值与实时热生效行为。
- **[完整配置示例 (settings.example.ini)](settings.example.ini)**：包含每个字段详尽中文注释的默认配置文件。

### 2.2 架构设计与界面规范
- **[C++ / Qt 架构设计 (CPP_QT_ARCHITECTURE.md)](CPP_QT_ARCHITECTURE.md)**：阐述纯 C++20 领域核心（时钟、时间轴、防重叠物理轨道调度）与 Qt Quick 场景图渲染分层架构。
- **[FluentWinUI3 界面规范 (UI_DESIGN_SYSTEM.md)](UI_DESIGN_SYSTEM.md)**：阐述编译期锁定 Qt 官方 FluentWinUI3 样式、响应式布局、4 单位网格、统一弹出层与键盘焦点支持。
- **[看板娘设计设定集 (MASCOT_DESIGN.md)](MASCOT_DESIGN.md)**：官方拟人化形象“艾克斯（DanmaX）”的视觉人设、功能映射、二游立绘 Prompt 与 UI 状态差分规范。
### 2.3 部署、分发与开源合规
- **[Windows 便携分发 (PACKAGING.md)](PACKAGING.md)**：详解静态单 EXE（`/MT`）、配套材料包、零外部运行时解压与目录权限隔离。
- **[第三方组件与开源合规 (THIRD_PARTY.md)](THIRD_PARTY.md)**：阐述 LGPLv3 静态链接合规流水线（源码包、SBOM、重编译脚本与 Map 文件）及依赖许可范围。

- **[开发与构建脚本手册 (SCRIPTS.md)](SCRIPTS.md)**：包含全量 24 个 PowerShell 脚本参数详解、环境准备、静态编译、一键发布与基准测试端到端工作流。
### 2.4 测试基准与交付验证
- **[重构交付状态 (FRAMEWORK_STATUS.md)](FRAMEWORK_STATUS.md)**：记录从基线至今所有特性的演进、测试用例覆盖（CTest 13/13）、真机测试数据与待人工验收边界。
- **[自适应文字预算性能报告 (TEXTURE_BUDGET_PERFORMANCE_20261008.md)](TEXTURE_BUDGET_PERFORMANCE_20261008.md)**：对比固定 64 MiB 与自适应 512 MiB 预算在 165Hz/2K/4K 高 DPI 下的性能表现。
- **[文字渲染优化记录 (RENDERER_PERFORMANCE_20261007.md)](RENDERER_PERFORMANCE_20261007.md)**：QSGTextNode 矢量渲染与文字纹理图集渲染的实测帧率与 CPU 占用对照。
