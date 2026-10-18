# 内置播放页（开发版）

实施日期：2026-10-18。仅 `windows-libvlc-debug`／`windows-libvlc-release` 包含内置播放器；默认发布构建保持原来的单 EXE 能力。本功能不属于已经完成分发验收的播放器产品。

后续观看布局已增加应用自身的内置全屏和动态底栏，见 [观看布局交付记录](PLAYBACK_PRESENTATION.md)。下文最初轮次的证据及时间保持原记录。

## 使用与复现

```powershell
pwsh -NoProfile -File scripts/prepare-libvlc.ps1 -Offline
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-libvlc-release -Test
pwsh -NoProfile -File scripts/run.ps1 -Preset windows-libvlc-release -NoBuild
```

离线准备要求已缓存经过校验的官方二进制与源码归档；首次联网准备显式去掉 `-Offline`。构建、运行和部署不下载依赖。Debug 使用独立的 `windows-libvlc-debug` 目录，Release 与 Debug 不共享构建缓存。

在播放器设置中选择“内置播放器”，然后打开本地视频。含该功能的新安装默认内置；已有 `player-presets.json` 的默认项及外部路径保持原值。改变默认项只影响下一次打开。未包含内置功能的构建仍识别该预设，明确报不可用，不接管外部 VLC 或静默回退。

开发运行目录包含 `libvlc/`，依赖来自共享的 `toolchain/libvlc.lock.json`（3.0.23 x64），不查找系统 VLC。沿用已有 `.deps/libvlc-probe/` 与下载缓存，避免重复准备同一官方归档；旧探针入口继续可用。没有新增 Qt Multimedia、第三方 Qt 绑定、视频帧回调或自定义图形后端。

30 分钟实播及短回归命令：

```powershell
pwsh -NoProfile -File scripts/validate-libvlc-playback.ps1 -Video '<本地视频完整路径>' -Seconds 1800 -RunName hevc-local -NoBuild
```

每次使用新的 RunName，独立部署到 `out/run/validation/`，设置、测试 XML、报告与日志位于 `out/validation/libvlc-integration/<RunName>/`。脚本显式关闭在线弹幕匹配并使用合成 XML；真实视频不复制进仓库。`-Scale 1.5` 仅模拟缩放，`-Opaque` 关闭用户材质偏好；它们不等于真实混合 DPI 或系统高对比度验收。

内部验证驱动只在 C++ 命令行使用，未向 QML 暴露测试时钟。它通过正常产品模型进行暂停／恢复、前后定位、页面切换和自然结束后的重新播放，使用真实 libVLC 时间轴。持续播放计数、七项状态检查与进程退出记录分开保存；自动通过不等于声音可听、音画同步或光学帧率验收。

## 架构与窗口规则

- `LibVlcBackend` 实现公共 `MediaPlaybackBackend`，独立身份为 `libvlc`；外部 `VlcHttpBackend` 保持原协议。呈现能力区分 `DesktopOverlay` 与 `EmbeddedViewport`，公共任务层不按播放器名推断行为。
- libVLC C API 在唯一工作线程串行执行；读取当前媒体 URI，连接 token 与内容 ID 分开。GUI 接收有代次、新鲜度校验的采样，排队控制在任务失效后拒绝。日志回调只写有界队列，不接触 QML。
- 暂停与恢复由 NowPlayingModel 编排，定位／历史恢复统一经过 SeekController。定位接口返回只表示 Submitted，仍等待实际落点；首轮精度为 Unspecified。
- 原生 QWindow 由 VideoViewportController 在 GUI 线程拥有，WindowContainer 管理嵌入几何。使用现有唯一引擎、帧驱动、DanmakuItem 和覆盖窗。没有弹幕也可以播放视频。
- 覆盖窗由主窗拥有，穿透且不激活，跟随视频视口，不使用桌面全局置顶。切换页面／最小化隐藏画面及弹幕，媒体继续；隐藏期不补发弹幕。不打开浮动小窗。
- 视频区域固定，信息独立滚动；保留现有侧栏与底栏。内置播放期间的菜单／业务弹窗使用 Popup.Window；其他工作流保留原呈现。拖放提示独立呈现，不尝试捕获原生视频做场景模糊。
- 首次生产主窗实测发现 Mica 下黑边没有保持不透明纯黑；首轮采用内置资源存续期间的纯色主窗回退。用户 `micaEnabled` 不被改写，设置显示实际回退原因，释放后恢复偏好。切换不重建仍被 libVLC 使用的 HWND。
- 停止、取消、交接与退出先使旧控制失效，再后台释放。等待超过 8 秒显示实际状态，继续保留输出窗口，不 terminate 线程。应用关闭等待释放完成；开发启动脚本也不强杀旧实例。

## 验证记录

具体机器、构建哈希、媒体统计、状态读回、截图资格和退出结果以 `out/validation/libvlc-integration/` 的各轮证据为准。历史最小探针见 [最小验证报告](LIBVLC_EMBEDDING_PROBE.md)，其中的 27 项结果不能替代正式接入的验收。

