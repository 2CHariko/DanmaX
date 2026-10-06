# 本地弹幕播放器 Flutter 重构计划

> 文档状态：规划稿
>
> 更新时间：2026-08-23
>
> 目标：在保留现有功能的基础上，重写为以 Flutter Desktop 为主、平台适配层独立的高性能本地弹幕播放器。

## 1. 项目目标

### 1.1 总目标

从当前 Qt6/Python 架构迁移到 Dart/Flutter Desktop，形成以下能力：

- 使用本地 XML 弹幕文件进行播放。
- 通过系统媒体会话同步 PotPlayer、MPC-HC 等本地播放器。
- 使用透明、无边框、鼠标穿透、可置顶的弹幕覆盖层。
- 支持滚动、顶部固定、底部固定三种弹幕模式。
- 支持字体、颜色、描边、透明度、速度、轨道数量、对象池大小等设置。
- 支持 AUMID/媒体会话发现、配置持久化、热应用、日志和调试信息。
- 为 Windows、macOS、Linux 保留统一的核心接口，平台能力通过适配器实现。

### 1.2 性能目标

性能目标必须以固定测试场景验证，不能仅以 Flutter 或 Canvas 的理论上限作为承诺。

| 指标 | 首版验收目标 | 压力目标 |
|---|---:|---:|
| 帧率 | 1080p 窗口稳定 60 FPS | 1440p/4K 仍保持 60 FPS |
| 单帧 CPU 时间 | P95 小于 12 ms | P95 小于 8 ms |
| 活动弹幕数 | 500 条无明显卡顿 | 2000 条可用 |
| 加载弹幕数 | 10 万条 XML 可完成加载 | 50 万条可完成索引 |
| 同步误差 | 正常播放小于 100 ms | 播放跳转后 300 ms 内恢复 |
| 内存 | 常规场景小于 250 MB | 10 万条数据小于 500 MB |
| 启动时间 | 冷启动到控制面板小于 3 秒 | 发行版小于 2 秒 |

### 1.3 非目标

首个 Flutter 版本不包含以下内容：

- 直接播放视频文件；播放器仍由外部本地播放器负责。
- 重新实现视频播放器内核。
- 在没有实测依据时宣称固定支持 5000 条以上同屏弹幕。
- 为了打包而叠加 Tauri。Flutter Desktop 本身已经能够生成 Windows、macOS、Linux 应用；只有在需要 WebView、现有 Rust 能力或特殊系统集成时，才重新评估 Tauri。

## 2. 当前仓库分析与迁移范围

当前根目录是 Python/Qt6 实现，核心职责如下：

| 当前模块 | Flutter 对应模块 | 迁移策略 |
|---|---|---|
| `danmaku_models.py` | `lib/core/models/` | 改为不可变数据模型；运行时活动对象与静态弹幕分离 |
| `danmaku_parser.py` | `lib/core/parser/` | XML 解析放入 Isolate；主线程只接收批处理结果 |
| `config_loader.py`、`config.ini` | `lib/core/config/` | 使用强类型配置对象和版本化存储，兼容旧配置字段 |
| `danmaku_controller.py` | `lib/core/playback/` | 拆分为媒体同步、弹幕时间轴和应用编排三个服务 |
| `danmaku_renderer.py` | `lib/renderer/` | `CustomPainter` 负责绘制；状态更新与绘制分离 |
| `debug_overlay.py` | `lib/debug/` | 调试数据由指标采集器提供，UI 只负责显示 |
| `control_panel.py` | `lib/ui/` | 改为页面路由和响应式状态管理 |
| `monitors/base_monitor.py` | `lib/platform/media/` | 定义跨平台媒体监控接口 |
| `monitors/windows_monitor.py` | `windows/` 或 `lib/platform/media/windows/` | 通过 FFI、平台插件或原生 Method Channel 接入 SMTC |
| `test/` | `test/`、`integration_test/` | 迁移为 Dart 单元测试、渲染测试和端到端测试 |

仓库中已有一个空的 Flutter 目录，但目录名包含异常字符。正式开发前应确定规范路径，例如 `flutter_app/`，避免后续脚本、CI 和打包工具处理路径时出现问题。

