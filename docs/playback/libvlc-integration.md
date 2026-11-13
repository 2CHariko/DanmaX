# 内置播放页接入（开发版）

> [!NOTE]
> **状态：仅限开发验证构建**。仅 `windows-libvlc-debug` 与 `windows-libvlc-release` 预设通过 `DANMAKU_ENABLE_LIBVLC` 启用内置播放器；默认静态单 EXE 发布构建不包含 libVLC。

本文档记录基于 libVLC 3.0.23 x64 的内置播放页架构、窗口与释放规则，以及已完成的开发实机验证与未验收边界。观看布局与全屏行为见 [播放底栏与内置观看布局](playback-presentation.md)，覆盖层跟随优化见 [覆盖层原生跟随设计](overlay-follow.md)。

## 1. 复现与运行步骤

运行以下命令在本地离线准备共享依赖（`toolchain/libvlc.lock.json`）并构建运行开发版：

```powershell
pwsh -NoProfile -File scripts/prepare-libvlc.ps1 -Offline
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-libvlc-release -Test
pwsh -NoProfile -File scripts/run.ps1 -Preset windows-libvlc-release -NoBuild
```

- 首次联网准备请移除 `-Offline` 参数；配置、构建与运行阶段严禁自动联网。
- 在 **设置 → 外部播放器** 中选择 **内置播放器** 后打开本地视频。未包含该特性的构建仍保留该条目但明确提示不可用，不静默回退到外部 VLC。
- 执行 30 分钟实播及短回归验证：

```powershell
pwsh -NoProfile -File scripts/validate-libvlc-playback.ps1 -Video '<本地视频完整路径>' -Seconds 1800 -RunName hevc-local -NoBuild
```

> [!NOTE]
> 请每次更换新的 `-RunName`，产物保存在 `out/validation/libvlc-integration/<RunName>/`。`-Scale 1.5` 为模拟缩放，`-Opaque` 关闭材质偏好，均不替代真实混合 DPI 或系统高对比度验收。

## 2. 架构与窗口规则

- **后端身份与线程隔离**：`LibVlcBackend` 实现公共 `MediaPlaybackBackend`（身份标识 `libvlc`），在单一工作线程串行调用 libVLC C API；GUI 接收带代次与新鲜度校验的采样，日志回调仅写入有界队列。
- **定位与呈现分离**：暂停/恢复由 `NowPlayingModel` 编排，定位经 `SeekController` 确认（首轮精度 `Unspecified`）；呈现能力区分 `DesktopOverlay` 与 `EmbeddedViewport`。
- **视频视口与透明覆盖层**：`VideoViewportController` 在 GUI 线程持有原生视频 `QWindow` 并由 `WindowContainer` 管理嵌入几何；弹幕覆盖层由主窗拥有，保持穿透且不激活，随页面隐藏/最小化同步隐藏而不补发隐藏期弹幕。
- **Mica 黑边回退**：针对透明 Mica 主窗下视频黑边无法保持纯黑的问题，在内置资源存续期间启用不透明主窗回退（不改写用户 `micaEnabled` 偏好，释放后自动恢复）。
- **释放顺序**：停止、交接或退出时先使旧控制失效并等待后台释放 libVLC 资源，超时（8 秒）报告真实状态，严禁强制 `terminate` 工作线程。

## 3. 已验证记录与环境限制

真实媒体契约记录于 `out/validation/libvlc-integration/`（前期最小探针见 [libVLC 内置播放探针](../research/probes/libvlc-embedding-probe.md)，两者结果独立统计）：

| 验证项目 | 结果与证据摘要 |
|---|---|
| 真实 HEVC / AVI 后端契约 | `lifecycle-contract-hevc.txt`、`lifecycle-contract-avi.txt` 各 7 项 Qt Test 通过（0 失败、0 跳过），覆盖输出目标、暂停冻结、`SeekController` 读回、旧 token 拒绝、EOF、历史恢复与正常释放 |
| 内置 Release 全量 CTest | 23/23 目标通过（292.29 秒，`out/build/libvlc-ctest-complete.log`）；未提供真实媒体的用例明确跳过 |
| 默认静态 Release 回归 | 21/21 目标通过（181.51 秒）及 `video_open` 1/1 通过 |
| 30 分钟 HEVC 长时实播 | `hevc-integrated-30min/`：1800 秒、1797 次播放采样、7 项状态检查全通过、0 QML 警告、正常退出码 0（末尾工作集约 497.46 MiB，保留硬解分配日志警告） |
| 交互与外观轮次 | `hevc-final-interactions/`（360 秒）、`hevc-final-light-layout/`（180 秒模拟 DPR 1.5）、`hevc-final-opaque-windows/`（180 秒最大化/最小化/遮挡）状态检查全通过 |

> [!WARNING]
> **尚未验收与发布边界**：
> - 默认发布构建禁止启用 `DANMAKU_ENABLE_LIBVLC`，`package.ps1` 会显式拒绝内置构建。
> - 开发版携带完整 libVLC 归档及许可文件（LGPL-2.1-or-later 与部分 GPL 插件），可分发插件最小集尚未审定，单文件分发可行性见 [libVLC 单文件分发可行性](../release/libvlc-single-file.md)。
> - 真实资源管理器向原生视频区拖放、真实混合 DPI、系统高对比度、扬声器实听与音画同步、4K/HDR 与异常媒体释放上限尚待专项验收。

## 相关内容

- [播放底栏与内置观看布局](playback-presentation.md)
- [覆盖层原生跟随设计](overlay-follow.md)
- [外部播放器预设与契约](external-players.md)
- [libVLC 内置播放探针](../research/probes/libvlc-embedding-probe.md)
