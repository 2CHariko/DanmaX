# 独立顶层弹幕窗原生跟随验证

> [!NOTE]
> **文档状态：独立同步策略探针**。该探针验证的子类化跟随策略已集成至正式播放页，见 [覆盖层原生跟随设计](../../playback/overlay-follow.md)。

日期：2026-10-18。继续采用“原生视频子窗＋主窗拥有的独立透明顶层弹幕窗”，验证移动跟随的改进空间。独立 Release 探针，Qt 6.11.0、libVLC 3.0.23、现有 QWindowKit；此处记录的是正式接入前的实验，未在该次实验中修改生产同步策略、播放器后端或弹幕引擎。

后续已将验证路径接入正式 VideoViewportController，见 [生产接入记录](../../playback/overlay-follow.md)。下文 A/B 为所列历史 EXE 的结果；重新编译探针时，`Qt` 模式会使用当前正式控制器，已经不能作为旧版延后同步基线。不得把新的构建与下列历史哈希混用。

## 结论

**当前窗口结构仍可优化，原生窗口消息触发的同步路径已通过本机最小验证。** 同一份 EXE 中，现有 Qt 视口同步基线在播放／暂停的程序移动消息结束后，测得矩形最大差 64px；修订后的原生路径在相应采样点为 0px，并通过 18 项状态／几何检查。

这是窗口几何同步的结果，**不是逐帧光学延迟或两个独立表面原子呈现的保证**。每次移动量是探针设定的离散步长，最大差会随输入而变；不能将 64px 当作用户日常拖动的固定偏差。

该次实验结束时正式应用仍使用原有实现，原生同步仅在探针的 `-FollowSync Native` 模式运行。后续接入仍未切换为同父子窗、软件场景图或视频像素合成；其他两条子窗路线见 [此前对照](libvlc-child-windows-probe.md)。

## 实验方式

- 两组直接编译复用正式 `VideoViewportController.cpp`，共用现有 DanmakuEngine／DanmakuItem、libVLC 工作线程、16ms 单一探针帧定时器和同一个 QML 界面。挂接现有 QWindowKit 的公开 QuickWindowAgent，主窗保持不透明。
- Qt 基线将覆盖窗交给正式控制器，其 `QueuedConnection`／`singleShot(0)`／QML 视口坐标路径保持原样。两组还有相同的帧内兜底调用；这个探针定时器不等同于产品的 afterAnimating／33ms 维护调度，因此结论限于呈现组件与原生几何，不是完整生产界面的回归。
- 原生组仍用正式控制器管理视频视口，覆盖窗几何由独立实验适配器管理，避免两个写入者互相覆盖。GUI 线程使用 Windows SDK SetWindowSubclass 观察主窗和视频窗口的消息；先调用 DefSubclassProc 保留 Qt／QWindowKit 消息链，再在 WM_WINDOWPOSCHANGED／WM_EXITSIZEMOVE 后跟随视频 HWND。
- 几何目标用 GetWindowRect 校验。优先通过视频 QWindow 的平台坐标映射和公开 setGeometry 更新覆盖窗，维护 Qt 的逻辑几何及场景图尺寸；读回 HWND 若仍有差，才进行一次不激活、不改变层级的 SetWindowPos 修正。没有全局 DPR 乘法、额外媒体轮询或第二套弹幕时钟。
- 有界保存最多 20000 条原生消息观测，回调内不写 JSON；报告在普通检查点／1 秒证据定时器／正常退出时写盘。原生回调耗时只统计当前线程这段代码，不等于显示延迟。
- 分别记录 DefSubclassProc 前后的原生矩形差。播放／暂停两组程序移动各约 20 次；暂停组另检查媒体位置保持不变。手动阶段通过 computer-use 执行标题栏拖动、恢复播放、全屏、弹窗与点击穿透。

## 最终 A/B 结果

证据根目录：`out/validation/libvlc-probe/`。

`owned-follow-native-04` 与 `owned-follow-qt-05` 使用相同 EXE，SHA-256：`96B3B7527D72189183178FBF3228FB9ADB78CE35827B8BC526DB5251BC582FAF`。各轮 environment.json 记录运行参数、EXE／运行库／锁文件和关键探针／正式视口源码哈希。