## 3. 推荐总体架构

```text
Flutter Desktop Application
├── UI 层
│   ├── MainShell
│   ├── PlayerPage
│   ├── SettingsPage
│   ├── LogsPage
│   └── DiscoveryDialog
├── 状态层
│   ├── AppState
│   ├── PlaybackState
│   ├── SettingsState
│   └── DiagnosticsState
├── 核心服务层
│   ├── DanmakuRepository
│   ├── DanmakuParser
│   ├── TimelineEngine
│   ├── PlaybackSyncEngine
│   ├── TrackAllocator
│   └── ConfigRepository
├── 渲染层
│   ├── DanmakuOverlay
│   ├── DanmakuPainter
│   ├── TextLayoutCache
│   └── DebugPainter
└── 平台层
    ├── WindowOverlayAdapter
    ├── MediaSessionAdapter
    ├── FilePickerAdapter
    └── SystemMetricsAdapter
```

### 3.1 核心设计原则

1. **时间轴与渲染解耦**：时间轴只回答“当前应该出现哪些弹幕”，渲染器只负责“如何绘制当前状态”。
2. **核心逻辑平台无关**：解析、排序、二分查找、轨道分配和播放状态机不能依赖 Flutter Widget 或 Windows API。
3. **主线程不做重活**：XML 解析、文本测量批处理、日志读取和会话发现不得阻塞 UI 帧。
4. **状态更新批量化**：同步引擎按固定节拍输出快照，覆盖层按帧消费快照，避免每条弹幕触发一次 Widget 重建。
5. **优先稳定的桌面渲染路径**：首版先使用 Flutter Desktop 的稳定 Canvas 绘制和缓存；着色器、实例化绘制等优化只有在基准测试证明必要时再加入。

## 4. 数据模型与时间轴设计

### 4.1 静态弹幕模型

建议定义以下强类型模型：

```dart
class DanmakuItem {
  final double startTime;
  final DanmakuMode mode;
  final String text;
  final int colorArgb;
}

enum DanmakuMode { scroll, bottom, top }
```

要求：

- `startTime` 统一使用秒，内部保留双精度。
- 颜色保存为整数，避免核心模型依赖 `Color`。
- 解析后按 `startTime` 排序，并建立独立的时间数组用于二分查找。
- 对空文本、非法时间、未知模式和非法颜色进行统计，不因单条坏数据中断整个文件加载。
- 保留原始索引或行号，便于日志和错误诊断。

### 4.2 活动弹幕模型

活动弹幕对象只保存渲染帧所需的运行时数据：

- 当前轨道和位置。
- 当前速度或固定弹幕的结束时间。
- 文本布局缓存的引用。
- 颜色、模式和可见状态。
- 对象池回收所需的重置状态。

不得在每一帧创建新的 `TextPainter`、字符串包装对象或 Widget。对象池应优先复用运行时对象，但必须保证回收时完全重置，避免上一条弹幕的缓存泄漏到下一条。

### 4.3 播放时间轴

`TimelineEngine` 负责：

- 根据当前播放时间计算弹幕插入范围。
- 使用单调递增游标进行正常播放。
- 对播放跳转、倒退、重新开始执行二分查找重定位。
- 处理暂停时的冻结策略。
- 处理播放器时间倒退、媒体切换和时长变化。
- 输出本次新增、需要移除和仍然活动的弹幕集合。

建议将“媒体时间”和“墙钟时间”分开：媒体时间决定弹幕应该出现，墙钟时间用于计算动画帧间隔。这样可以避免媒体会话轮询抖动直接变成弹幕位移抖动。

## 5. 渲染方案

### 5.1 首版渲染路径

- 使用透明顶层窗口承载 `CustomPaint`。
- 使用 `Canvas` 绘制已缓存的文本布局或位图。
- 每条弹幕只更新位置，不触发独立 Widget rebuild。
- 用 `Ticker` 或 `SchedulerBinding` 驱动渲染帧；不要为每条弹幕创建独立动画控制器。
- 根据窗口尺寸和字体设置建立文本测量缓存。
- 字体、描边宽度、字号或 DPR 变化时清理相关缓存。

