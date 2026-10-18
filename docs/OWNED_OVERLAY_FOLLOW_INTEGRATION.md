# 内置播放的弹幕覆盖窗跟随优化

日期：2026-10-18。将 [独立探针验证](OWNED_OVERLAY_FOLLOW_PROBE.md) 的原生同步策略接入正式播放页。仍使用原生视频子窗和主窗拥有的透明顶层弹幕窗，Qt Quick 继续使用 GPU。没有引入新播放器接口、像素帧回调、软件场景图或同父弹幕子窗。

## 实现

`VideoViewportController` 在 GUI 线程拥有原生跟随适配器。只对本次自有主窗和视频 HWND 使用 Windows SDK `SetWindowSubclass`，每个适配器有独立身份；先执行 `DefSubclassProc`，保留 Qt／QWindowKit 的消息链，然后在 `WM_WINDOWPOSCHANGED`／`WM_EXITSIZEMOVE` 同步弹幕窗。

实际视频窗口是几何基准：先读取 `GetWindowRect`，相同就跳过写入；实际有变化时用视频 QWindow 的 `mapToGlobal`、`size` 和覆盖窗的公开 `setGeometry` 维护 Qt 逻辑几何与场景图大小，再校验物理矩形。仍有差异时最多一次 `SetWindowPos` 修正，不激活、不改 Z 序或所有者层级；纯移动不重复写入尺寸。没有全局 DPR 换算。原生跟随已经接管时，Qt 兜底不再提前写入相同目标，避免逻辑／物理舍入差形成反复修正。

Qt 布局、屏幕、DPI、窗口状态与表面创建事件合并为单个待处理兜底更新，原生消息内的跟随不等待下一帧或媒体采样。没有新增定时器、第二套引擎或媒体轮询。相同矩形不重复写入；适配器重入受保护。

只有内置视口有效且页面可见时才挂接。页面隐藏、结束／交接、主窗更换、覆盖窗替换、表面销毁及控制器析构撤销挂接；回调检查对象存续、挂接代次与所有者。销毁事件中不调用可能创建表面的 `winId()`。WindowContainer 继续管理视频父子关系与布局，不修改运行中视频的窗口标志，不重建 libVLC 使用的 HWND；释放顺序仍由既有媒体生命周期管理。

适配器只依赖自有 QWindow 和呈现状态，独立于 libVLC。后续其他内核采用原生视频视口时可以复用。播放器后端、SeekController、历史恢复、XML 选源和唯一弹幕帧驱动保持原有契约。

运行目标额外链接 Windows 系统的 `comctl32`，使用现有 Windows SDK；没有新增第三方库、下载或分发运行包，默认静态发布能力不变。

诊断报告增加 `videoOverlayFollow`：挂接状态、原生位置事件数、修正数、失败数、原生移动循环次数、同步前后最大矩形差和当前差。只维护固定数量的累计计数，输出复用既有报告入口；不在每条原生消息中分配样本数组或写盘。`maxAfterEventPx` 只统计成功跟随的有效事件，须同时检查 `failures`；`currentDifferencePx=-1` 表示未挂接／不可读取，不等于已对齐。

## 回归与证据

自动专项 `video_viewport` 在内置与默认静态构建运行。覆盖无事件泵的连续原生移动、视频单独移位／改尺寸、实际视频矩形与分数 QML 视口的区别、隐藏／任务交接不争用共享覆盖窗、覆盖窗替换、未绑定媒体时的表面重建、最小化恢复及控制器析构。真实媒体 QML 合约另外检查播放／暂停各 20 次移动、暂停位置冻结、全屏与连接／HWND 身份，以及菜单和定位草稿原有行为；增加连续 100 次静止同步不得增长原生写入次数的检查。

原有独立弹幕菜单测试在完整运行中两次出现导航后菜单未打开，单独运行该用例通过。该测试原来连续切换三个页面后立即发送按键，现补充等待实际 `afterAnimating` 帧及确认目标焦点的前置条件，再执行原有键盘／菜单断言；未删除断言、改为直接调用菜单或修改产品菜单实现。修订前的完整失败、单用例通过及修订后的验证分别保存在最终证据目录。

首个生产诊断轮 `out/validation/libvlc-integration/owned-follow-production-hevc/` 发现重复写入：拖动后矩形差为 0，但 Qt 兜底和原生舍入修正互相覆盖，80 秒报告累计 9345 次原生修正。该轮由验证者正常关闭、应用退出 0；因未到计划的 240 秒，脚本明确失败，保留报告而不计为稳定性通过。修订后以实际矩形为跳过条件并使原生适配器成为唯一几何写入者；下列最终证据使用新的 EXE 和目录，不复用此轮成绩。

最终 Release 主程序 SHA-256：`0EDB3EECDDFD18960E0FB95DF937F56A3229F998054DA81C76B976D327219609`。最初构建及真实契约保留于 `out/validation/owned-overlay-follow/production/`，修复静止重复写入后的最终契约与构建清单位于 `out/validation/owned-overlay-follow/final/`，不混用二进制成绩。

