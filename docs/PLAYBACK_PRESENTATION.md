# 播放底栏与内置观看布局

实施日期：2026-10-18。适用于当前 C++／Qt 开发版。内置视频继续使用显式启用的 `windows-libvlc-debug`／`windows-libvlc-release`，正式分发限制见 [内置接入说明](LIBVLC_INTEGRATION.md)。

## 产品行为

底栏左侧只显示标题和简短状态，没有缩略图。进度位于上行，时长超过一小时使用 `h:mm:ss`；下行播放按钮相对整个窗口居中，两侧为后退 10 秒和前进 30 秒。标题可以通过键盘激活并返回播放页。窄窗先移除次要状态和播放器按钮，再省略／隐藏标题；极端文字放大时动作区另起一行，保留控件命中尺寸。

只有“播放页＋内置视频任务”进入观看布局，隐藏侧栏与分隔线。窗口模式保留紧凑标题栏、返回入口和固定底栏；视频填充剩余区域并保持比例，黑边不透明。返回最近的非播放页后，媒体继续推进，视频及弹幕隐藏，返回时不补发隐藏期间的弹幕。外部播放器、独立弹幕、无任务继续采用常规导航和同一底栏。

底栏按钮或 F11 切换应用自身全屏。Escape 先交给菜单／弹窗，没有弹窗才退出全屏；不增加双击手势。页面跳转、任务结束和类型交接退出全屏，恢复原屏幕及普通位置／尺寸或最大化状态。自然结束保留观看页和“重新播放”。不控制外部播放器全屏。

全屏视频几何不随底栏显隐变化。鼠标、触摸或键盘交互唤出控制条，播放中空闲 3 秒隐藏；暂停、完成、加载、错误、定位等待、拖动、控件键盘焦点和弹窗打开时保持显示。失去前台或最小化隐藏控制窗，恢复时重新显示并计时。收起后控制窗实际隐藏，不留下透明输入层，也不强制隐藏系统光标。

观看标题栏、底栏及其菜单通过统一语义颜色和公开 palette 使用深色；其他页面及来源业务弹窗保持用户主题。系统高对比度有单独语义分支，但仍需实机验收。官方控件内部素材和工具提示可能继续呈现全局主题差异，不承诺像素级复刻。保留内置资源存续期间的不透明主窗回退及用户 Mica 偏好。

## 实现边界

- `PlaybackPresentationController` 只管理 GUI 呈现、窗口恢复、输入与可见性；全屏期间的 50ms 指针／前台感知不是媒体采样或第二套弹幕时钟。
- `VideoViewportController` 保有原生视频 QWindow，在 GUI 线程转交输入通知。全屏只改变主窗状态和几何，不重建、重新绑定 libVLC 使用的 HWND。
- 窗口模式底栏在主场景，全屏迁移同一个底栏 Item 到主窗拥有的独立 QQuickWindow。控制窗仅占底栏矩形，不采用桌面置顶；业务菜单／弹窗高于控制条，控制条高于视频和弹幕。
- 切换底栏宿主前关闭菜单并取消未提交拖动草稿；已经提交的定位继续由原 SeekController 确认。`NowPlayingModel::seekRelative` 在当前预览目标上累计并夹取，视频和历史恢复共用原事务，独立弹幕走既有本地定位。
- 弹窗检测同时检查公开 Popup 可见属性和原生拥有关系，覆盖 Popup 已打开但原生表面尚未建立的过渡帧，避免 Escape 提前退出全屏。
- 新增两枚图标来自已锁定微软 Fluent System Icons，版本及哈希记录于 `resources/fluent/manifest.json`。无新增运行库、网络构建步骤、播放器线程或渲染后端。

## 复现

在项目根目录执行；真实媒体使用本地完整路径，不提交媒体文件。

