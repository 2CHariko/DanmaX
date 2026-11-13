# DanmaX 文档中心

欢迎来到 DanmaX 文档中心。DanmaX 是基于 C++20 与 Qt 6 Quick 构建的 Windows 原生高性能外挂弹幕工具。本索引按职能与受众统一分类全仓文档，所有文档遵循 [DanmaX 文档撰写规范](engineering/documentation-style.md)（Microsoft 简体中文技术文档标准）。

> [!NOTE]
> 文档状态标记说明：**[规范]** 表示现行强制或参考标准；**[指南]** 表示操作手册；**[状态]** 表示当前里程碑或归档流水；**[调研]** / **[探针]** / **[基准]** 表示阶段性评估、实验探针或特定条件性能数据，不直接等同于当前产品承诺。

## 1. 文档分类目录树

```text
docs/
├── README.md                              # 统一文档中心与索引（本文档）
├── user/                                  # 用户指南与配置字典
│   ├── user-guide.md
│   ├── configuration.md
│   └── settings.example.ini
├── architecture/                          # 核心架构与业务契约
│   ├── cpp-qt-architecture.md
│   ├── dual-workflows.md
│   ├── now-playing.md
│   └── seek-transactions.md
├── playback/                              # 播放器适配、观看布局与覆盖层
│   ├── external-players.md
│   ├── libvlc-integration.md
│   ├── playback-presentation.md
│   └── overlay-follow.md
├── ui/                                    # 界面设计系统与视觉资产
│   ├── ui-design-system.md
│   └── mascot-design.md
├── engineering/                           # 工程、脚本、文档与提交规范
│   ├── documentation-style.md
│   ├── git-commit-convention.md
│   └── scripts-manual.md
├── release/                               # 打包、分发与开源合规
│   ├── packaging.md
│   ├── third-party.md
│   └── libvlc-single-file.md
├── status/                                # 交付状态与按月验证归档
│   ├── current-status.md
│   ├── changelog-2026-11.md
│   └── changelog-2026-10.md
└── research/                              # 技术调研、探针与性能基准
    ├── anitomy-evaluation.md
    ├── danmaku-refactor-feasibility.md
    ├── potplayer-drop-open.md
    ├── vlc-integration.md
    ├── ui-library-evaluation.md
    ├── winui-xaml-islands.md
    ├── probes/
    │   ├── acrylic-popup-probe.md
    │   ├── libvlc-child-windows-probe.md
    │   ├── libvlc-embedding-probe.md
    │   └── owned-overlay-follow-probe.md
    └── benchmarks/
        ├── renderer-performance-20261007.md
        └── texture-budget-performance-20261008.md
```

## 2. 按专题索引

### 2.1 用户指南与配置（`docs/user/`）

| 文档 | 标签 | 内容摘要 |
|---|---|---|
| [DanmaX 用户指南](user/user-guide.md) | [指南] | 便携运行目录、双任务工作流、混合弹幕匹配、快捷悬浮工具栏与常见问题排查 |
| [配置文件参考 (settings.ini)](user/configuration.md) | [规范] | `settings.ini` 全量字段字典、手工编辑语法、防抖原子写盘与错误恢复规则 |
| [示例配置文件](user/settings.example.ini) | [参考] | 首次启动自动生成的带完整中文注释 UTF-8 INI 模板 |

### 2.2 核心架构与任务契约（`docs/architecture/`）

| 文档 | 标签 | 内容摘要 |
|---|---|---|
| [C++ / Qt 核心架构与项目内开发边界](architecture/cpp-qt-architecture.md) | [规范] | 分层边界、纯 C++20 领域核心、单一时钟驱动、自适应纹理预算与项目内工具链隔离规则 |
| [双任务工作流](architecture/dual-workflows.md) | [规范] | 视频同步与独立弹幕互斥交接、继续观看条件、`recent-videos.json` 与实例关闭边界 |
| [播放中心](architecture/now-playing.md) | [规范] | 播放页与全宽底栏入口、能力投影边界及数据隔离 |
| [公共定位事务 (SeekController)](architecture/seek-transactions.md) | [规范] | `Rejected`/`Submitted`/`Unknown` 三态语义、连续输入排队、8 秒超时与验证要求 |

### 2.3 播放器适配与呈现（`docs/playback/`）

| 文档 | 标签 | 内容摘要 |
|---|---|---|
| [外部播放器预设与公共后端契约](playback/external-players.md) | [规范] | PotPlayer x64 与 VLC 3.x x64 自动发现、PE 静态校验、受控拥有权及 `MediaPlaybackBackend` 契约 |
| [内置播放页接入（开发版）](playback/libvlc-integration.md) | [状态] | `windows-libvlc-*` 开发构建下的 libVLC 3.0.23 接入架构、实播验证与不进发布包边界 |
| [播放底栏与内置观看布局](playback/playback-presentation.md) | [规范] | 统一底栏响应式布局、内置观看模式隐藏侧栏、F11 全屏迁移与 32 分钟实测记录 |
| [内置播放的弹幕覆盖窗跟随设计](playback/overlay-follow.md) | [规范] | 基于 Windows `SetWindowSubclass` 的原生几何跟随设计与回归证据 |

### 2.4 界面设计系统与视觉资产（`docs/ui/`）