Flutter 中的文字布局接口应以当前 SDK 的桌面实现为准，优先采用 `Paragraph`/`TextPainter` 缓存；不要把“`Canvas.drawText`”作为固定 API 设计目标。

### 5.2 轨道分配

保留现有三类逻辑，但将其改为独立的 `TrackAllocator`：

- 滚动弹幕：根据预计占用时间和速度判断轨道是否可用。
- 顶部弹幕：按固定显示时长占用轨道。
- 底部弹幕：按固定显示时长占用轨道。
- 允许重叠时使用更低成本的轮询或伪随机分配。
- 不允许重叠时，轨道分配失败应有明确策略：丢弃、延迟或降级到允许重叠，首版默认丢弃并计入统计。

### 5.3 缓存与对象池

缓存分为三层：

1. **解析缓存**：已排序的静态弹幕数据。
2. **文本布局缓存**：由文本、字体、字号、描边、DPR、颜色等参数组成键。
3. **活动对象池**：复用活动弹幕运行时对象。

缓存必须设置上限和淘汰策略。不能因为“缓存能提升性能”而无限保留历史文本，尤其要考虑弹幕文件来自长视频或直播录制文件的情况。

## 6. 播放器同步与平台适配

### 6.1 跨平台接口

```dart
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
```

### 6.2 Windows 首发实现

Windows 是首个可用平台，优先复现当前 Python 行为：

- 通过 SMTC 获取会话列表、标题、播放状态、当前位置、时长和来源 AUMID。
- 支持当前前台窗口识别，用于控制覆盖层显示和置顶状态。
- 支持无边框、透明、鼠标穿透和多显示器定位。
- 将 Win32/WinRT 调用限制在平台适配层，不让 `TimelineEngine` 直接依赖 FFI。

实现路径需要在 Phase 1 进行技术验证：

1. 评估已有 Flutter Windows 插件是否覆盖 SMTC 和窗口扩展能力。
2. 若插件不足，使用 C++/WinRT 原生插件或 FFI 暴露最小接口。
3. 编写独立的原生探针，先验证会话枚举和播放进度读取，再接入 Flutter。

### 6.3 macOS/Linux

- macOS：后续接入 Now Playing/媒体会话能力，覆盖层窗口行为单独验证。
- Linux：优先实现 MPRIS/D-Bus 监控；窗口透明、置顶和输入穿透按桌面环境分别测试。
- 不支持的平台不能静默假装同步成功，应在 UI 和日志中显示“媒体同步不可用”。

## 7. 状态管理与页面设计

建议使用 Riverpod 或项目团队已熟悉的单一状态管理方案，首版不要同时引入 Hooks、Riverpod、Bloc 三套状态系统。

### 7.1 页面

- **主界面**：弹幕文件选择、加载状态、目标播放器摘要、开始/停止控制。
- **设置页**：显示、弹幕、同步、置顶、调试、日志六个分组。
- **日志页**：实时日志、级别过滤、清空和导出。
- **会话发现对话框**：展示应用 ID、标题、状态和当前进度，选择后回填目标播放器。
- **调试覆盖层**：FPS、帧耗时、CPU、内存、总弹幕、活动弹幕、对象池空闲数和同步状态。

### 7.2 状态划分

- `AppController`：启动、停止、错误和生命周期。
- `SettingsController`：配置读取、校验、保存和应用。
- `DanmakuController`：文件加载、播放、暂停、清理。
- `PlaybackSyncController`：媒体会话轮询/订阅和目标会话匹配。
- `DiagnosticsController`：帧时间、内存、CPU、缓存命中率和丢弃统计。

设置应用不应通过“停止后延时重启”的方式作为唯一热重载机制。应区分：

- 可立即应用：透明度、速度、轨道分配策略、调试开关。
- 需要重建缓存：字体、字号、描边、DPR。
- 需要重建窗口：屏幕、窗口模式、点击穿透属性。
- 需要重新加载文件：弹幕源文件路径。

## 8. 配置与兼容性

### 8.1 配置字段迁移

兼容当前 `config.ini` 中的字段：

