# libVLC 内置播放最小验证

> [!NOTE]
> **文档状态：独立最小探针**。本探针的 27 项检查仅代表原型结果，正式内置播放接入验收见 [内置播放页接入](../../playback/libvlc-integration.md)。

历史独立探针记录；后续正式开发接入见 [接入报告](../../playback/libvlc-integration.md)。

日期：2026-10-18。范围：独立开发探针，Windows x64、Qt 6.11.0、libVLC 3.0.23。没有修改生产播放流程、领域引擎、渲染器、配置或归档，没有注册正式内置后端。

## 结论

本机已证明“应用自有原生视频窗口＋独立透明 Qt Quick 覆盖窗”能够显示真实 HEVC 视频、现有 DanmakuItem 弹幕和可交互的 Fluent 浮层。暂停冻结、暂停定位读回、恢复、缩窗、全屏及返回窗口的最小检查通过，可据此继续设计正式内置后端。不能据此宣称内置播放器已完成产品接入。

探针没有使用视频内存帧回调、自定义 RHI、Qt 私有 API 或第三方 Qt-VLC 绑定。libVLC 直接管理解码、声音和原生视频输出，现有引擎继续调度及绘制弹幕。

## 实现与复现

- 源码：`tools/libvlc-probe/`，独立 CMake 工程；复现命令见该目录 README。
- 独立显式依赖准备：`scripts/prepare-libvlc-probe.ps1`；版本、官方地址、两个归档的 SHA-256、许可与用途锁定于 `toolchain/libvlc.lock.json`。运行库及公开头文件位于 `.deps/libvlc-probe/`，不依赖本机安装的 VLC。
- 离线构建／运行：`scripts/probe-libvlc.ps1`，Release 构建位于 `out/build/libvlc-probe-release/`。复用项目公共工具定位及环境隔离；真实外部系统 MSVC／SDK 路径仅记录在本地产物中。
- 视频为 WindowContainer 内的原生 QWindow；其不透明黑色 backing store 负责宿主背景。弹幕和浮层位于独立 owned 顶层 QQuickWindow，跟随视频区域的屏幕坐标、大小和显隐。主窗不启用全局透明 alpha。
- libVLC C API 使用动态加载，在单一后台线程串行执行。GUI 单一 16ms 帧定时器使用实际单调时间差推进弹幕，后台 100ms 采样用于校准；不承诺视频与弹幕共用同一交换链或逐帧锁定。

## 本机结果

硬件：AMD Ryzen 5 5600，12 个逻辑处理器，NVIDIA GeForce RTX 4070（驱动 32.0.16.1742），2560×1440、165.002Hz 显示器，Release。标准缩放 DPR=1；第二轮用 QT_SCALE_FACTOR=1.5 模拟 DPR=1.5，不等于混合系统 DPI 验收。

真实样本为此前用户提供的第 1 集 MP4：HEVC/H.265 1920×1080、23.976 fps、AAC，时长 1420.086 秒。合成 AVI 为已有项目测试产物，80×48、5fps、60 秒，无音轨。没有把视频内容复制或提交进 Git。

| 最终轮次 | 自动检查 | 视觉证据 | 正常释放耗时 |
|---|---|---|---|
| `hevc-opaque-main` | 9/9 通过，0 QML 警告 | 前台全屏浮层截图、窗口暂停截图；computer-use 目视确认视频／字幕／弹幕／浮层，实际点击暂停与关闭浮层 | 1190ms |
| `hevc-scale150` | 9/9 通过，0 QML 警告 | computer-use 目视确认放大后的真实画面／弹幕／浮层，手动触发前台截图 | 146ms |
| `avi-final` | 9/9 通过，0 QML 警告，脚本退出 0 | 基础状态回归，不纳入真实视频视觉验收 | 61ms |

九项自动检查为：视频显示计数与现有文字节点建立、暂停冻结媒体和弹幕、定位清空活动弹幕、暂停定位位置读回、定位后恢复、缩窗尺寸／所有者／位置对齐、全屏尺寸对齐、返回窗口、无 QML 警告。两轮暂停观察均为媒体变化 0 秒、弹幕首对象变化 0 像素；请求 60.25 秒，均读回 60.25 秒。探针判断窗口为 0.5 秒，不扩大成帧级精确保证。