| 文档 | 标签 | 内容摘要 |
|---|---|---|
| [FluentWinUI3 界面设计系统与交互规范](ui/ui-design-system.md) | [规范] | 官方 FluentWinUI3 控件、`Ui.qml` 语义、独立卡片、`AppComboBox`、`AppScrollBar` 留白滚轮、原生对话框与 Fluent SVG 约束 |
| [DanmaX 官方看板娘设定集](ui/mascot-design.md) | [参考] | 官方角色“艾克斯”的视觉三视图、功能映射与 UI 状态差分规范 |

### 2.5 工程规范与自动化脚本（`docs/engineering/`）

| 文档 | 标签 | 内容摘要 |
|---|---|---|
| [DanmaX 文档撰写规范](engineering/documentation-style.md) | [规范] | Microsoft 简体中文技术文档结构、语言风格、术语表与目录索引维护规则 |
| [Git 提交规范与 Hook 指南](engineering/git-commit-convention.md) | [规范] | Conventional Commits 简体中文提交格式、`type`/`scope` 字典与本地 `commit-msg` Hook 启用 |
| [PowerShell 开发与构建脚本手册](engineering/scripts-manual.md) | [指南] | `scripts/` 全部 42 个脚本的参数说明、端到端工作流与常见排错 |

### 2.6 打包、分发与开源合规（`docs/release/`）

| 文档 | 标签 | 内容摘要 |
|---|---|---|
| [Windows 便携分发](release/packaging.md) | [规范] | `static-exe` 单文件打包、静态 Qt 编译、依赖白名单检查与 `release.ps1` 发布流水线 |
| [第三方组件、上游项目与合规说明](release/third-party.md) | [规范] | GPL-3.0 / LGPL-3.0 / Apache-2.0 / MPL-2.0 / MIT 许可清单与配套源码重链接材料包 |
| [libVLC 内嵌单文件分发可行性](release/libvlc-single-file.md) | [调研] | 静态链接与自解压单文件方案体积分析及未验证技术风险 |

### 2.7 交付状态与按月记录（`docs/status/`）

| 文档 | 标签 | 内容摘要 |
|---|---|---|
| [DanmaX 当前交付状态与验收边界](status/current-status.md) | [状态] | 当前里程碑交付总表、已知问题清单与尚未完成的人工验收项 |
| [2026 年 11 月交付与验证记录](status/changelog-2026-11.md) | [状态] | 2026-11 交付基线复验、拖动分析、匹配弹窗与层级修复记录 |
| [2026 年 10 月交付与验证记录](status/changelog-2026-10.md) | [状态] | 2026-10 C++20 重构、UI 迁移、打包体系与内置播放演进记录 |

### 2.8 技术调研、探针与性能基准（`docs/research/`）

| 文档 | 标签 | 内容摘要 |
|---|---|---|
| [Anitomy 接入可行性验证](research/anitomy-evaluation.md) | [调研] | C++20 静态链接 Anitomy 选型评估与规则混合边界 |
| [弹幕引擎重构可行性分析](research/danmaku-refactor-feasibility.md) | [调研] | 早期从 Python/Qt 迁移至 C++20/Qt Quick 的重构可行性评估 |
| [拖入视频与 PotPlayer 联动调研](research/potplayer-drop-open.md) | [调研] | PotPlayer `/new`、原生消息与自动弹幕联动基线调研 |
| [VLC 外部播放器接入调研](research/vlc-integration.md) | [调研] | VLC 3.0.23 Lua HTTP 精确状态探针与外部后端调研 |
| [Qt/QML Fluent 控件库验证](research/ui-library-evaluation.md) | [调研] | 历史第三方 Fluent 控件库评估（已替代为官方 FluentWinUI3） |
| [WinUI 3 XAML Islands 接入评估](research/winui-xaml-islands.md) | [调研] | WinUI 3 XAML Islands 原型评估与生产构建排除原因 |
| [下拉原地展开与 Acrylic 最小验证](research/probes/acrylic-popup-probe.md) | [探针] | DWM 透底与 Acrylic 浮层独立探针数据 |
| [同父视频子窗与弹幕子窗验证](research/probes/libvlc-child-windows-probe.md) | [探针] | 同父原生子窗 Z 序与穿透边界探针 |
| [libVLC 内置播放最小验证](research/probes/libvlc-embedding-probe.md) | [探针] | libVLC 独立嵌入探针 27 项检查记录 |
| [独立顶层弹幕窗原生跟随验证](research/probes/owned-overlay-follow-probe.md) | [探针] | 子类化原生跟随机制原型探针 |
| [600 条真实文字渲染优化](research/benchmarks/renderer-performance-20261007.md) | [基准] | `QSGTextNode` 与 `QImage` 纹理渲染性能对比（2026-10-07） |
| [文字图片预算优化记录](research/benchmarks/texture-budget-performance-20261008.md) | [基准] | 自适应纹理显存预算（64–512 MiB）对比测试（2026-10-08） |

## 3. 根仓库关联文档

- [项目主页 (README.md)](../README.md)
- [代理开发约束 (AGENTS.md)](../AGENTS.md)
- [参与贡献指南 (CONTRIBUTING.md)](../CONTRIBUTING.md)
- [免责声明与使用条款 (DISCLAIMER.md)](../DISCLAIMER.md)
- [脚本目录说明 (scripts/README.md)](../scripts/README.md)
