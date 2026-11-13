# 内置播放的弹幕覆盖窗跟随设计

本文档说明内置视频播放模式下，如何通过 Windows 原生子类化消息回调实现低延迟弹幕覆盖窗几何同步，并记录正式集成后的实机验证边界。独立探针对比数据见 [独立顶层弹幕窗原生跟随探针](../research/probes/owned-overlay-follow-probe.md)。

## 1. 原生同步架构与设计约束

- **子类化挂接**：`VideoViewportController` 在 GUI 线程对本次自有主窗与视频 HWND 调用 Windows SDK `SetWindowSubclass`，先执行 `DefSubclassProc` 保留 Qt / QWindowKit 的消息处理链，再在 `WM_WINDOWPOSCHANGED` 与 `WM_EXITSIZEMOVE` 时同步弹幕覆盖窗。
- **单一几何写入者**：以实际视频窗口 `GetWindowRect` 为基准：矩形未变直接跳过；发生变化时更新覆盖窗逻辑几何并做有界校验，仍有像素差时最多执行一次 `SetWindowPos`（使用 `SWP_NOACTIVATE` / `SWP_NOZORDER` / `SWP_NOOWNERZORDER`），纯移动不重写尺寸。当原生跟随已接管时，Qt 兜底逻辑不再重复写入，防止逻辑与物理舍入差导致循环重绘。
- **生命周期安全**：仅在内置视口有效且页面可见时挂接；页面隐藏、任务交接、表面销毁或控制器析构时立即撤销挂接，回调中严格检查对象存活与代次，销毁路径禁止调用可能重建表面的 `winId()`。
- **诊断统计**：报告新增 `videoOverlayFollow` 字段（挂接状态、原生事件数、修正数、失败数、重入循环次数、同步前后最大像素差与当前差），仅维护固定计数器，不分配样本数组。

## 2. 回归与实机验证记录

最终 Release 主程序（SHA-256：`0EDB3EECDDFD18960E0FB95DF937F56A3229F998054DA81C76B976D327219609`）在修复早期静止重复写入问题（`owned-overlay-follow-production-hevc` 80 秒曾累计 9345 次修正）后，实测证据位于 `out/validation/owned-overlay-follow/final/` 与 `out/validation/libvlc-integration/`：

| 验证项目 | 结果与指标 |
|---|---|
| 默认静态完整 CTest | 23/23 目标通过（176.01 秒） |
| 内置完整 CTest 与独立 UI 复验 | 内置 24/25 目标通过（独立菜单测试经补充动画就绪等待后复验 32 passed / 0 failed / 1 skipped） |
| 原生视口专项（`video_viewport`） | 内置与静态构建各 6 passed / 0 failed / 0 skipped |
| 真实 HEVC / AVI 播放页与后端契约 | 播放页各 3 项通过（含播放/暂停各 20 次同步移动与 100 次静止无递增检查）；后端契约各 7 项通过 |
| 120 秒无人干预 HEVC 实播 | `owned-follow-production-unattended/`：112 个播放采样、12 项检查全通过、0 QML 警告、15 次移动事件最大差 0 px、当前差 0 px、失败 0、累计补偿 4 次 |
| 240 秒交互拖动观察轮 | `owned-follow-production-hevc-final/`：手动暂停及标题栏播放/暂停拖动，31 次移动事件最大差 0 px、当前差 0 px、失败 0、累计补偿 7 次（因手动暂停导致采样数 215 低于脚本 220 阈值，按脚本规则判失败并保留报告，不计入稳定性通过） |

> [!NOTE]
> 消息回调后的矩形相等仅证明两个独立 GPU 表面对齐，不代表跨表面逐帧原子呈现。高速连续拖动录像、真实多屏混合 DPI、触摸以及本构建的 30 分钟稳定性仍需另行专项验收。

## 相关内容

- [内置播放页接入](libvlc-integration.md)
- [播放底栏与内置观看布局](playback-presentation.md)
- [独立顶层弹幕窗原生跟随探针](../research/probes/owned-overlay-follow-probe.md)