- `Display.font_name`
- `Display.font_size`
- `Display.stroke_width`
- `Display.max_tracks`
- `Display.opacity`
- `Display.line_spacing_ratio`
- `Danmaku.scroll_speed`
- `Danmaku.fixed_duration_ms`
- `Danmaku.max_danmaku_count`
- `Danmaku.allow_overlap`
- `Sync.target_aumid`
- `Debug.enabled`
- `Debug.info_position`
- `OnTopStrategy.method`
- `Logging.level`
- `Logging.log_to_file`
- `DEFAULT.LastDanmakuPath`

建议使用 JSON 或平台标准应用数据目录保存新配置，并保留一次性 INI 导入：

1. 新配置不存在时读取旧 `config.ini`。
2. 转换并校验字段。
3. 写入新配置格式。
4. 保留旧文件，不做破坏性删除。
5. 在配置中写入 schema 版本，后续支持迁移。

### 8.2 输入校验

所有配置项都要在 UI 和核心层双重校验，重点包括：

- 透明度范围 `0..1`。
- 字号、描边、轨道、对象池数量的上下限。
- 滚动速度不得为零或负数。
- 固定弹幕时长不得小于最小显示阈值。
- AUMID 为空时明确提示，而不是启动后静默等待。
- 文件路径不存在、无权限、XML 损坏时提供可读错误。

## 9. 分阶段执行计划

### Phase 0：基线与决策冻结，预计 0.5–1 天

**任务**

- 记录当前 Python 版本在 1080p、60 FPS、不同弹幕密度下的 CPU、内存和帧率。
- 确认 Flutter SDK、Dart SDK、Windows 编译工具链和最低支持系统版本。
- 确定 Flutter 项目规范路径，例如 `flutter_app/`。
- 冻结首版范围、配置字段和 Windows 优先策略。

**产出**

- 性能基线表。
- Flutter 项目初始化记录。
- 平台能力验证清单。

**出口条件**

- 能在目标 Windows 机器上运行空白 Flutter Desktop 应用。
- 能明确 SMTC 和透明置顶窗口的实现路线。

### Phase 1：项目骨架与领域模型，预计 1–2 天

**任务**

- 创建 Flutter Desktop 项目。
- 配置静态检查、格式化、测试命令和基础 CI。
- 建立 `core`、`renderer`、`platform`、`ui`、`debug` 目录。
- 实现 `DanmakuItem`、`MediaSessionSnapshot`、`AppSettings` 等模型。
- 实现配置读写、schema 版本和旧 INI 导入。

**产出**

- 可启动的控制台窗口。
- 领域模型单元测试。
- 配置迁移测试。

**出口条件**

- `flutter analyze`、格式检查和单元测试通过。
- 旧 `config.ini` 可以转换为新配置，非法值有明确错误。

### Phase 2：XML 解析与时间轴，预计 2–3 天

**任务**

- 实现 Bilibili XML 解析器。
- 使用 Isolate 处理大文件解析和排序。
- 建立时间数组和二分查找。
- 实现正常播放、暂停、跳转、倒退、重新加载状态机。
- 统计坏数据、过滤模式和解析耗时。

**产出**

- `DanmakuParser`。
- `TimelineEngine`。
- 与现有 XML 样本的兼容性测试。

**出口条件**

- 现有 `testDanmaku/` XML 文件均可加载。
- 10 万条数据加载不阻塞 UI。
- 正常播放和任意时间跳转的索引结果正确。

### Phase 3：渲染器最小闭环，预计 3–5 天

**任务**

- 创建透明、无边框、全屏覆盖窗口。
- 实现 `CustomPainter` 和单一帧驱动器。
- 实现滚动、顶部、底部三种模式。
- 实现轨道分配、对象池、文本布局缓存。
- 加入暂停、清空、窗口尺寸变化和 DPI 变化处理。

**产出**

- 不依赖播放器的弹幕压力测试页面。
- 可调整密度、速度、字号和活动弹幕数的基准工具。

**出口条件**

- 500 条活动弹幕在目标设备上稳定 60 FPS。
- 无持续增长的对象数量或缓存数量。
- 窗口移动、缩放、DPI 变化后位置和字体正确。