| 验证 | 结果 |
|---|---|
| 首次内置完整 CTest | 25/25，280.28 秒；在静止写入修复前，保留原始记录 |
| 最终默认静态完整 CTest | 23/23，176.01 秒 |
| 最终内置完整 CTest | 24/25，295.19 秒；独立菜单测试失败，原始日志保留 |
| 菜单就绪条件补充后的完整 UI 组复验 | CTest 1/1，105.74 秒；Qt Test 32 passed、0 failed、1 skipped（未指定真实媒体），原断言保留 |
| 最终原生视口专项 | 内置、静态各 6 passed、0 failed、0 skipped（含初始化／清理） |
| 最终真实 HEVC／AVI 播放页 | 各 3 passed、0 failed、0 skipped（含初始化／清理），播放／暂停各 20 次同步移动、100 次静止更新、全屏／菜单／定位草稿检查通过 |
| 最终真实后端契约 | HEVC／AVI 各 7 passed、0 failed、0 skipped，覆盖恢复、定位、内容交接和释放 |
| 150% 模拟缩放真实播放页 | HEVC 3 passed、0 failed、0 skipped；不等于真实混合 DPI 验收 |
| 最终无人干预 HEVC 实播 | 120 秒、112 个播放采样、12 项状态检查全通过，脚本／应用均退出 0 |

最终其余 24 个内置目标已通过；菜单测试修订只影响测试前置条件，主程序及原生跟随组件未再改变。没有将分开的复验写成最终单轮完整 25/25。

正式应用交互轮 `out/validation/libvlc-integration/owned-follow-production-hevc-final/`：240 秒、12 项状态检查全部为 true、0 QML 引擎警告、应用正常退出 0。前台实际观察 HEVC 画面、双语字幕和弹幕；通过 F11 返回窗口，在播放和暂停中各进行一次标题栏拖动，捕获原生移动循环进入／退出各 2 次。最终 31 个成功位置事件的同步后最大矩形差为 0px，当前差 0px、失败 0；原生补偿仅累计 7 次，80 秒与 110 秒报告均为 2 次，没有继续静止写入。全屏进出及最终身份检查通过，视频 HWND=34016928、连接保持同一实例；没有重新打开媒体。

此交互轮额外手动暂停，播放采样 215，低于脚本固定的 220 阈值，因此脚本明确失败；没有修改阈值或把交互轮算作稳定播放验收。其 `foreground-paused-after-move.png` 保存动作后的前台合成；没有高速拖动录像。菜单／定位草稿的自动真实 QML 合约与本次前台拖动记录分别计验，不宣称已逐项实测所有业务弹窗。

同一最终 EXE 的无人干预轮 `out/validation/libvlc-integration/owned-follow-production-unattended/` 正常完成 120 秒，112 个播放采样、12 项检查全通过、0 QML 引擎警告，脚本／应用均退出 0。15 个成功位置事件的同步后最大差 0px、当前差 0px、0 原生失败、累计 4 次原生补偿；全屏往返和最终连接／HWND 身份检查通过。displayedPictures=2771、lostPictures=3、playedAudioBuffers=4926，末尾工作集约 529.26 MiB。这是短功能回归，不是新的 30 分钟稳定性结论。

常规开发运行目录 `out/run/windows-libvlc-release/` 已同步相同最终 EXE 和本地运行库；没有启动或覆盖运行中的用户实例。可按下方命令启动，用户配置未清理。

实播环境：Windows 11、Qt 6.11.0、libVLC 3.0.23、Ryzen 5 5600（12 逻辑处理器）、RTX 4070（32.0.16.1742），标准屏幕 1920×1080、180Hz、缩放参数 1。日志确认 Qt D3D11 与 VLC D3D11VA；保留内置资源存续期间的不透明主窗回退，没有改写用户 Mica 偏好。媒体为既有 HEVC 1920×1080／23.976fps、AAC、1420.086 秒 MP4；AVI 样本为既有 80×48、5fps、60 秒、无音轨文件。

240 秒交互轮末尾工作集约 539.70 MiB、displayedPictures=5461、lostPictures=4、playedAudioBuffers=9425；保留 swapchain alphaBufferSize 及退出 External WM_DESTROY 警告。退出日志先释放 VLC 的 Direct3D11 资源再结束拥有的覆盖窗。同期运行自动回归，未控制系统负载，不作为受控性能、音画同步或内存泄漏结论；真实契约的 HEVC 定位还出现过 get_buffer／NAL 控制台警告，不宣称零编解码错误。

## 复现与边界

```powershell
pwsh -NoProfile -File scripts/build.ps1 -Preset windows-libvlc-release -Test
pwsh -NoProfile -File scripts/run.ps1 -Preset windows-libvlc-release -NoBuild
# 使用新的 RunName，避免覆盖已有证据
pwsh -NoProfile -File scripts/validate-libvlc-playback.ps1 -Video '<本地视频完整路径>' -Seconds 120 -Fullscreen -RunName owned-follow-new -NoBuild
```

实播脚本自动检查暂停／恢复、定位、页面切换、全屏及连接身份；标题栏拖动、视频／字幕／弹幕与弹窗视觉仍需要前台实测。截图只证明动作后的合成；消息回调后的矩形相等不代表两个独立 GPU 表面逐帧原子呈现。高速连续拖动录像、真实多屏混合 DPI、触摸及本次最终构建的 30 分钟稳定性仍需专项验收，不能沿用探针或上一轮的长测结论。正式内置播放器分发继续不纳入本轮。
