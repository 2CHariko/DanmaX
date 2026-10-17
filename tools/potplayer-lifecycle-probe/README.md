# PotPlayer 生命周期最小探针

正式接入现已另行实现于 `src/platform/windows/PotPlayerBackend.*`，见 [统一播放说明](../../docs/NOW_PLAYING.md) 和 [验收状态](../../docs/FRAMEWORK_STATUS.md)。本目录继续保留独立最小复现；下文“没有修改正式流程”描述当时探针验证的边界，不表示后续正式接入仍未完成。

独立验证项目，不接入正式应用，也不修改正式播放器适配器、弹幕调度或用户设置页。覆盖层只是带文字的透明占位窗，工具栏是独立测试按钮；不代表真实弹幕渲染、进度保存或正式工作流已完成改造。

运行前须无已运行的 PotPlayer。探针会拒绝已有实例；测试中的“对照实例”也是本探针创建的。使用随 PotPlayer 安装的 `CmdLine64.txt` 作为 `/new`／`/current` 参数依据，不更改多实例偏好。会正常播放本地生成的无声 60 秒 AVI（80×48、5fps），仅向持有进程句柄、核实路径且 HWND 属于该 PID 的测试实例发送正常关闭请求及下述 IPC 消息，不使用强制终止或按名称批量关闭。

```powershell
./scripts/probe-potplayer-lifecycle.ps1 -BuildOnly
./scripts/probe-potplayer-lifecycle.ps1
./scripts/probe-potplayer-lifecycle.ps1 -Ipc
# 无唯一注册路径时，使用 -Player 显式传入 PotPlayerMini64.exe 路径。
```

脚本从自身推导根目录，使用项目锁定 Qt／CMake／Ninja 和 `.local/toolchain.local.json` 的显式系统工具链配置。构建输出位于 `out/build/potplayer-lifecycle-probe/`，独立配置会拒绝移位或更换工具链后复用缓存。生成样本、原始事件和环境记录位于 `out/validation/potplayer-lifecycle-<时间>/`。无依赖准备或自动下载。

流程：

1. `/new` 创建对照实例，记录保留的进程句柄、PID、创建时间和真实路径。
2. `/current` 打开另一测试文件，记录启动进程退出、对照实例接管文件的实际现象。
3. `/new` 创建受控实例并显示探针层；点击 **探针工具栏** 的“结束播放”，等待受控实例退出。应仅退出受控实例，对照实例仍存活。
4. 第二轮再次 `/new` 创建受控实例。依据 `result.json` 的受控 PID 定位对应 PotPlayer 窗口，点击其关闭按钮；Windows 进程句柄通知应使探针层和工具栏隐藏，对照实例仍存活。相同标题的两个窗口不能仅凭标题识别。
5. 探针正常关闭自己的对照实例并退出。总超时 120 秒；失败清理也只请求自己的实例正常关闭，不强杀。

`result.json` 最终 `stage=passed`、`completedDirections=2` 为本次探针预期结果。事件中的时间只供诊断，不是性能基准。SMTC 仅做只读快照，不控制其他会话。

## 2026-10-17 本机结果

Release 独立构建通过，PotPlayer64.dll 文件版本 **1.7.22851.0**。实际完成两方向交互并退出 0，所有测试进程已清理；原始记录为 `out/validation/potplayer-lifecycle-20261017-203219/result.json`。

- `/current` 的启动 PID 34484 退出，视频由对照 PID 34036 接管，验证了不能把启动 PID 一律当作播放实例。
- `/new` 的受控 PID 34164 与对照 PID 34036 同时存在；点击探针结束按钮后，受控实例正常退出、对照存活、探针层隐藏。
- 第二轮受控 PID 15112 被实际点击关闭按钮后，进程句柄退出通知触发探针层／工具栏隐藏，对照仍存活；随后探针正常清理对照。
- 同时存在的两个 SMTC 会话均使用 `PotPlayerMini64.exe`，且本次故意存在同名内容，标题也相同。现有按应用 ID 选会话不能唯一绑定本次新实例；进程生命周期可绑定，不代表当前 SMTC 同步链已解决多实例选择。后续产品接入必须另行解决会话对应关系，或显式限制到唯一实例，不能直接把 `/current` 改成 `/new` 后宣布完成。