### Phase 4：Windows 媒体同步与覆盖层行为，预计 3–5 天

**任务**

- 完成 SMTC 会话枚举和当前会话读取。
- 完成 AUMID 发现和目标会话选择。
- 接入播放、暂停、跳转、媒体切换。
- 实现窗口置顶策略、前台判断和鼠标穿透。
- 处理播放器关闭、会话失效和权限错误。

**产出**

- Windows 首版端到端可用流程。
- 平台探针和故障日志。

**出口条件**

- 至少在 PotPlayer 和一个备用播放器上完成播放、暂停、跳转、切换媒体测试。
- 播放器无媒体、暂停或关闭时覆盖层状态正确。
- 不影响播放器鼠标和键盘交互。

### Phase 5：控制面板与调试系统，预计 3–5 天

**任务**

- 实现主界面、设置页、日志页和会话发现对话框。
- 接入所有配置项和分级应用策略。
- 实现实时 FPS、帧耗时、CPU、内存、活动弹幕和缓存指标。
- 提供加载进度、错误提示、日志导出和恢复默认配置。
- 处理窗口关闭、异常退出和后台服务清理。

**产出**

- 功能完整的 Flutter 控制面板。
- 调试面板和性能报告。

**出口条件**

- 当前 Python 版本 README 中列出的功能全部有对应入口。
- 设置保存、重新启动恢复和热应用流程可重复验证。

### Phase 6：测试、优化与发布，预计 3–5 天

**任务**

- 完成单元、渲染、集成和端到端测试。
- 针对 500、1000、2000 条活动弹幕做压力测试。
- 检查发布包、依赖、日志目录、配置目录和升级行为。
- 生成 Windows 安装包或便携版。
- 完成 macOS/Linux 可行性验证，不在未验证的平台上宣称完整支持。

**出口条件**

- 关键测试通过，性能指标有实际采样记录。
- 发布包可在干净环境启动。
- 旧配置和弹幕文件可迁移。
- 失败时可以回退到 Python 版本。

## 10. 测试策略

### 10.1 单元测试

- XML 字段解析、颜色转换和模式过滤。
- 无效 XML、空文本、非法数字和超长文本。
- 时间轴二分查找和游标推进。
- 播放跳转、倒退和暂停恢复。
- 轨道分配和不重叠策略。
- 对象池借出、回收和容量上限。
- 配置范围校验、默认值和版本迁移。

### 10.2 渲染测试

- 滚动弹幕从右侧进入并从左侧离开。
- 固定弹幕显示时长正确。
- 文本描边、颜色、透明度和字体变化正确。
- 高 DPI 和窗口尺寸变化不产生裁切。
- 长文本、中文、英文、emoji 和混合字符不导致布局异常。

### 10.3 集成测试

- 选择文件、加载、开始、停止和重新加载。
- 发现媒体会话并选择目标 AUMID。
- 播放、暂停、跳转和媒体切换。
- 控制面板关闭时清理覆盖层和媒体监控任务。
- 配置保存后重启恢复。

### 10.4 性能测试

固定以下变量后再比较不同实现：

- 分辨率、DPI、屏幕刷新率。
- 字体、字号、描边和透明度。
- 活动弹幕数、总弹幕数、文本平均长度。
- 是否允许重叠。
- 是否显示调试层。

至少采集平均 FPS、P95/P99 帧耗时、CPU、内存、GC/对象分配趋势、缓存命中率和丢弃弹幕数量。

## 11. 风险与应对

| 风险 | 影响 | 应对 |
|---|---|---|
| Flutter Desktop 窗口扩展能力不足 | 高 | Phase 0 先做原生探针；必要时使用最小 C++ 插件 |
| SMTC 在不同播放器上行为不一致 | 高 | 以会话快照接口隔离；为每个播放器保留兼容日志 |
| 每帧文字布局开销过高 | 高 | 文字布局缓存、预渲染、批量绘制和压力测试 |
| Dart Isolate 数据传输成本过高 | 中 | 解析结果批量传输，避免逐条 SendPort 通信 |
| 5000 条弹幕的目标不可稳定复现 | 中 | 将首版验收目标定为 500/2000 条，并以设备基准为准 |
| Tauri 与 Flutter 重复承担桌面壳职责 | 中 | 首版不叠加 Tauri，除非出现明确系统集成需求 |
| 配置格式变化造成用户设置丢失 | 中 | 版本化迁移、保留旧文件、迁移前备份 |
| 多显示器和 DPI 导致覆盖层错位 | 中 | 单独做窗口几何测试，保存逻辑屏幕而非物理像素坐标 |