最终后端的真实 HEVC／AVI 契约分别记录于 `lifecycle-contract-hevc.txt` 与 `lifecycle-contract-avi.txt`：各 7 项 Qt Test 条目通过（含初始化／清理）、0 失败、0 跳过。覆盖原生输出目标、暂停冻结、公共 SeekController 读回、旧 token 拒绝、自然结束、旧关闭隔离、历史恢复、从头播放、失败独立 XML 保留视频、成功交接及正常释放。音频缓冲统计与扬声器实听分开，不以缓冲计数宣称实听通过。

| 验证 | 结果与证据 |
| --- | --- |
| 内置 Release 全量 CTest | 23/23 目标通过，292.29 秒；`out/build/libvlc-ctest-complete.log`。未提供真实媒体的自动用例会明确跳过，真实契约另行执行，不能混算。 |
| 默认静态 Release | 全量 21/21 目标通过，181.51 秒；最终变更另补主程序构建与 video_open 检查（1/1 通过，1.37 秒；`out/build/static-libvlc-final-check.log`）。未包含 libVLC 的预设保留配置并显示不可用。 |
| 内置 Debug | 独立预设配置、主程序编译通过；`out/build/libvlc-debug-build.log`。本轮未将 Debug 编译等同于 Debug 实播验收。 |
| 真实 HEVC 长时 | `hevc-integrated-30min/`：1800 秒、1797 次播放状态计数、七项检查全通过，0 QML 警告、无任务错误、正常退出码 0。 |
| 深色与业务窗口 | `hevc-final-interactions/`：360 秒、正常退出、七项检查通过；实际视频、字幕、弹幕、原生菜单和业务弹窗合成经前台观察；`business-dialog.png` 保留实际窗口截图。 |
| 最终构建浅色布局 | `hevc-final-light-layout/`：180 秒，模拟 DPR 1.5，七项检查通过、0 QML 警告、退出码 0，视频与弹幕保持对齐。 |
| 最终构建纯色窗口 | `hevc-final-opaque-windows/`：180 秒，七项检查通过、0 QML 警告、退出码 0；实际最大化、最小化及还原后恢复视频，其他应用遮挡时覆盖层没有压到前台应用上。 |

环境为 Windows 11、Ryzen 5 5600（12 逻辑处理器）、RTX 4070（32.0.16.1742）、2560×1440 桌面。物理显示设置先前记录约 165 Hz，本次 Qt 报告 60 Hz，不能据此宣称 165 Hz 呈现通过。HEVC 样本 1920×1080、23.976 fps、AAC，时长 1420.086 秒；AVI 样本 80×48、5 fps、60 秒、无音频。长时轮次末尾工作集约 497.46 MiB，活动弹幕 6，文字缓存 1065 条；libVLC 累计 displayedPictures 43288、lostPictures 33、playedAudioBuffers 77930。日志保留硬解图片分配错误，不宣称零错误／零掉帧。同期存在构建和其他验证负载，这些是功能观察，不能作为受控性能基线或泄漏结论。

长时轮次除驱动暂停、恢复与前后定位外还有一次额外手动定位，不能直接以墙钟与末尾媒体位置之差推算漂移。该轮启动后又修复了业务窗口边缘、布局重入与关闭入口保护，因此长时运行和最终短回归分别保存 EXE SHA-256 于各自 `environment.json`；不能宣称最终同一二进制连续验证了 30 分钟。

原生 Popup.Window 的透明阴影边缘在此机呈现白色，最终保留官方 Dialog／原生窗口框，将窗口清屏色设为主窗实际主题色，并在可用屏幕内居中。`hevc-scale150-popup`、`hevc-popup-clear` 的状态检查虽通过，视觉问题未通过；`hevc-ui-final` 的布局重入警告也使该轮不合格。相关诊断轮次与早期 Mica 黑边失败轮次均保留，不混入通过统计。

## 发布与未验收边界

- 默认发布预设不启用 `DANMAKU_ENABLE_LIBVLC`。打包脚本检查实际 CMake 缓存并拒绝对启用该功能的构建生成正式包，静态 Qt 与内置功能组合也明确拒绝。
- 开发版保留完整已验证 VLC 运行库组合及许可文件，没有确定可分发插件最小集。libVLC 公共库与完整归档中的 GPL 插件须按实际组件分别审查，不能把整包视为单一 LGPL 组件。
- 后续观看布局已增加应用自身全屏；仍不包含小窗、音量／倍速／轨道／字幕选择、网络媒体和播放列表。字幕由 libVLC 当前默认行为显示。
- 真实资源管理器向原生视频区域拖放、真实多显示器混合 DPI、系统高对比度、扬声器实听及音画同步、更多编码、4K／HDR、损坏媒体及解码卡死的释放上限尚需另行验证。工作线程提供 GUI 隔离，不提供原生崩溃隔离或任意调用的硬超时保证。
- 本轮没有交付包含 libVLC 的正式目录包或无需释放运行库的纯单 EXE，也没有宣称干净系统部署已经完成。

官方接口依据：[libVLC 3 播放器](https://videolan.videolan.me/vlc-3.0/group__libvlc__media__player.html)、[Qt WindowContainer](https://doc.qt.io/qt-6.11/qml-qtquick-windowcontainer.html)、[Qt Popup.Window](https://doc.qt.io/qt-6.11/qml-qtquick-controls-popup.html)。
