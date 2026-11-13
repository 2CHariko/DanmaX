# FluentWinUI3 界面设计系统与交互规范

本文档定义 DanmaX 控制界面的 FluentWinUI3 设计规范、窗口布局、卡片与滚动组件契约、图标资产约束、原生系统对话框规则及界面验收范围。

> [!IMPORTANT]
> 所有控制面板与弹窗界面必须基于锁定 Qt 的官方 `import QtQuick.Controls.FluentWinUI3` 构建，保留官方 Fusion 回退依赖；严禁引入第三方 FluentUI、自行绘制非原生图标或使用 Qt 非原生文件选择器。弹幕覆盖层属于内容渲染层，不套用本文档中的控制面板卡片、底色或字号规则。

## 1. 控件库边界与主题语义

- **官方控件保真**：按钮、开关、输入框、下拉主体、滑块、标签页、对话框与滚动条均保持 Qt 官方 FluentWinUI3 默认实现、字体、圆角与交互状态，严禁调用 Qt 私有样式 API；`TabButton` 在 Qt 6.11 下允许将公开 `icon.color` 绑定至 `palette.buttonText`。
- **主题管理**：默认跟随系统主题，用户选项仅为 **系统**、**浅色**、**深色**；主题色、字号、间距与圆角统一集中于 `qml/theme/Ui.qml` 语义层管理，禁止在各页面散落固定浅色 `palette` 或硬编码状态颜色。系统启用高对比度时单独适配并跟随系统语义。
- **实时生效设置**：设置默认即时生效并保存，文本字段在完成编辑后校验提交，高频输入节流持久化；不提供统一的“应用全部”按钮，不要求无条件重启。

## 2. 主窗口布局、Mica 材质与内置观看例外

- **主窗口结构**：常规最小尺寸 `800 × 600` 逻辑单位（可用工作区不足时降至工作区尺寸减 32），默认启动尺寸 `1080 × 760`；导航展开阈值为 `1008`。上部采用“左侧导航栏 + 右侧主内容区”并排布局，下部由全宽底部播放条独占；侧栏、主内容背景与 1 px 垂直分割线止于播放条顶部。
- **侧栏导航规范**：使用标准 `ItemDelegate` 并配备左侧主题色胶囊指示条（Pill Indicator）；上部为 **首页 / 播放 / 媒体库**，底部依次为 **日志 / 关于 / 设置**。图标基准 `20`、图文间距 `12`、展开内边距 `16 / 12`，尺寸集中于 `Ui` 并响应文字缩放；窄屏收缩为纯图标模式，保留键盘焦点、可访问名称与工具提示。
- **Mica 与沉浸式标题栏**：通过静态编译的 `QWindowKit` 与 `WindowBackdropController` 在主窗协同管理 Mica 底板与 Windows 11 Snap Layouts 贴靠布局。左侧导航栏透出原生 Mica，右侧主内容区叠加半透明基底色（`Ui.contentAreaSurface`）；顶部搜索框位于窗口按钮左侧居中区域（最大宽度 `440`、紧凑高度不低于 `28`），通过 QWindowKit 公开 API 排除拖拽命中。
- **内置视频观看布局例外**：仅当开发版内置播放器处于播放页时隐藏侧栏、垂直分割线与视频周围页面边距，窗口模式保留返回与 QWindowKit 窗口按钮及固定底栏，全屏模式隐藏标题栏并自动收起独立控制层；观看界面与底栏菜单通过 `Ui` 局部深色语义呈现，不改写全局主题。详见 [播放底栏与内置观看布局](../playback/playback-presentation.md)。

## 3. Fluent 卡片组合与统一组件

- **独立卡片体系**：`SettingsRow` 与 `ActionCard` 统一采用 `Ui.cardMinHeight`（`70 px`）、`Ui.cardPaddingX/Y`（`16 px` 内衬）、圆角、半透明背景（`Ui.cardBackground`）与 1 px 微细描边（`Ui.cardBorder`），卡片间保持 `4 px` 垂直间距（`Ui.cardGap`）。
- **分组容器**：`SettingsSection` 仅提供外置分组标题、说明与卡片流编排（`card: false` 时无外层大底板，仅在复合控制面板显式启用整体底板）。页面最大宽度 `1040`、组间距 `24`、宽窗边距 `32`、窄窗 `16`。文字缓存、诊断与关于默认展开展示。
- **`AppComboBox` 与 `PopupSurface`**：
  - 浮层采用公开 QML 实现（`8 px` 外圆角、`4 px` 内嵌胶囊项、`3 × 16` 左侧指示条、`36 px` 最小行高）并复用 `RectangularShadow`（模糊 12、下偏 3、浅/深 16%/32% 透明度；软件渲染或高对比度自动禁用）。当前仅提供纯色回退，不宣称原生 Acrylic 或 ThemeShadow（历史调研见 [Acrylic 探针](../research/probes/acrylic-popup-probe.md) 与 [XAML Islands 评估](../research/winui-xaml-islands.md)）。
  - 非编辑态有有效选中项时围绕当前行原地展开并隐藏原控件视觉外观，长列表最高 `360 × 文本缩放` 且距窗边至少 `8` 逻辑单位，支持 `F4` / `Alt+Down`、方向键、`Enter` 与 `Escape`。

