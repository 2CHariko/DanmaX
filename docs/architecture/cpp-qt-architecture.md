# C++ / Qt 核心架构与项目内开发边界

本文档阐述 DanmaX 0.2+ 版本的 C++20 与 Qt 6 Quick 分层架构、数据流、线程与渲染规则，以及项目内工具链与构建目录的隔离边界。

> [!NOTE]
> 本文档反映当前 **0.2 实现基线**。各子系统的最新实机验收状态与已知限制以 [当前交付状态](../status/current-status.md) 为准。

## 1. 核心架构决策

| 维度 | 选型 | 理由与约束 |
|---|---|---|
| 语言标准 | C++20 | 利用标准库模型、RAII 与明确所有权，兼容当前 MSVC 工具链 |
| UI 与控件风格 | Qt 6 Quick / QML / 官方 FluentWinUI3 | 控制面板使用官方 FluentWinUI3 与 Fusion 回退；弹幕内容独立渲染，不套用面板卡片样式 |
| 窗口与标题栏 | 静态编译 QWindowKit 1.5.0 | 实现 Windows 11 沉浸式标题栏、贴靠布局（Snap Layouts）与 Mica 背景；锁定于 `toolchain/qwindowkit.lock.json` |
| 渲染管线 | Qt Quick 公开场景图（D3D11） | 复用文本布局与 GPU 后端；不默认引入私有 API 或自定义 RHI/Direct3D 后端，关闭 OpenGL/Vulkan |
| 平台目标 | Windows x64 优先 | 深入验证受控实例、窗口穿透、焦点与多显示器 DPI；领域核心保持纯标准库可移植性 |
| 构建与测试 | CMake + Ninja + MSVC / CTest + Qt Test | 单一构建体系，不引入 vcpkg、Conan、Boost、spdlog、独立 XML/JSON 库或第三方测试框架 |
| 依赖管理 | 版本清单 + 显式 PowerShell 准备脚本 | 配置与构建阶段严禁自动联网；新依赖须说明现有能力无法满足的原因与部署影响 |
| 分发形态 | 静态 Qt 单 EXE（默认）+ 动态部署目录 | 静态构建使用 `/MT`，并附带完整开源合规材料 |

## 2. 目录与构建目标规划

```text
项目根目录/
├── AGENTS.md                       # 代理开发红线与总索引指针
├── CMakeLists.txt                  # 根构建入口
├── CMakePresets.json               # 共享预设（禁止提交本机绝对路径）
├── cmake/                          # Qt、编译器校验与部署辅助模块
├── scripts/                        # PowerShell 7 自动化构建、诊断、打包与验证脚本
├── toolchain/                      # dependencies/qt-static/qwindowkit/anitomy/libvlc 锁清单
├── src/
│   ├── core/                       # 纯 C++20 领域核心（不依赖 Qt/QML/Win32/WinRT）
│   ├── application/                # 会话编排、设置管理与状态输出
│   ├── infrastructure/             # XML 解析、INI 配置、日志、Anitomy 适配器与在线缓存
│   ├── platform/windows/           # 受控播放器适配、层级协调、窗口穿透与原生对话框
│   ├── renderer/                   # QQuickItem、场景图节点与纹理显存预算管理
│   └── app/                        # main 入口、QML 注册与对象装配
├── qml/                            # 控制面板页面、公共 Fluent 组件与主题语义层
├── resources/fluent/               # 微软官方 Fluent System Icons SVG 原稿与深色改色稿
├── tests/                          # CTest、Qt Test 与脚本回归测试
├── .tools/                         # 未入库：项目内 Qt、CMake、Ninja
├── .deps/                          # 未入库：QWindowKit、Anitomy、libVLC 等离线依赖
├── .cache/                         # 未入库：downloads、tmp、qml 缓存
├── .local/                         # 未入库：toolchain.local.json 等本机配置与日志
└── out/                            # 未入库：build、run、packages、validation
```

构建目标分为 `danmaku_core`（纯 C++20 静态库）、`danmaku_runtime`（Qt/平台服务）与 `danmaku_app`（QML 与场景图渲染）。