```powershell
pwsh -NoProfile -File scripts/prepare-libvlc.ps1 -Offline
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-libvlc-release -Test
pwsh -NoProfile -File scripts/run.ps1 -Preset windows-libvlc-release -NoBuild
pwsh -NoProfile -File scripts/validate-libvlc-playback.ps1 -Video '<本地 HEVC 视频完整路径>' -Seconds 1920 -Fullscreen -RunName fullscreen-32min -NoBuild -Theme light
```

全屏长测先完成窗口／页面／暂停／定位契约，再进入持续全屏，因此用 1920 秒总运行确保至少 1800 个实际全屏采样。每 300 秒计划暂停 4 秒，每 300 秒偏移 180 秒提交一次相对定位；样本接近末尾时在同一连接内定位回 5 秒，EOF／重播由独立真实契约验证。它是带控制操作的长时功能运行，不是无定位的漂移基准。手动退出全屏会影响最终身份与全屏计数判定。

报告、环境、EXE SHA-256、截图、libVLC 日志和退出码位于 `out/validation/libvlc-integration/<RunName>/`；截图只在拥有前台时采集。`-Scale 1.5` 是模拟缩放，不能替代真实混合 DPI。`-Opaque` 显式关闭材质偏好。脚本不强杀应用，等待后台正常释放。

## 本轮验证

自动检查以 CTest 目标数、Qt Test 条目数分别统计，真实媒体未提供时的跳过不计为实播通过。

| 验证 | 证据与边界 |
| --- | --- |
| 内置 Release 全量 | 最终构建 `out/build/playback-presentation-final-ctest.log`：24/24 目标，287.03 秒；界面 32 passed、0 failed、1 skipped（显式真实媒体用例另跑）。 |
| 默认静态 Release | `out/build/playback-layout-static-tests.log`：22/22 目标，193.77 秒；最终构建与呈现／两个冒烟 3/3，6.85 秒，`out/build/playback-presentation-static-checks.log`。不包含 libVLC。 |
| 内置 Debug | 最终构建与呈现专项 1/1，2.40 秒，`out/build/playback-presentation-debug-checks.log`；不将编译等同于 Debug 实播。 |
| 相对定位和普通界面 | 工作流 27 个条目通过；覆盖累计目标、上下界、只有一个在途请求、任务取消。界面覆盖无任务／独立／外部投影、时长格式、800／1080／1600 窗宽居中与边界。 |
| 真实后端 | `presentation-backend-hevc.txt`、`presentation-backend-avi.txt`：各 7 passed，0 failed，0 skipped，包含自然结束、重播、任务交接及释放。 |
| 真实全屏界面 | 最终构建 `fullscreen-ui-contract-final-hevc.txt`、`fullscreen-ui-contract-final-avi.txt`：各 3 passed，0 failed，0 skipped（3878／2889ms）；单底栏迁移、取消草稿、菜单 Escape、连接／HWND 稳定、正常释放，0 QML 引擎警告。操作系统未授予激活时，测试明确只检查焦点意图。HEVC 定位／释放时仍有解码器 stderr，未作为零解码错误结论。 |
| 放大文字 | `presentation-large-text-ui.txt`：13.5pt 文字下 4 passed，0 failed，0 skipped；不是系统高对比度验收。 |
| 前台交互与模拟缩放 | `fullscreen-layout-scale150/`：浅色应用、DPR 1.5，实际观察深色底栏和菜单、视频和弹幕、来源搜索业务窗口层级，Tab 可见焦点；Escape 关闭业务窗口后保留全屏。480 秒自动运行正常退出。 |

所有相对证据路径以 `out/validation/libvlc-integration/` 为基准，`out/build/` 开头的路径除外。前台观察和自动化输入边界另记于 `playback-presentation-manual-observations.json`。这些本地产物不入 Git。早期布局居中、弹窗过渡和测试准备失败的记录保留，不计为通过；人工退出的 `fullscreen-layout-32min/` 不作为稳定性成功证据。

### 长时与最终构建

