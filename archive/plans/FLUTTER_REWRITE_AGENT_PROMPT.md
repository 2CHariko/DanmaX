# Flutter 重构 Agent 一次完成提示词

下面的内容可以直接复制给具备代码执行能力的 Agent。目标是让 Agent 在一次任务中完成从仓库检查、项目初始化、核心实现、平台接入、测试到构建验证的完整闭环。

## 可复制提示词

```text
你是一个资深 Flutter Desktop、Dart、Windows 原生集成和高性能 2D 渲染工程师。请在当前代码仓库中直接完成“本地弹幕播放器从 Python/Qt6 迁移到 Flutter Desktop”的完整实现。不要只给计划，不要只给代码片段，不要停留在分析阶段。你必须读取现有代码、创建或修改文件、运行测试和构建，并在最后汇报实际完成情况。

一、仓库上下文

当前工作区：D:\1Temp\Code\LocalPlayerDanmakuOverlay

现有 Python/Qt6 代码仍然是可工作的旧版本，必须保留，不得删除、覆盖或使用破坏性 Git 命令回滚。重点参考以下文件：

- danmaku_models.py：静态弹幕和活动弹幕模型。
- danmaku_parser.py：Bilibili 风格 XML 解析。
- danmaku_controller.py：播放同步、时间轴推进和媒体会话编排。
- danmaku_renderer.py：透明覆盖层、对象池、轨道分配和绘制。
- control_panel.py：主界面、设置、日志和播放器发现。
- debug_overlay.py：FPS、CPU、内存和弹幕统计。
- config_loader.py、config.ini：配置字段和旧配置格式。
- monitors/base_monitor.py：跨平台媒体监控接口。
- monitors/windows_monitor.py：Windows SMTC 实现。
- testDanmaku/：XML 测试样本。
- test/：旧版本测试和覆盖层测试。

仓库中可能存在空的或目录名异常的 Flutter 目录。先检查所有目录。如果没有完整可运行的 Flutter 项目，创建规范目录：

    flutter_app/

不要因为已有异常目录就把新代码继续放入包含盘符或特殊字符的目录。除非确认它是完整且正确的 Flutter 项目，否则不要依赖它。

二、最终目标

实现一个 Windows 优先、核心逻辑跨平台的 Flutter Desktop 本地弹幕播放器，至少具备：

1. 选择和加载本地 Bilibili XML 弹幕文件。
2. 解析滚动、顶部固定、底部固定三类弹幕。
3. 使用透明、无边框、全屏、可置顶、鼠标穿透的弹幕覆盖层。
4. 使用 CustomPainter 或等价的单画布高效渲染，不为每条弹幕创建独立 Widget 或独立动画控制器。
5. 支持对象池、轨道分配、文本布局缓存和批量状态更新。
6. 通过 Windows SMTC 获取当前播放器会话、标题、播放状态、当前位置、总时长和来源 AUMID。
7. 支持媒体会话发现、目标 AUMID 选择、播放/暂停/跳转/倒退/媒体切换同步。
8. 支持字体、字号、描边、透明度、滚动速度、固定弹幕时长、最大活动弹幕数、最大轨道数、是否允许重叠、置顶策略、调试开关和日志设置。
9. 支持配置保存、默认值、旧 config.ini 导入、配置版本迁移和热应用。
10. 支持实时日志和调试指标。
11. 具备单元测试、渲染/集成测试、压力测试入口和 Windows Release 构建能力。

三、必须遵守的工程规则

1. 先读代码再编辑。先运行目录检查、Git 状态检查，并确认现有 Python 改动；不要覆盖用户已有修改。
2. 不得删除旧 Python/Qt6 实现。Flutter 是新实现，旧实现保留为回滚版本。
3. 不要使用 git reset --hard、git checkout --、递归删除或其他破坏性命令。
4. 不要只生成 TODO、占位函数、空页面或“未来再接入”的核心功能。
5. 如果某个第三方 Flutter 包无法满足能力，优先选择稳定方案；必要时实现最小 Windows 原生插件或 FFI 接口。不得在没有验证的情况下宣称平台功能已经完成。
6. Flutter Desktop 本身负责桌面打包。不要默认引入 Tauri；只有确认存在明确的 Rust/WebView/系统集成需求时才使用，并在最终报告中解释原因。
7. 不要把 Flutter 的理论性能当作实际性能。必须用压力测试记录真实 FPS、帧耗时、CPU、内存和活动弹幕数。
8. 核心模型不得依赖 PyQt、Windows API 或 Flutter Widget。
9. XML 解析、排序和大批量数据处理必须放入 Isolate 或等价后台任务，不能阻塞 UI 线程。
10. 不要在每一帧创建大量短生命周期对象；避免每帧 TextPainter、Paragraph、Widget、Timer 或 AnimationController 的重复创建。
11. 使用强类型模型、不可变配置快照和明确的状态机。不要把所有逻辑塞进一个 StatefulWidget。
12. 处理异常时保留可诊断信息。单条坏弹幕不能导致整个文件加载失败；平台能力不可用时必须在 UI 和日志中明确显示。
13. UI 需要适合桌面工具反复使用：信息密度适中、控件分组清晰、状态明确，不能做成营销落地页。
14. 代码、文件名和配置键默认使用 ASCII；用户可见的中文可以保留。
15. 代码注释只解释非显然的算法、平台限制或生命周期处理，不添加空泛注释。

四、建议的实施顺序

按以下顺序执行，但遇到实际仓库结构差异时可以做等价调整。每完成一个阶段都要运行对应检查，不要把所有错误留到最后。

阶段 0：环境和基线

- 检查 Flutter、Dart、Windows 编译工具链是否可用。
- 检查现有 Git 状态和 Flutter 目录。
- 读取现有 Python 模块和 config.ini，整理真实字段，不要臆造旧功能。
- 尝试运行现有测试，记录基线结果。
- 建立 Flutter 项目或修复已有 Flutter 项目，使 `flutter analyze` 和最小测试可运行。
- 记录当前设备或测试机的分辨率、DPI、刷新率。

阶段 1：Flutter 项目和目录结构

在 `flutter_app/` 中建立清晰结构，至少包括：

    lib/
      core/models/
      core/parser/
      core/playback/
      core/config/
      core/logging/
      renderer/
      platform/media/
      platform/overlay/
      state/
      ui/shell/
      ui/player/
      ui/settings/
      ui/discovery/
      ui/logs/
      debug/
    test/
    integration_test/
    assets/
    windows/

配置 Riverpod 或一个统一的状态管理方案。不要同时混用 Hooks、Riverpod、Bloc 三套系统。选择一种并在代码中保持一致。

阶段 2：领域模型和配置迁移

实现至少以下模型：

- `DanmakuItem`：startTime、mode、text、colorArgb、原始索引或行号。
- `DanmakuMode`：scroll、bottom、top。
- `ActiveDanmaku`：轨道、位置、速度、结束时间、布局缓存引用和活动状态。
- `MediaSession`：应用 ID、标题、艺术家、播放状态、位置、时长。
- `AppSettings`：覆盖旧 config.ini 的全部配置项。

将颜色和时间等核心字段保存为平台无关的基础类型，不要在模型中直接使用 Flutter `Color` 或 Windows 类型。

实现配置仓库：

- 默认值完整。
- 强类型读取和范围校验。
- 新配置带 schema version。
- 新配置不存在时导入旧 `config.ini`。
- 迁移前保留旧文件或备份。
- 读取失败时回退到默认值并记录原因。
- 配置保存使用原子写入或等价的防损坏方案。

至少覆盖以下旧字段：

- Display.font_name
- Display.font_size
- Display.stroke_width
- Display.max_tracks
- Display.opacity
- Display.line_spacing_ratio
- Danmaku.scroll_speed
- Danmaku.fixed_duration_ms
- Danmaku.max_danmaku_count
- Danmaku.allow_overlap
- Sync.target_aumid
- Debug.enabled
- Debug.info_position
- OnTopStrategy.method
- Logging.level
- Logging.log_to_file
- DEFAULT.LastDanmakuPath

阶段 3：XML 解析和时间轴

实现 Bilibili 风格 XML 解析器：

- 读取 `<d p="...">文本</d>`。
- 至少解析 p 的前四项：开始时间、模式、用户/保留字段、十进制 RGB 颜色。
- 只接受模式 1、4、5；未知模式计入过滤统计。
- 空文本、非法数字、非法颜色和损坏记录单独记录并跳过。
- 解析结束后按开始时间排序。
- 建立独立的开始时间数组，用于二分查找。
- 长文本、中文、英文、emoji 和 XML 转义内容必须保留正确。
- 使用 Isolate 处理文件读取、解析和排序。
- 大文件处理要输出进度或阶段状态，避免 UI 看起来卡死。

实现 `TimelineEngine`：

- 正常播放使用单调递增游标。
- 播放时间前进时批量产出到期弹幕。
- 播放跳转、倒退或媒体切换时使用二分查找重置游标。
- 播放暂停时冻结弹幕动画状态。
- 处理播放器时间抖动、会话丢失和 duration 变化。
- 提供清空活动弹幕和重新加载文件的接口。
- 明确媒体时间与墙钟时间：媒体时间决定弹幕出现，墙钟 delta 决定动画位移。

阶段 4：高性能渲染闭环

实现一个最小但真实可运行的覆盖层和压力测试页：

- 透明、无边框、全屏窗口。
- 可切换置顶和鼠标穿透。
- 单个帧驱动器更新全部活动弹幕。
- `CustomPainter` 或等价的画布渲染。
- 滚动弹幕从右侧进入，从左侧完全离开后回收。
- 顶部和底部弹幕按固定时长消失。
- 对象池限制活动对象数量。
- 轨道分配支持允许重叠和禁止重叠两种模式。
- 字体布局缓存使用文本、字体、字号、描边、DPR 等作为缓存键。
- 字体、字号、描边、DPR 或窗口尺寸变化时正确失效缓存。
- 不为每条弹幕创建 Widget、Timer 或动画控制器。
- 不在每帧创建大量列表和临时对象；必要时使用可复用缓冲区。
- 绘制失败时不能让整个渲染循环崩溃。

文字绘制 API 以当前 Flutter SDK 实际可用接口为准，优先使用可缓存的 Paragraph/TextPainter 或等价的布局结果。不要把不存在或未经当前 SDK 验证的 `Canvas.drawText` API 写进代码。

先完成正确性，再进行缓存和对象池优化。不要过早添加复杂 shader、实例化渲染或原生位图管线；只有压力测试证明 Canvas 路径不足时才引入。

阶段 5：Windows 媒体和窗口平台适配

定义平台无关接口，例如：

    abstract interface class MediaSessionAdapter {
      Future<List<MediaSession>> listSessions();
      Stream<MediaSessionSnapshot?> watchCurrentSession();
      String? getForegroundApplicationId();
    }

    abstract interface class OverlayWindowAdapter {
      Future<void> configure(OverlayWindowOptions options);
      Future<void> setVisible(bool visible);
      Future<void> setAlwaysOnTop(bool enabled);
      Future<void> setPointerPassthrough(bool enabled);
    }

Windows 首发实现必须尽量复现 Python 版本行为：

- 使用 Windows SMTC 获取活动媒体会话。
- 列出应用 ID/AUMID 和媒体标题。
- 读取播放状态、位置、时长、标题和来源应用。
- 支持目标播放器发现和选择。
- 支持前台窗口应用识别或合理的降级方案。
- 支持透明覆盖层、无边框、置顶和鼠标穿透。
- 播放器关闭、会话失效、权限失败和 API 不可用时输出明确错误。

先做独立平台探针验证 SMTC 和窗口行为，再接入 Dart 状态层。若需要原生代码，只暴露最小、稳定、可测试的接口；平台代码不得向核心时间轴泄漏 Windows 类型。

macOS/Linux 只需完成接口和明确的不可用状态，除非本地环境已经具备可验证的媒体 API。不要把未测试的平台标记为完成。

阶段 6：控制面板和状态管理

实现以下可操作页面，不要只做静态展示：

1. 主界面
   - 弹幕文件路径。
   - 文件选择按钮。
   - 加载进度和错误状态。
   - 当前目标播放器摘要。
   - 开始和停止按钮。
   - 运行中/已暂停/未连接状态。

2. 设置页
   - 显示设置：字体、字号、描边、透明度、行间距。
   - 弹幕设置：速度、固定时长、最大活动数、轨道数、允许重叠。
   - 同步设置：目标 AUMID、会话发现。
   - 窗口设置：置顶策略、鼠标穿透或覆盖层行为。
   - 调试和日志设置。
   - 应用设置、恢复默认、保存状态和错误反馈。

3. 会话发现对话框
   - 展示应用 ID、标题、播放状态和进度。
   - 正确处理空结果、失效会话和取消操作。

4. 日志页
   - 实时追加。
   - 级别过滤。
   - 清空和导出。

5. 调试覆盖层
   - FPS。
   - P95/P99 或最近窗口帧耗时。
   - CPU 和内存。
   - 总弹幕、活动弹幕、对象池空闲数。
   - 缓存命中率和丢弃弹幕数量。
   - 当前媒体标题、播放时间和同步状态。

设置应用必须区分：

- 可立即应用：透明度、速度、轨道策略、调试开关。
- 需要重建文字缓存：字体、字号、描边、DPR。
- 需要重新配置窗口：显示器、置顶、点击穿透。
- 需要重新加载文件：弹幕路径。

不要把所有设置变更都实现成盲目停止后延时重启。

阶段 7：测试、性能和发布

至少建立以下测试：

单元测试：

- XML 正常解析和排序。
- 颜色转换、模式过滤和坏记录跳过。
- 空文件、损坏 XML、长文本和特殊字符。
- 时间轴游标、二分查找、跳转和倒退。
- 轨道分配、固定弹幕释放和对象池回收。
- 配置默认值、范围校验和旧 INI 迁移。

渲染/组件测试：

- 滚动、顶部、底部弹幕生命周期。
- 透明度、颜色、描边和字体变化。
- 高 DPI 和窗口尺寸变化。
- 空活动列表和对象池满载。

集成测试：

- 选文件、加载、开始、停止、重新加载。
- 会话发现和 AUMID 选择。
- 播放、暂停、跳转、倒退和媒体切换。
- 关闭控制面板时覆盖层和后台任务都被清理。
- 保存配置后重启恢复。

压力测试：

- 500、1000、2000 条活动弹幕。
- 10 万条和更大 XML 文件。
- 不同字体、字号、描边和窗口分辨率。
- 允许重叠/禁止重叠。
- 开启/关闭调试层。

记录平均 FPS、P95/P99 帧耗时、CPU、内存、对象分配趋势、缓存命中率、丢弃弹幕数量和同步误差。首版最低验收目标：1080p 下 500 条活动弹幕稳定 60 FPS；2000 条场景必须给出真实测试结果，不能用理论数字代替。

运行并修复至少以下命令：

    flutter pub get
    dart format --output=none --set-exit-if-changed .
    flutter analyze
    flutter test
    flutter build windows --release

如果环境不支持某个命令，继续完成其他可运行验证，并在最终报告中明确说明原因和未验证范围。不要把命令失败隐藏成“完成”。

五、推荐实现细节

1. 使用不可变 `DanmakuItem`，活动弹幕作为可复用的内部运行时对象。
2. 颜色以 ARGB/RGB 整数存储，UI 层再转换为 Flutter `Color`。
3. 用开始时间数组配合 `lowerBound`/二分查找，避免每次跳转线性扫描。
4. 解析结果和新增弹幕按批次传递，避免 Isolate 逐条发送消息。
5. 通过单一帧驱动器更新全部活动弹幕。
6. 用最近帧时间计算 delta，不要固定假设每帧恰好是 1/60 秒。
7. 处理窗口最小化、暂停、失去目标会话和覆盖层不可见状态，避免无意义地持续高频绘制。
8. 对文本布局缓存设置容量上限和淘汰策略。
9. 对象池满时使用明确策略并记录丢弃原因。
10. 媒体轮询/订阅线程和 UI 状态更新要有取消和关闭生命周期。
11. 所有异步任务、StreamSubscription、Timer、Ticker 和原生资源都必须在 dispose/stop 时清理。
12. 日志不要在每一帧写磁盘；帧指标应按时间窗口采样。
13. 不要在界面中使用巨大的营销式 Hero、装饰性渐变或无意义卡片堆叠。应用是桌面工具，优先清晰、紧凑、可扫描。

六、完成定义

只有满足以下条件才可以认为任务完成：

- Flutter 项目可启动，核心页面不是占位页。
- 现有 XML 样本可加载并显示。
- 滚动、顶部、底部三种弹幕都能真实运行。
- 至少 Windows 媒体同步或明确可用的测试替代源已接入；如果 Windows 原生能力受阻，必须提供原生探针、接口实现状态和具体剩余阻塞点。
- 控制面板、设置、会话发现、日志和调试功能可操作。
- 配置迁移和保存恢复可用。
- 单元测试、Flutter analyze 和可用的构建命令已运行。
- 压力测试有真实结果。
- 旧 Python/Qt6 版本仍然存在并可回滚。
- 没有未说明的核心 TODO、假实现、吞异常或静默失败。

七、遇到问题时的处理规则

- 如果某个库版本不兼容，先检查当前 SDK 和官方文档，选择兼容版本或替代实现。
- 如果窗口插件无法提供所需功能，先实现平台适配接口和最小原生实现，不要把 Windows API 调用散落在 UI 中。
- 如果性能不足，先用指标确认瓶颈，再优化最热路径；不要凭感觉重写整个渲染器。
- 如果时间不足，优先保证：XML 解析、时间轴、渲染闭环、Windows 同步、配置迁移和测试；不要优先做装饰性 UI。
- 如果无法在本机验证 macOS/Linux，不要伪造测试结果。
- 不要因为发现旧代码有问题就顺手大范围重构旧 Python 项目；只记录兼容性影响，保持变更边界。
- 除非某项信息确实无法从仓库、SDK、工具输出或官方文档确认，否则不要向用户反复提问；做出合理工程决策并记录假设。

八、最终汇报格式

完成后用简洁但具体的方式汇报：

1. 已实现的功能。
2. 关键文件和目录。
3. 运行过的命令及结果。
4. 性能测试设备、场景和数据。
5. Windows 原生能力的实际状态。
6. 未完成事项、已知风险和原因。
7. 启动或构建命令。

最终报告必须区分“已验证”“已实现但未验证”和“尚未实现”，不得把计划内容写成完成内容。
```

## 使用说明

将上面的代码块完整复制给 Agent，并将当前仓库设置为工作目录。Agent 应当拥有文件读写、命令执行和构建权限；如果没有 Windows Flutter 编译环境，它仍然应该完成核心 Dart 代码、测试和可运行的替代测试入口，并明确列出无法验证的原生部分。

该提示词采用 Windows 优先的交付顺序，但刻意要求核心逻辑保持平台无关。这样可以先完成可验证的 XML、时间轴和渲染闭环，再把 SMTC、窗口置顶和鼠标穿透等高风险能力隔离在平台适配层中。