## 3. 分层模块与数据流

```text
QML UI  <──(属性绑定 / 命令)──>  Application Service (NowPlayingModel / Sessions)
                                        │                      ▲
                                  XML / Config             媒体快照 / 帧时间
                                        ▼                      │
                                 Infrastructure         Windows Platform Adapter
                                        │                      │
                                        └───> danmaku_core ────┘
                                                   │
                                           活跃弹幕快照移交
                                                   ▼
                                        Renderer (Qt Quick Scene Graph)
```

### 3.1 领域核心（`src/core`）

- **纯标准库边界**：仅依赖 C++20 标准库，禁止包含 Qt、QML、Win32 或 WinRT 头文件。
- **核心组件**：
  - `DanmakuItem`：媒体时间（`double` 秒）、弹幕模式、RGB/ARGB 整数、文本及源索引。
  - `TimelineEngine`：排序索引、播放游标、跳转与媒体切换语义；按时间区间产出批次，不逐条跨线程发送信号。
  - `TrackAllocator`：根据渲染层回传的真实文本宽高、速度与轨道边界进行物理防重叠分配，不依赖固定 `0.8` 系数提前释放轨道；滚屏允许末行按行高向上取整并局部裁剪，固定弹幕保持完整锚定。
  - `ActiveSet`：预留容器存储、活动状态、剩余寿命、回收与丢弃统计。

### 3.2 应用层与基础设施（`src/application` & `src/infrastructure`）

- **播放中心与互斥任务**：`NowPlayingModel` 按 `MediaSession` 与 `DanmakuSession` 的当前所有者投影状态与控制能力；`PlaybackCoordinator` 负责双任务互斥交接，`AppController` 持有唯一弹幕引擎、帧驱动与覆盖层。详见 [双任务工作流](dual-workflows.md) 与 [播放中心](now-playing.md)。
- **视频打开与后端契约**：`VideoOpenService` 通过 `IExternalPlayerAdapter` 与 `ExternalPlayerRegistry` 接入 PotPlayer x64、VLC 3.x x64（及开发版内置 libVLC）；`MediaPlaybackBackend` 统一采样、控制与生命周期能力。详见 [外部播放器预设与契约](../playback/external-players.md)。
- **定位事务**：视频定位与历史续播统一经由唯一 `SeekController` 编排，严格区分 `Rejected`、`Submitted` 与 `Unknown` 状态。详见 [公共定位事务](seek-transactions.md)。
- **标题解析与自动匹配**：`HybridMediaTitleParser` 结合规则与静态链接的 `Anitomy`（锁定于 `toolchain/anitomy.lock.json`，输入上限 1024 字符、候选上限 16 条）解析标题；`PlaybackRecovery` 在视频就绪后协调匹配期间暂停与结果恢复；`MediaAssociationStore` 原子维护 `media/media-associations.json`（上限 512 条、512 KiB）。
- **流式 XML 解析**：异步读取并将非法 C0 控制字符替换为空格（保留 TAB/LF/CR），支持 UTF-16/32 字节序；模式 1/2/3 映射滚动、4/5 映射固定，缺失颜色回退白色；100 万条总上限包含被过滤条目，失败或取消不提交部分结果。
- **配置与日志**：`settings.ini`（UTF-8 带中文注释）通过 `QSaveFile` 原子保存并合并 200 ms 高频输入；日志系统提供有界 UI 队列（最多 1000 条）与 2 MiB 单文件轮转。

### 3.3 Windows 平台适配（`src/platform/windows`）

- **受控实例识别**：通过进程句柄、PID、创建时间、程序路径与所属主窗口绑定校验受控播放器，禁止按进程名、窗口标题、枚举次序或 AUMID 猜测或接管已有普通播放器窗口。
- **层级协调与置顶维护**：`WindowLayerController` 在 GUI 线程集中维护“面板所属对话框 > 工具栏所属浮层 > 工具栏 > 主面板 > 弹幕覆盖层”的次序；置顶仅保留 `0`/`1`，通过进程外 WinEvent（16 ms 合并）与 100 ms 周期兜底检查异常并整组恢复 `HWND_TOPMOST`，不激活窗口。
- **原生文件对话框**：在 Windows 上使用系统原生文件对话框并绑定所属窗口与任务代次，任务失效时立即作废请求并关闭对话框。
- **RAII 生命周期**：线程、窗口钩子与句柄遵循 RAII，停止时合作取消并等待退出，严禁强制 `terminate` 工作线程。

