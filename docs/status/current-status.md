# DanmaX 当前交付状态与验收边界

本文档汇总 DanmaX 0.2+ 版本的当前已知问题、各子系统交付与验证矩阵以及尚未完成的人工验收边界。按月归档的详细构建、实机测试与证据记录见 [2026 年 11 月交付记录](changelog-2026-11.md) 与 [2026 年 10 月交付记录](changelog-2026-10.md)。

> [!IMPORTANT]
> 本项目严格区分 **已验证**、**已实现未验证** 与 **计划中**。自动化单元测试或模拟缩放不能替代真实多显示器混合 DPI、高对比度、Narrator 无障碍及干净机器部署验收。

## 1. 交付状态与验证矩阵

| 模块 / 维度 | 当前状态 | 验证依据与说明 |
|---|---|---|
| **目标架构与构建** | 已验证（基础构建） | C++20 + Qt 6.11.0 Quick/QML + MSVC 2022（Windows x64）；默认静态 Release（23/23 CTest）与内置 Release（25/25 CTest）在 `c3a01ae` 基线通过 |
| **默认便携分发** | 已验证（本机隔离目录） | 静态单 EXE（`/MT`，约 38.9 MiB，含 QWindowKit、Anitomy 与 Fluent SVG 合规材料）；`validate-static.ps1` 8 项检查通过 |
| **弹幕引擎与渲染** | 已验证（CTest + 本机样本） | 异步流式 XML 解析、物理防重叠轨道调度、自适应纹理显存预算（64–512 MiB）；`DANMAKU_RENDER_BACKEND` 支持对比 |
| **在线服务与缓存** | 已验证（`OnlineDanmakuTests`） | 弹弹play 兼容多服务顺序回退、混合标题解析（规则 + Anitomy）、`cache/danmaku/` 与 `cache/metadata/` 隔离缓存 |
| **外部播放器联动** | 已验证（受控 AVI 样本） | PotPlayer `/new` 独立实例消息同步与 VLC 3.x HTTP 受控关闭通过；PotPlayer 完整路径与倍速受原生协议限制 |
| **手动匹配原生选择器** | 已实现（含原生集成） | `055200b` 已将匹配弹窗“选择本地 XML”迁移至 Windows 原生选择器 `RecoveryXmlPicker`，需保持跨任务失效与换片关闭回归 |
| **内置 libVLC 播放** | 已验证（仅限开发构建） | `windows-libvlc-release` 32 分钟全屏长测与覆盖层原生跟随通过；禁止进入正式默认发布包 |
| **无障碍与跨环境** | **未完成实机验收** | 干净 Windows 系统、真实多显示器混合 DPI、系统高对比度、Narrator、扬声器实听与音画同步仍待专项验收 |

## 2. 当前已知问题与约束

- **外置播放器前台拖动主窗口掉帧**：外置 PotPlayer 前台播放时拖动 DanmaX 主窗口可能导致弹幕短暂停顿或掉帧（三次拖动无覆盖窗隐藏或时间轴重置，保留时间轴稳定修复，暂未启用激进预请求调度；实测记录位于 `out/analysis/external-drag-release/manual/run-20261113-024425-580/gesture-analysis.json`）。
- **外部播放器全屏与任务栏行为**：外部播放器全屏场景下的物理鼠标操作、真实混合 DPI 跨屏以及部分窗口置顶竞争下的任务栏闪烁仍需人工复测。
- **内置播放图形日志警告**：内置 libVLC 退出与切换期间日志仍存在 swapchain `alphaBufferSize` 与 `External WM_DESTROY` 警告，暂未消除，不宣称零图形错误。

## 3. 按月交付流水归档

- [2026 年 11 月交付与验证记录](changelog-2026-11.md)：涵盖播放基线复验与默认便携包、弹幕覆盖窗层级闪烁修复、拖动卡顿分析、延迟采样误重置修复与弹幕准备暂停/手动匹配窗。
- [2026 年 10 月交付与验证记录](changelog-2026-10.md)：涵盖 C++20/Qt 6 重构落地、FluentWinUI3 回归、静态单 EXE 打包体系、首页番剧搜索与元数据缓存、PotPlayer/VLC 受控接入、公共 `SeekController` 与内置 libVLC 观看布局。

## 相关内容

- [DanmaX 文档中心](../README.md)
- [C++ / Qt 核心架构](../architecture/cpp-qt-architecture.md)
- [Windows 便携分发](../release/packaging.md)
