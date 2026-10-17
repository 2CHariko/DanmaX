# libVLC 原生输出最小探针

仅用于验证内置播放的窗口合成与基本控制，不参与根项目生产构建，不注册正式 MediaPlaybackBackend，不修改应用配置。

## 准备与运行

依赖现有项目内 Qt 6.11.0、CMake、Ninja，以及 `.local/toolchain.local.json` 明确指定的系统 MSVC／SDK。先运行独立准备步骤，构建和运行不联网：

```powershell
pwsh -File scripts/prepare-libvlc-probe.ps1
# 已缓存时可用 -Offline，仍会校验两个归档的 SHA-256。
pwsh -File scripts/probe-libvlc.ps1 -Video '<本地视频完整路径>'
pwsh -File scripts/probe-libvlc.ps1 -Video '<本地视频完整路径>' -NoBuild -KeepOpen -Scale 1.5
```

`-BuildOnly` 只构建，`-NoBuild` 使用已有探针 EXE，`-RunName` 指定不覆盖旧证据的目录名，`-KeepOpen` 在自动检查结束后保留交互窗口；默认完成后正常释放并退出。Space 暂停／继续，F11 切换全屏，Escape 返回窗口。界面另有定位、截图和视频上方浮层按钮。自动检查会主动缩窗、全屏并恢复。

构建在 `out/build/libvlc-probe-release/`，报告、截图、libVLC 日志与本轮缓存在 `out/validation/libvlc-probe/<RunName>/`。TEMP/TMP、QML 缓存使用项目公共脚本规定的 `.cache/`。未安装 VLC 的独立部署、最终发布插件清单和单 EXE 封装不属于此探针。

## 实现与证据范围

- Qt WindowContainer 嵌入自建的原生视频 QWindow；libVLC 通过 set_hwnd 输出到该窗口。没有视频帧回调，也没有 QImage 视频上传路径。
- 视频宿主使用不透明黑色 backing store；主窗口保持不透明。弹幕与 Fluent 浮层在 owned 顶层透明 QQuickWindow 中，跟随视频宿主的位置、大小与显隐。
- 直接编译复用现有 DanmakuEngine／DanmakuItem，无第二套轨道或文字渲染实现。单一 GUI 帧定时器使用实际单调时间差，后台每 100ms 校准 libVLC 状态；暂停冻结、定位清空并等待位置读回。
- libVLC 的加载、媒体、控制和释放均在同一工作线程，日志回调只写有界内存列表。停止超过 8 秒会报告仍在等待，不强制终止线程，不把隐藏窗口当成释放成功。此机制不保证任意第三方解码器都能在固定时间内退出。
- `result.json` 的 `passed` 仅代表自动状态／几何检查通过。视觉合成必须另行检查前台截图；`captures.foreground=false` 的截图不作为视觉通过证据。计时器 P95 不是实际视频 FPS。
- 自动检查未经过正式 SeekController，只验证探针自身暂停定位后的时间读回（0.5 秒窗口），不代表帧精确定位、公共后端契约或产品历史恢复通过。

## 依赖与许可

`toolchain/libvlc.lock.json` 锁定 VideoLAN 官方 VLC 3.0.23 x64 ZIP 和同版本源码归档。运行库与公开头文件位于 `.deps/libvlc-probe/vlc-3.0.23/`；归档缓存位于 `.cache/downloads/libvlc-probe/`。源码仅提取 include/vlc 与 COPYING.LIB，不编译 VLC，也不引入其他 Qt 绑定。

libVLC 公开接口采用 LGPL-2.1-or-later。开发探针保留完整上游运行归档，其中还包含 GPL 插件及其他组件，保留 COPYING.txt、COPYING.LIB、THANKS.txt 等材料；本目录没有给出生产可分发插件清单。C API 使用动态加载与库自身的释放函数，避免 MSVC 与上游工具链之间交换分配器所有权。

完整实机记录见 [LIBVLC_EMBEDDING_PROBE.md](../../docs/LIBVLC_EMBEDDING_PROBE.md)。