## 12. 推荐目录结构

```text
flutter_app/
├── lib/
│   ├── main.dart
│   ├── app.dart
│   ├── core/
│   │   ├── models/
│   │   ├── parser/
│   │   ├── playback/
│   │   ├── config/
│   │   └── logging/
│   ├── renderer/
│   │   ├── danmaku_overlay.dart
│   │   ├── danmaku_painter.dart
│   │   ├── track_allocator.dart
│   │   └── text_layout_cache.dart
│   ├── platform/
│   │   ├── media/
│   │   ├── overlay/
│   │   └── platform_services.dart
│   ├── state/
│   ├── ui/
│   │   ├── shell/
│   │   ├── player/
│   │   ├── settings/
│   │   ├── discovery/
│   │   └── logs/
│   └── debug/
├── test/
├── integration_test/
├── assets/
├── windows/
├── macos/
├── linux/
├── pubspec.yaml
└── README.md
```

## 13. 交付顺序与回滚策略

建议采用“双轨迁移”：

1. Python/Qt6 版本继续作为当前可用版本，不在 Flutter 尚未通过端到端验收前删除。
2. Flutter 先完成离线解析和压力测试，再接入 Windows 媒体同步。
3. Flutter 版本提供独立配置目录或明确的迁移开关，避免两个版本同时写入同一份配置。
4. Windows 首发稳定后，再将 Flutter 设为默认入口。
5. 至少保留一个 Python 版本的可运行标签或发布包，便于快速回退。

## 14. 最终验收清单

- [ ] Windows Release 包可在干净环境启动。
- [ ] 可以选择并加载现有 Bilibili XML 弹幕文件。
- [ ] 滚动、顶部、底部弹幕行为正确。
- [ ] 播放、暂停、跳转、倒退、媒体切换同步正确。
- [ ] AUMID/媒体会话发现可用。
- [ ] 覆盖层透明、置顶、鼠标穿透和多显示器行为正确。
- [ ] 设置保存、恢复默认和热应用正确。
- [ ] 日志、错误提示和调试信息可用。
- [ ] 500 条活动弹幕场景稳定 60 FPS。
- [ ] 2000 条活动弹幕压力测试结果已记录，并明确设备、分辨率和配置。
- [ ] 配置迁移和 Python 版本回滚路径已验证。
- [ ] 未验证的平台能力不会在 UI 或 README 中被标记为已完成。

## 15. 第一周建议任务拆分

### 第 1 天

- 初始化 Flutter Desktop 项目。
- 确认 Windows 编译链和目标最低系统版本。
- 做 SMTC 会话读取和透明置顶窗口两个独立探针。
- 建立性能基线采集脚本。

### 第 2 天

- 完成领域模型、配置模型和旧 INI 导入。
- 加入静态检查、测试和格式化流程。
- 迁移 XML 解析测试样本。

### 第 3 天

- 完成 Isolate XML 解析。
- 完成排序、时间数组和时间轴游标。
- 覆盖跳转、暂停和倒退测试。

### 第 4 天

- 完成透明覆盖层和最小 `CustomPainter`。
- 实现单条滚动弹幕与固定弹幕。
- 加入帧耗时采集。

### 第 5 天

- 完成对象池、轨道分配和文本缓存初版。
- 跑 500/1000/2000 条压力测试。
- 根据实测结果决定是否需要位图缓存或更底层的原生渲染扩展。

第一周结束时的判断标准不是“项目目录是否完整”，而是能否证明：解析不阻塞 UI、覆盖层能稳定绘制、时间轴行为正确，并且 Windows 平台能力有可行的实现路径。
