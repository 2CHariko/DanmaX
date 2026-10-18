# 同父视频子窗与弹幕子窗验证

日期：2026-10-18。独立 Release 探针，Qt 6.11.0、libVLC 3.0.23、Windows x64。沿用锁定依赖与既有 DanmakuEngine／DanmakuItem，没有修改生产播放页、播放器后端、领域引擎或产品配置。

后续保留原生视频子窗＋独立透明顶层弹幕窗的优化对照，另见 [原生跟随验证](OWNED_OVERLAY_FOLLOW_PROBE.md)；不改变本文两条同父子窗路线的验收结论。

## 结论

**同父子窗结构可以工作，但当前 Qt Quick D3D11 透明子窗没有通过合成验收。Qt 官方软件场景图＋透明 layered 子窗完成了本机最小验证。** 后者仍由 VLC 使用 D3D11／D3D11VA 解码和输出视频，弹幕通过 Qt 软件场景图绘制；不是 GPU 统一合成，也没有把弹幕写入 VLC 的视频像素或字幕接口。

结构为不透明主窗下两个兄弟 HWND：一个黑底原生视频宿主（VLC 在其下建立输出窗口），一个透明 Qt Quick 弹幕子窗。两者的位置和大小完全交给 WindowContainer，没有使用全局坐标跟随。交互浮层继续使用主窗拥有的 Popup.Window。

这证明“播放器输出与弹幕绘制解耦、共享原生父窗口”的方向有可行基础。**尚不能直接替换生产覆盖窗**：软件适配目前作用于整个探针的 Qt Quick 界面；生产界面保持 D3D11 的条件下，如何单独提供弹幕软件表面仍待设计。GPU 弹幕子窗也没有找到已验证的透明呈现方式。

## 两条路径的区别

两条探针路径使用相同的同父兄弟子窗结构，区别在 Qt Quick 的绘制后端和透明表面的提交方式。视频播放控制、弹幕调度和弹幕绘制仍然分离，软件路径没有把弹幕交给 VLC 处理。

| 项目 | 路径 A：Qt Quick D3D11 | 路径 B：Qt Quick 软件场景图 |
|---|---|---|
| 视频解码与输出 | libVLC 原生 HWND 输出，本机使用 D3D11／D3D11VA | 相同；视频没有改为 CPU 帧回调或 QImage 上传 |
| 弹幕绘制 | 复用 DanmakuItem 文字缓存及 GPU 场景图，在透明 Quick 子窗中绘制 | 复用相同弹幕引擎和绘制项，由 Qt 软件场景图绘制 |
| 透明呈现 | Qt D3D11 窗口表面；另有原生 layered／穿透样式对照 | 透明 layered 子窗，由 Qt 软件 backing store 提供逐像素 alpha |
| 输入与位置 | WindowContainer 管理同父关系与几何；原生样式对照可穿透输入 | 相同；标准及 150% 轮均实测点击到达视频子窗 |
| 本机合成结果 | 未通过：透出其他应用或黑底遮住视频；补穿透样式后仍失败 | 最小验证通过：真实视频、字幕、弹幕可叠加，标准轮另确认全屏与 Fluent 浮层 |
| 自动检查 | 最后一轮 15/15，但不能抵消视觉合成失败 | 两轮 HEVC 均 15/15；AVI 15/15、正常退出 0 |
| 开销与影响 | 由 GPU 执行场景图合成；没有与软件路径进行等负载性能对比 | 更依赖 CPU 绘制和像素数据传递；当前开关影响整个探针的 Quick UI，仅有下文短时整进程快照 |
| 接入状态 | 仅为独立探针，未替换正式弹幕窗 | 仅为独立探针，未替换正式弹幕窗 |

“软件”描述的是 Qt Quick 绘制方式，不表示视频解码或 Windows 最终合成都改由 CPU 完成。两条路径均保留视频与弹幕各自的窗口表面，**都不是把视频纹理和弹幕放入同一合成目标的 GPU 统一合成**；后者仍是待单独验证的候选方案。

### 与当前正式实现的关系

本次记录时，正式内置播放页仍采用“原生视频子窗＋主窗拥有的独立透明顶层弹幕窗”。视频经 `libvlc_media_player_set_hwnd` 输出到 WindowContainer 中；弹幕由现有 DanmakuItem／Qt Quick GPU 场景图绘制，VideoViewportController 使用视频区域的屏幕坐标更新覆盖窗位置和大小。**正式实现尚未切换为这里的任一同父子窗路径。**

源码入口为 [PlayerPage.qml](../qml/pages/PlayerPage.qml)、[VideoViewportController.cpp](../src/platform/windows/VideoViewportController.cpp)、[OverlayWindow.qml](../qml/OverlayWindow.qml) 和 [LibVlcBackend.cpp](../src/infrastructure/LibVlcBackend.cpp)。当前正式覆盖窗需要额外同步屏幕坐标；这与兄弟子窗由父窗口统一带动的位置机制不同。用户报告的移动偏移仍需在正式实现中定位，探针的坐标检查不能视为该问题已经修复。