| 项目 | Qt 基线 `owned-follow-qt-05` | 原生路径 `owned-follow-native-04` |
|---|---|---|
| 自动检查 | 16/18；全屏矩形与暂停后最终对齐失败 | 18/18 |
| 播放中纯移动，消息结束后最大差 | 64px，22/22 个采样有差 | 0px，21/21 个采样无差 |
| 暂停纯移动，消息结束后最大差 | 64px，17/17 个采样有差 | 0px，17/17 个采样无差 |
| 标准窗口最终矩形差 | 1px，覆盖窗顶部比视频窗口高 1px | 0px |
| 播放／暂停移动回调 P95 | 观察器取整为 0µs，不执行原生组的跟随工作 | 924／915µs，包含 Qt 几何维护和读回 |
| HWND 身份 | 主窗／视频／覆盖窗各自未变 | 主窗／视频／覆盖窗各自未变 |
| 正常释放 | 1097ms；探针退出 2，脚本报告失败，保留失败证据 | 130ms；KeepOpen 正常关闭，shutdownCompleted=true |

基线最终示例：视频 RECT `[412,151,1508,811]`，覆盖窗 RECT `[412,150,1508,810]`。原生组返回窗口后两者均为 `[412,151,1508,811]`。这个 1px 差与该探针的 QML 视口／原生窗口坐标映射有关，没有据此宣称所有生产布局都会固定偏移 1px。

原生组视频 HWND=1772284、覆盖窗 HWND=789098、主窗 HWND=593098，覆盖窗 GW_OWNER 为主窗，全屏往返后与初始值相同。每轮只建立一次 libVLC player，没有在窗口操作时重新打开媒体；探针没有产品 MediaSession／sessionToken，不将这些数值作为公共连接契约验收。

### 实机交互与图像

- 标准原生组完成暂停和播放中的真实标题栏拖动：捕获 WM_ENTERSIZEMOVE=2、WM_EXITSIZEMOVE=2、WM_MOVING=2。手动阶段两个主窗纯移动采样的入口差最大 160px、回调结束后均为 0px。这里只捕获离散输入产生的消息，没有高速录像，不证明所有显示帧均无视觉偏移。
- 全屏和返回窗口均实际看到视频、双语字幕、GPU 弹幕和绿色视口参考线；独立 Fluent Popup.Window 在其上方正常显示，Escape 先关闭弹窗，再退出全屏。视频与弹幕仍是独立 GPU 窗口表面。
- 点击弹幕区域后，视频宿主的原生 WM_LBUTTONDOWN 计数为 1，穿透实测通过。
- 自动缩窗、最大化、最小化恢复检查通过，最小化期间媒体继续推进。尝试鼠标边缘拖动没有捕获 WM_SIZING，本次不将其算作原生交互缩放验收。
- 探针没有复刻生产全屏控制条的自动隐藏、全部业务弹窗、页面切换或来源交接；没有将既有产品验收成绩算入本轮。

前台截图留存在标准原生轮的 `foreground-fullscreen-popup.png`、`foreground-window-after-fullscreen.png`；基线截图为 `owned-follow-qt-02/foreground-paused-after-move.png`。PNG 是动作后的前台合成证据，不能替代移动过程的录像。

## 缩放、回归与失败轮次

`owned-follow-native150-06` 使用相同 EXE，QT_SCALE_FACTOR=1.5：18/18，播放／暂停程序移动入口差最大 96px，回调结束后均为 0px。前台看到放大后的真实视频、字幕、弹幕并执行标题栏拖动与点击，图像在该轮 `foreground-paused-after-move.png`。模拟缩放不代表真实混合系统 DPI；窗口较高时部分界面超出屏幕，本轮不作为窄屏／文字放大完整可访问性验收。