最终二进制 SHA-256 为 `CC1759F129273C1472422E6844FD7E87BE9A72B1BD7A939375193C699B92E04B`，`fullscreen-layout-final-dark/` 实播 360 秒、286 次全屏采样、348 次播放采样、12 项状态检查全通过，0 QML 引擎警告、无任务错误、正常退出码 0。此轮采用深色主题和显式纯色偏好，最终 HEVC／AVI 界面契约也基于该构建。

0 QML 引擎警告不等于零图形／原生日志警告。最终短回归仍记录 swapchain alphaBufferSize 警告，退出时 owned 弹幕窗口记录 External WM_DESTROY；后者在 libVLC D3D11 资源与输出线程释放后出现，进程正常退出。尚未消除这些警告，不宣称零图形错误。短回归末尾工作集约 516.62 MiB，displayedPictures 8482、lostPictures 4、playedAudioBuffers 15144。

本轮全屏长测 `fullscreen-layout-final-32min/` 于本地时间 15:17:17–15:49:20 完成：1920 秒、1846 个实际全屏状态采样、1888 个播放采样、12 项检查全通过，0 QML 引擎警告、无任务错误、正常退出码 0。视频 HWND 始终为 `200854`，连接始终为 `danmax-libvlc:2cc58c18030240d99d09124c44b04b2d`；全屏往返、隐藏控制条及样本末尾定位保护没有重建或重连。所有本轮验证进程均已正常退出。

长测二进制 SHA-256 为 `0831DC0B3F33EC313041BA26959E868B00E94DAC040A49E53E9E2C353F462D97`。长测启动后，最终构建仅补充警告文字色、菜单初始焦点与保留光标策略，没有改媒体工作线程／连接／全屏宿主路径；最终构建另完成上述 360 秒与真实界面回归。不能据此声称最终同一 EXE 连续实播了 30 分钟。开发运行目录已同步最终构建，`playback-presentation-runtime-hashes.json` 记录构建与运行 EXE 哈希一致。

启动长测时的旧脚本只允许 20 秒非播放采样，没有扣除每五分钟计划暂停 4 秒，因 1888 小于旧阈值 1900 而误报失败；保留该原始脚本退出失败记录。修订阈值为 1876，完整条件按修订脚本独立复核通过，见 `adjudication.json`（应用退出码仍为 0，12 项检查均为真）。修订脚本在最终 360 秒轮次自身退出成功，不将旧脚本声称为退出成功。

环境为 Windows 11、Ryzen 5 5600（12 逻辑处理器）、RTX 4070（32.0.16.1742）；本轮 Qt 报告 1920×1080、DPR 1、180 Hz，不沿用先前 2560×1440／60 Hz 记录。HEVC／AAC 样本 1920×1080、23.976 fps、1420.086 秒，AVI 为 80×48、5 fps、60 秒、无音频。长测末尾工作集约 476.09 MiB，displayedPictures 45653、lostPictures 16、playedAudioBuffers 81730；保留时间戳转换、迟到图片、swapchain 和退出 owned 窗口警告。同期有构建和其他任务，未控制前台占用或系统负载，不作为光学帧率、音画同步、受控性能或内存泄漏验收。

## 尚未验收及后续

真实多显示器／混合 DPI、系统高对比度、触摸、Narrator、扬声器实听与音画同步未完成本轮验收；模拟缩放、音频缓冲计数及焦点意图不能替代它们。减少动画复用现有语义，全屏路径没有新增动画，但没有单独完成系统设置切换实机验收。外部 PotPlayer／VLC 的新全屏入口不适用，现有后端自动回归不等于重新完成其所有版本的实机矩阵。

全屏路径的真实最大化还原、最小化恢复及文件拖放仍需补充逐项前台验收；自动状态测试、真实后端契约和上一轮窗口模式结果各自保留边界。本轮前台鼠标暂停的后续尝试因检测到用户输入而未完成，不能记为通过；暂停读回由真实自动契约覆盖。

未新增音量、音轨／字幕选择、小窗、播放列表或上一集／下一集。正式内置发布仍未完成，发布脚本继续拒绝将启用 libVLC 的构建产出为正式单 EXE 包。