## 对照与证据

目录均位于 `out/validation/libvlc-probe/`，失败轮次完整保留。

| 轮次 | 表面配置 | 自动状态／几何检查 | 前台实际观察 | 正常释放 |
|---|---|---|---|---|
| `child-parent-hevc-01` | Quick D3D11，主窗 alpha=8，弹幕 alpha=8 | 12/13；输入样式与身份的合并断言失败 | 弹幕可见，视频区域透出其他应用 | 140ms |
| `child-parent-opaque-02` | Quick D3D11，主窗 alpha=0，弹幕 alpha=8 | 14/15；穿透样式失败，身份通过 | 不再透出其他应用，但变成黑底弹幕，视频被挡 | 119ms |
| `child-parent-layered-03` | 上一配置＋原生 layered／穿透样式、兼容 manifest | 15/15 | 仍为黑底弹幕；隐藏弹幕子窗立即出现正常视频，重显后重新遮黑 | 157ms |
| `child-parent-software-04` | Quick software，主窗 alpha=0，透明 layered 弹幕子窗 | 15/15 | 真实视频、双语字幕、弹幕、全屏及 Fluent 浮层正常叠加；Escape 先关浮层，再退出全屏 | 138ms |
| `child-parent-software150-05` | 上一配置，QT_SCALE_FACTOR=1.5 | 15/15，实际 Quick graphicsApi=Software | 放大后的真实画面、字幕、弹幕正常；点击穿透 | 157ms |
| `child-change-owned-regression-06` | 原探针 owned 顶层覆盖窗、Quick D3D11，AVI | 9/9，脚本退出 0 | 仅自动回归，不新增视觉验收结论 | 68ms |
| `child-parent-software-avi-07` | 软件透明子窗，AVI | 15/15，实际 Quick graphicsApi=Software，脚本退出 0 | 仅自动回归，不新增视觉验收结论 | 54ms |

前三轮的实机观察已在本次 computer-use 会话中检查；不能使用 libVLC displayedPictures 计数来推翻黑底／透出应用的视觉失败。第一轮的合并断言随后拆为身份、WS_CHILD、WS_EX_TRANSPARENT 三项，第二轮明确定位到缺少穿透样式；因此总数不同。

`child-parent-layered-03` 的早期 `nativeLayeredSetup.styleError=6` 来自错误读取成功调用后残留的 GetLastError，不能视为配置失败。后续版本改为仅在 SetWindowLongPtr 返回 0 时读取错误；软件轮为 styleError=0，原生样式和实际点击另有独立证据。没有修改旧报告。

软件路径不调用 SetLayeredWindowAttributes：让 Qt 的软件 backing store 提供逐像素 alpha。微软文档说明，调用前者会阻止后续 UpdateLayeredWindow，除非重置 layered 样式。这与 D3D11 对照中的全局 alpha 设置有明确区别。

### 移动、输入与身份

- 标准软件轮记录 35 条主窗移动消息，两个子窗屏幕矩形原点的最大偏差为 0px；150% 轮 29 条，完整矩形最大偏差为 0px。包含程序移动、全屏／最大化／最小化及恢复；标准轮额外操作了真实标题栏拖动。
- `WM_MOVING` 计数仍为 0。本轮事件过滤器没有证明覆盖 Windows 模态拖动循环的全部阶段，也没有测量两个交换链的逐帧呈现差。结论限于已采到的原生坐标和前后台视觉检查，不宣称消除了所有拖动视觉延迟。
- 标准软件轮和 150% 轮均记录 videoClicks=1、layerClicks=0，WindowFromPoint 命中视频；穿透实测通过。D3D11 layered 轮也能穿透，所以穿透通过不能证明透明合成通过。
- 标准软件轮主窗 HWND=3474092，视频 HWND=2887208，弹幕 HWND=9178816；两个 GetParent 都为主窗。全屏往返后与初始值相同。150% 轮也保持三者身份不变；原始数值在 result.json。
- 所有轮次先后台停止／释放 libVLC，再退出并销毁窗口，没有重建运行中的视频 HWND、强制退出线程或结束播放器进程。KeepOpen 轮次通过正常关闭窗口结束，记录 shutdownCompleted；退出码仅在非 KeepOpen 回归中检查。

本次保存的前台原始合成截图：

- `child-parent-software-04/foreground-fullscreen-popup.png`：真实视频、字幕、弹幕及独立 Fluent 浮层。
- `child-parent-software-04/foreground-window-after-fullscreen.png`：返回窗口后继续显示。
- `child-parent-software150-05/foreground-window.png`：模拟 150% 缩放。