日志确认选中 `d3d11va`，出现 `Using D3D11VA (NVIDIA GeForce RTX 4070...) for hardware decoding`；声音输出存在已播放音频缓冲计数。没有验收扬声器实际可听或音画同步。

两轮结束快照分别有 10／11 条活动弹幕，进程工作集约 221.6／215.8MiB，GUI 定时器 P95 约 16.94／16.72ms。这些是包含定位、窗口切换及暂停的短探针快照，不是性能基准、峰值内存或视频 FPS。标准轮 libVLC lostPictures 为 10，150% 轮为 0；日志保留 hardware acceleration picture allocation failed，不能据此宣称零掉帧或解码稳定性已验收。

证据根目录为 `out/validation/libvlc-probe/`。每轮包含 `environment.json`（工具链、EXE／运行库／锁哈希、CPU／GPU）、`result.json`（读回、断言、尺寸、截图资格、退出）、`vlc.log` 和 PNG。标准轮 `fullscreen-popup.png` 与 `manual.png`、150% 轮 `manual.png` 均有前台资格并已目视检查。其余 foreground=false 的自动截图不作为视觉通过证据。

## 发现并修正的问题

1. 官方 Windows ZIP 未附开发头文件，改为显式校验同版本源码归档并仅提取公开头文件与库许可证。未从用户 VLC 安装或其他项目补依赖。
2. 收紧 DLL 查找后 Qt Fluent 插件无法载入，加入已加载 Qt6Core 所在的明确目录；不恢复隐式 PATH DLL 查找。libVLC 仍从明确的项目内绝对路径加载。
3. VLC 3 Windows 的 new_path 将 Qt 正斜杠盘符路径判为不支持的相对路径，改为传入 UTF-8 的 Windows 原生分隔符。中文、空格和括号路径的真实 MP4 已成功打开。
4. 初版透明 Qt Quick 子窗口组合出现白色残留，自动几何检查未发现；改用独立 owned 透明覆盖窗。本轮没有验证该子窗口路线的所有可能修复方式，不能据此判断 Qt 全面不支持。
5. 全局启用 Quick alpha 后，原生视频的黑边区域可透出其他窗口。保持主窗口不透明、宿主使用黑色底板后，前台全屏截图黑边正确。该失败过程证明状态／尺寸通过不能替代实际合成检查。

早期失败目录保留，不纳入最终通过轮次：`avi-initial`、`avi-dll-search`、`avi-media-diagnostic`、`hevc-first`、`hevc-owned-overlay`、`hevc-black-host`；`avi-native-path` 只验证早期基础状态。最终以本节指定的完整实现和证据为准。

## 尚未验证与正式接入边界

- 未注册正式 MediaPlaybackBackend，未接产品 SeekController／MediaSession、历史恢复、自动匹配和视频／独立 XML 交接，因此未新增公共后端契约验收。
- 未验收长时漂移、4K／HDR、更多编码与损坏文件、连续大跨度定位、网络／映射路径、真实多显示器混合 DPI、主题／高对比度／无障碍、遮挡与最小化等完整窗口策略。
- 未在没有 VLC 的干净系统验证部署。虽然运行库来自项目本地目录，但 Qt 仍是开发 SDK 运行路径；没有生成生产包、最小插件清单或单 EXE 封装。
- 工作线程可防止普通停止阻塞 GUI，但不能强制安全取消卡住的原生解码调用。超过 8 秒只报告仍等待，本轮正常退出不能证明任意媒体都可有界释放。第三方库同进程崩溃隔离尚未实现。
- 正式实现仍需抽离视频视口／呈现策略、声明能力及内容代次、复用唯一定位事务，并验证覆盖窗与任务生命周期。下一步应围绕当前已验证的原生输出与独立覆盖窗路线实施。

## 官方参考

- [libVLC 3 媒体播放器与 HWND／内存回调接口](https://videolan.videolan.me/vlc-3.0/group__libvlc__media__player.html)
- [Qt 6.11 WindowContainer 叠放限制](https://doc.qt.io/qt-6.11/qml-qtquick-windowcontainer.html)
- [VideoLAN 3.0.23 官方 Windows 归档](https://download.videolan.org/pub/videolan/vlc/3.0.23/win64/)