未验证崩溃／强制结束、关闭驻留托盘、播放器拒绝退出、权限不同、混合 DPI／高对比度、真实轨道资源回收、进度保存或完整应用退出联动；没有修改正式应用的停止语义。

## PID → HWND 消息验证

`-Ipc` 为自动实机验证模式，使用同一独立构建，不加载占位覆盖层。两个 `/new` 实例均播放同一个 `owned-B.avi`，故 SMTC 应用 ID、文件路径和标题都不足以唯一选择实例。通过各自保留的进程句柄、PID 和 `PotPlayer64` 主窗口绑定，直接向指定 HWND 查询和控制。每条消息前检查进程仍存活、HWND 有效且所属 PID 未变；`SendMessageTimeoutW` 设置 300ms 超时，不在正式 GUI 线程引入轮询。结果保存在 `out/validation/potplayer-ipc-<时间>/`。

消息编号参考 [PotPlayerControl 的常量](https://github.com/ld3l/PotPlayerControl/blob/b4483d52a16fe840aac6b84a94e27da9ec7a29ca/src/main/java/com/del/potplayercontrol/impl/JNAMessageConst.java)、[查询实现](https://github.com/ld3l/PotPlayerControl/blob/b4483d52a16fe840aac6b84a94e27da9ec7a29ca/src/main/java/com/del/potplayercontrol/impl/JNAPotPlayer.java)及[作者发布的 AHK 实现中的显式状态取值](https://www.cnblogs.com/hyaray/p/17050617.html)。它们是社区实现资料，不是 PotPlayer 官方稳定性承诺；探针未引入这些项目的源码或运行依赖。

| 消息 | wParam | lParam／结果 |
| --- | --- | --- |
| WM_USER | 0x5002 | 查询总时长，毫秒 |
| WM_USER | 0x5004 | 查询当前位置，毫秒 |
| WM_USER | 0x5005 | 设置目标位置，毫秒；随后读取确认 |
| WM_USER | 0x5006 | 查询状态，本次实测 1=暂停、2=播放 |
| WM_USER | 0x5007 | 1=暂停、2=播放；本次验证重复调用保持状态 |

本机 **1.7.22851.0** 的 Release 实机结果：`out/validation/potplayer-ipc-20261017-204312/result.json`，`stage=passed`，正常退出码 0。

- 受控 PID 35148、对照 PID 24884 均独立存活；总时长各为 60000ms。两条 SMTC 会话仍同 ID、同标题。
- 播放位置实际递增；暂停后 1200ms 的两次读取均为 3519ms。恢复后继续递增，重复显式播放／暂停保持预期状态。
- 暂停中跳转到 12500／40000／5000ms，实际回读为 12400／39800／4800ms，均在本样本一帧 200ms 内；不能据此声称任意格式支持毫秒精确定位。40000ms 阶段额外保持约 20 秒，回读仍为 39800ms。
- 对照被预置暂停在 10000ms（实际 9800ms），受控实例整个暂停／继续／前后跳转及退出过程中，对照始终保持暂停和 9800ms，未发生串控。已实际激活并目视核对两个窗口，受控显示 00:39、对照显示 00:09，均为 01:00 总时长及暂停图标；对照截图保存在该报告目录 `sentinel.jpg`。
- 受控实例经正常关闭退出后，旧绑定查询被进程存活检查拒绝，对照仍可查询；随后正常清理对照，无探针或 PotPlayer 进程遗留。没有模拟或宣称已覆盖实际 PID／HWND 复用。
- 首次尝试社区资料中的 WM_COMMAND 20000／20001，未得到预期播放状态，报告 `out/validation/potplayer-ipc-20261017-204232/result.json` 保留该失败；最终使用上表显式状态消息通过，不能按命令名称假定其幂等性。

结论：当前版本可由“保留进程身份＋PID → HWND＋窗口消息”同时提供生命周期、播放位置／时长和基础控制，从而绕开 SMTC 多实例同 ID 的选择歧义。正式接入应单独实现 PotPlayer 媒体适配器，采用有界异步查询、状态回读确认和失效解绑；不能把独立探针直接当成正式弹幕同步验收。

尚未验证其他 PotPlayer 版本／皮肤、权限不同、窗口销毁重建、消息超时／挂起恢复、变速查询、文件切换／播放列表、EOF、真实格式关键帧定位、真实弹幕同步与资源回收；现有正式应用、用户设置和播放方式未修改。