## 4. 统一滚动条与完整容器滚轮契约（`AppScrollBar`）

- **通道隔离布局**：所有控制界面（整页、内嵌列表、弹窗、下拉浮层与横向列表）统一复用 `AppScrollBar`。滚动条位于内容视口之外的独立留白通道内，完整交互区距所属容器边缘 `Ui.scrollEdgeInset`（`4` 逻辑单位），与内容至少间隔 `Ui.scrollGap`（`8`）。`Ui.scrollInset()` 按官方 `implicitWidth`/`implicitHeight` 与最小内边距较大值并按 4 单位向上取整；启用方向设为 `AsNeeded`，无需方向设为 `AlwaysOff`，显隐与溢出状态不得改变内容宽度或重复扣减条目宽度。
- **完整容器滚轮兜底**：`AppScrollBar` 的 `wheelRegion` 必须显式关联所属完整容器，覆盖内容区、左侧留白、内容与滚动条之间、滑块/轨道、外侧留白及上下留白（排除弹窗底部动作区）。像素增量优先于角度增量；横向列表上的普通纵向滚轮/触控板手势交还外层页面，横向手势与 `Shift + 滚轮` 驱动列表自身；容器隐藏或销毁时自动停用并释放处理器。

## 5. 原生系统文件对话框与 Fluent SVG 图标资产

- **Windows 原生文件选择器**：本地文件与目录的打开/保存必须使用系统原生对话框，**严禁设置 `DontUseNativeDialog` 或在原生创建失败时回退 Qt 绘制的非原生对话框**。绑定任务的对话框在任务失效、换片或所属匹配窗关闭时立即作废请求并请求关窗，迟到结果一律拒绝；无法满足窗口归属或关闭契约时使用 Windows SDK 原生适配并在失败时报告错误。
- **官方 Fluent System Icons SVG**：严禁在 QML 中使用 Canvas 或几何拼凑手绘功能图标。应用图标与标题栏按钮统一采用微软官方 Fluent System Icons SVG（集中管理于 `resources/fluent/`，通过 `resources/fluent/manifest.json` 锁定来源提交与 SHA-256，由 `scripts/verify-fluent-assets.ps1` 校验，`scripts/prepare-fluent-assets.ps1 -Offline` 离线重建深色改色稿）。通过 `Ui.icon(name)` 与 `FluentIcon` 按 `逻辑尺寸 × DPR` 栅格化调用；`QtSvg` 必须随包锁定与部署（静态方案显式包含 `QSvgPlugin` 与 `QSvgIconPlugin`）。品牌 Logo、番剧封面与 Qt 官方控件内部图形不强制转 SVG。

## 6. 首页搜索、异步占位与快捷交互组件

- **首页顶部搜索与番剧详情**：使用官方 `SearchField`（512 字符上限、`Ctrl+F` 聚焦、回车/按钮触发手动搜索、最多 20 条相关性结果）。详情弹窗先打开再异步请求并保留 `CachedCoverImage` 与 `LoadingPlaceholder`（延迟 150 ms 显示共享脉冲骨架，遵循 `SystemUiPreferences` 减少动画/高对比度开关）；剧集每组最多 100 项，通过单个 `FrameAnimation` 每帧最多追加 8 项（超过 2 ms 让出）。
- **全局视频拖放反馈**：仅在无弹窗打开时接受单视频拖放，使用 `QtQuick.Effects.MultiEffect` 捕获一次背景（最长边不超过 1600 px、模糊上限 40、深/浅底色遮罩 75%/80%），退出时通过 `Loader` 销毁纹理；软件渲染与高对比度回退纯色。
- **弹幕快捷工具栏与调试信息**：覆盖层调试信息默认宽 `320` 逻辑单位、标题最多两行纯文本省略。快捷工具栏以弹幕窗为所属窗口，由 `WindowLayerController` 统一协调 Z 序；顶部感应带与主条同宽、高 `10` 逻辑单位，未固定时离开整体区域 1 秒隐藏。

## 7. 验收要求

UI 自动化与人工验收须覆盖：
1. 三核心页面（首页、播放、媒体库/设置）的浅/深主题、`520` / `800` / `1080` 窗宽、放大文字（如 `13.5 pt`）与高 DPI 缩放；
2. 逐点验证内容区、左侧留白、内容与滚动条间隙、滑块/轨道、外侧留白及上下留白的真实滚轮（含像素/角度、边界与嵌套目标）、滑块拖动及长下拉展开（当前项在末尾、上下展开、缩窗）；
3. Mica 合成效果须使用桌面合成截图验证（软件渲染纯色截图仅用于布局核对）；Narrator 屏幕阅读器、系统高对比度与跨屏表现须独立记录，未完成实机检查前不得宣称验收通过。

## 相关内容

- [看板娘设计设定集](mascot-design.md)
- [C++ / Qt 核心架构](../architecture/cpp-qt-architecture.md)
- [播放底栏与内置观看布局](../playback/playback-presentation.md)
- [Acrylic 浮层最小验证](../research/probes/acrylic-popup-probe.md)
- [WinUI 3 XAML Islands 评估](../research/winui-xaml-islands.md)