以上由 computer-use 的窗口捕获留存。探针自带 QScreen 抓图在部分轮次只取得黑色内容，不能将文件已保存或 foreground=true 单独视为有效图像证据；必须检查实际内容。

## 环境与性能边界

Ryzen 5 5600（12 逻辑处理器）、RTX 4070、驱动 32.0.16.1742。标准软件轮 Qt 报告 X41、1920×1080、DPR=1、165.001Hz；150% 轮报告逻辑 1280×720、DPR=1.5、180Hz。两轮刷新率不同，不作为严格性能对比或真实混合 DPI 验收。

真实媒体为现有 HEVC 1920×1080／23.976fps、AAC、1420.086 秒的本地 MP4；未复制进 Git。标准软件轮日志确认 D3D11VA 使用 RTX 4070。AVI 回归使用已有 80×48／5fps／60 秒无音轨样本。扬声器可听性和音画同步本轮未验收。

标准软件轮约 142 秒、结束时 12 条活动长文字，字体 28 逻辑单位、描边 1；窗口和全屏均经过检查。10.012 秒整进程快照消耗 CPU 1.016 秒，即约 10.1% 单个逻辑核，工作集约 358.4MiB、PrivateMemorySize 389.1MiB，见该轮 cpu-snapshot.json。这包含 VLC、主窗、弹幕绘制和探针诊断写盘，不是弹幕单独开销、整机 CPU 百分比或密集弹幕性能基准。

标准软件轮 libVLC lostPictures 累计 22，150% 轮 10；包含定位、窗口状态切换及最小化，未做稳定播放阶段归因。日志还有 SetThumbNailClip 等警告。没有宣称零掉帧、渲染 FPS、连续 30 分钟稳定性、4K／HDR、高密度弹幕、触摸、真实多显示器、混合 DPI、Mica 或生产菜单／拖放验收。

## 复现与后续边界

先显式运行现有 `scripts/prepare-libvlc-probe.ps1`，离线缓存可用时添加 `-Offline`。构建／运行不联网，不查找系统 VLC，不新增依赖或永久 PATH。

```powershell
# 软件透明子窗的可用对照
pwsh -File scripts/probe-libvlc.ps1 -Video '<本地视频完整路径>' -ChildLayers -NativeLayeredChild -QuickSoftware -KeepOpen -RunName child-software-new
# 同样的窗口关系，D3D11 Quick 透明合成失败对照
pwsh -File scripts/probe-libvlc.ps1 -Video '<本地视频完整路径>' -ChildLayers -NativeLayeredChild -KeepOpen -RunName child-d3d11-new
# 放大模拟；不代表系统混合 DPI
pwsh -File scripts/probe-libvlc.ps1 -Video '<本地视频完整路径>' -ChildLayers -NativeLayeredChild -QuickSoftware -NoBuild -Scale 1.5 -KeepOpen -RunName child-software150-new
```

关闭旧探针并等待正常退出后再运行下一轮；脚本拒绝覆盖仍在运行的同一 EXE。界面的“弹幕层”按钮仅控制弹幕子窗显隐，不停止媒体；“浮层”为独立官方 Popup.Window。自动检查复用探针的唯一引擎和帧驱动，不注册生产后端、不引入第二套产品弹幕时钟；探针定位仍不构成 SeekController 契约验收。

继续 GPU 路线时，应保持播放控制、视频输出适配与弹幕绘制三者分离。对于能输出 HWND 的内核，兄弟子窗仍可作为呈现接口候选；对于 GPU 帧／纹理输出，应另行验证共同合成目标与同步、生命周期契约。不能把本轮软件透明成功扩大为任意内核、任意 GPU 后端都可直接叠加，也不建议据此把整个生产 UI 切为软件渲染。

## 官方依据

- [Qt 6.11 WindowContainer](https://doc.qt.io/qt-6.11/qml-qtquick-windowcontainer.html)：管理原生子窗，并示例用独立子窗覆盖嵌入窗口；没有承诺所有图形后端均支持所需透明混合。
- [Qt 6.11 软件场景图](https://doc.qt.io/qt-6.11/qtquick-visualcanvas-adaptations-software.html)：公开软件适配及能力限制。
- [Qt 6.11.0 Windows 平台实现](https://github.com/qt/qtbase/blob/v6.11.0/src/plugins/platforms/windows/qwindowswindow.cpp)：WindowTransparentForInput 对应原生穿透样式的代码位于 topLevel 分支，与本轮子窗样式观察一致；仅用于研究，没有调用 Qt 私有 API。
- [微软 Window Features](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features)：子窗随父窗移动，Windows 8 起支持 layered 子窗，逐像素 alpha／输入穿透及两种 layered 更新方式的限制。