## 4. 渲染、时间轴与资源规则

1. **单一时钟帧驱动**：可见播放由 Qt Quick `afterAnimating` 按实际单调时间差（`delta`）驱动；隐藏、未暴露或暂停时切换至低频 33 ms 维护节奏。
2. **显式语义分离**：
   - 媒体时间决定投放，动画时间决定滚动位移与寿命（随已知速率缩放，未知速率按 1 倍）。
   - **暂停**冻结弹幕位置、寿命与轨道占用；恢复时不补偿墙钟耗时。
   - **跳转**或**媒体切换**同时重置投放游标、活动对象与轨道索引，清空当前在屏弹幕。
   - **设置变更**通过 `reconfigure()` 保留投放游标与活动对象，仅局部重排轨道并淘汰超限较新对象（计入 `retiredBySettings`）。
3. **自适应文字纹理预算**：默认将每条弹幕及描边按实际 DPR 栅格化并通过 `QSGSimpleTextureNode`（`TextureCanUseAtlas`）渲染。自适应模式下从最多 64 MiB 起步，保留 25% 缓冲按两倍档位扩缩至上限（默认 512 MiB，可配 32–1024 MiB）；单帧新增估算上限 4 MiB、准备耗时上限 2 ms。超尺寸、超预算或未就绪条目平滑降级为 `QSGTextNode` 矢量节点。
4. **生命周期清理**：在 `sceneGraphAboutToStop` 的直接连接中释放场景图纹理资源；隐藏期间不补发新弹幕（计入 `suppressedWhileHidden`），恢复可见时保持存活对象 ID 与位置。

## 5. 项目内工具链与构建边界

- **严格控制范围**：Qt SDK、CMake、Ninja、下载缓存、QML 磁盘缓存（`.cache/qml/<Qt版本>/`）、临时目录（`.cache/tmp/`）与运行目录（`out/run/<预设>/`）全部受脚本子进程环境变量隔离在项目根目录下；禁止依赖全局包缓存或修改系统永久 `PATH`。
- **系统安装边界**：MSVC 编译器、Windows SDK 与显卡驱动缓存属于系统级组件，无法完全搬迁至项目目录。本机系统工具路径必须在 `.local/toolchain.local.json` 中显式声明（由 `scripts/doctor.ps1` 报告真实路径），禁止静默回退或复制既有 MSVC 安装目录。
- **独立准备与禁止内联构建**：依赖下载与哈希校验仅通过 `bootstrap.ps1`、`prepare-*.ps1` 显式执行；`build.ps1`、`package.ps1` 不得自动联网。禁止在源码目录内直接构建，Debug 与 Release 使用独立预设目录，移动项目后必须重新配置。

## 6. 内置 libVLC 开发接入边界

仅 `windows-libvlc-debug` 与 `windows-libvlc-release` 预设通过 `DANMAKU_ENABLE_LIBVLC` 启用内置播放页（依赖共享于 `toolchain/libvlc.lock.json`）。`VideoViewportController` 在 GUI 线程管理自有原生视频子窗并通过 `SetWindowSubclass` 同步拥有权下的透明弹幕覆盖层；`LibVlcBackend` 在单一后台线程串行调用 libVLC C API。默认发布打包脚本拒绝内置构建，完整细节见 [内置播放页接入](../playback/libvlc-integration.md) 与 [覆盖层原生跟随设计](../playback/overlay-follow.md)。

## 相关内容

- [双任务工作流](dual-workflows.md)
- [播放中心](now-playing.md)
- [公共定位事务](seek-transactions.md)
- [外部播放器预设与契约](../playback/external-players.md)
- [FluentWinUI3 界面设计系统](../ui/ui-design-system.md)
- [Windows 便携分发](../release/packaging.md)