`owned-follow-native-avi-07` 在同一 EXE 上使用已有 80×48、5fps、60 秒无音轨 AVI，18/18、0 QML 警告、脚本退出 0，正常释放 53ms。标准／150%／AVI 三轮原生路径和最终 Qt 基线的原始几何样本均没有 GetWindowRect 无效读回；汇总保存在 `out/validation/libvlc-probe/owned-follow-comparison.json`。

默认非 Follow 探针重新构建后，`owned-follow-default-regression-08` 的 AVI 回归 9/9、0 QML 警告、脚本退出 0；仅证明旧探针入口的自动状态回归，没有追加生产应用或实际 AVI 画面的视觉验收。

早期轮次保留：`owned-follow-qt-01` 为初始诊断，尚包含旧探针的额外直接几何连接；`owned-follow-qt-02` 去除该连接后仍复现偏差，16/18；`owned-follow-native-03` 第一版直接原生写入有若干尺寸／几何检查失败，不能算通过。修订版统一窗口矩形口径并维护 Qt 几何状态后，才以 04／05 为最终对照。没有修改历史结果或放宽几何断言。

## 环境、风险与后续

Ryzen 5 5600、12 逻辑处理器、RTX 4070、驱动 32.0.16.1742。标准最终两组 Qt 均报告 X41、1920×1080、DPR=1、180Hz；真实媒体为已有 HEVC 1920×1080／23.976fps、AAC、1420.086 秒 MP4。测试字体 Microsoft YaHei UI、28 逻辑单位、1 单位描边；标准原生组结束时 11 条活动弹幕。Release，约 233 秒总运行，含等待交互、暂停、定位和窗口切换，不是稳定播放时长。

标准原生组 lostPictures 累计 12，Qt 基线也为 12；基线控制台出现 HEVC get_buffer／NAL 解析错误。报告和原始日志保留，不宣称零掉帧或编解码稳定性通过。原生组 GUI tick P95 约 16.83ms，属于探针定时器间隔，不是视频 FPS。未做等时长 CPU／显存／峰值内存对比，不把回调耗时扩展成整体性能结论。

构建成功但 CMake 有 QWindowKit 对 Qt Private target 的依赖元数据警告和可选 Vulkan headers 未找到提示；QWindowKit 为现有锁定静态依赖，本轮只调用其公开 API，没有新增 Qt 私有 API 或自定义图形后端。构建和运行均离线，配置与缓存留在项目内。

该次实验提出的下一步是将原生跟随封装进唯一 GUI 呈现组件，管理 HWND 消息挂接／撤销、模式交接和显示前定位，保留 QWindowKit 与现有弹幕时钟。其后续状态见生产接入记录；本实验不承诺已解决用户环境里的所有偏移。

## 复现

依赖继续使用 `toolchain/libvlc.lock.json` 与 `toolchain/qwindowkit.lock.json`；在构建前显式准备项目本地依赖，不在配置／构建／运行期间联网。构建缓存独立为 `out/build/libvlc-follow-probe-release/`。

```powershell
# 构建并运行原生路径，自动检查后保留窗口供实机操作
pwsh -File scripts/probe-libvlc.ps1 -Video '<本地视频完整路径>' -FollowSync Native -KeepOpen -RunName owned-native-new
# 正常关闭上一窗口后，在同一 EXE 上运行 Qt 基线
# 基线可能退出 2；这是被记录的几何检查失败，不要覆盖失败目录。
pwsh -File scripts/probe-libvlc.ps1 -Video '<本地视频完整路径>' -FollowSync Qt -NoBuild -RunName owned-qt-new
# 同一 EXE 的模拟缩放对照
pwsh -File scripts/probe-libvlc.ps1 -Video '<本地视频完整路径>' -FollowSync Native -NoBuild -Scale 1.5 -KeepOpen -RunName owned-native150-new
```

`-FollowSync` 与 `-ChildLayers`／`-NativeLayeredChild`／`-QuickSoftware` 互斥，默认 Off 保留旧探针。脚本拒绝覆盖同一仍在运行的 EXE；所有轮次先后台停止／释放 VLC 再退出，不强制结束进程或工作线程。调试状态、几何检查和实机视觉验收分别记录，不以自动 passed 代替完整生产验收。
