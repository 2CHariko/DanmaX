# FluentWinUI3 界面设计与实现约束

状态：0.2 控制界面已按此基线实现；完整无障碍/高对比度实机验收仍待完成。
官方文档核对日期：2026-10-07。项目锁定 Qt 6.11.0；Qt 6.11 在线文档可能显示后续补丁版本，具体 API 和控件支持以项目内 SDK 及编译验证为准。

## 1. 规范来源与适用范围

本项目全部控制界面遵循 **Qt 官方 FluentWinUI3 + 少量自建组合组件**。标准控件的视觉和状态以 Qt FluentWinUI3 实现为基准，导航、设置、排版和交互模式参考微软 Windows/WinUI 官方设计指南。本文标注为“项目决策”的内容是对这些指南的落地选择，不冒充官方强制要求。

- 适用：控制面板、导航、设置、会话选择、日志、错误反馈和对话框；不能只替换按钮皮肤后宣称完成。
- 弹幕文字及视频上方的性能调试层属于内容渲染，不套用控制面板的卡片、窗口底色或 UI 字号。它们的配置入口仍遵守本规范。
- Qt FluentWinUI3 是 Qt Quick 风格，不是微软原生 WinUI 运行时；不为外观引入 Windows App SDK、WinUI XAML、第三方 Fluent 库或另一套 UI 框架。[S1]
- Microsoft 文档中的 XAML 控件、ThemeResource 和类型名称仅作为设计参考，不能直接当成存在的 QML 类型或属性。
- 当 Qt 能力与设计目标有差距时，记录差异及替代方案，验证后采用；不得静默换风格、依赖私有 API 或声称像素级复刻。

## 2. 唯一风格与基础控件

**项目决策：采用编译期风格选择。** 每个直接使用 Controls 类型的项目 QML 文件统一使用：

```qml
import QtQuick
import QtQuick.Controls.FluentWinUI3
```

纯布局、纯内容渲染文件无需强行导入 Controls。禁止项目代码同时显式导入 Basic、Fusion、Material、Universal、Windows 或动态的 `QtQuick.Controls` 来混搭皮肤；不提供用户切换控件风格的入口。浅色/深色切换是主题切换，不是风格切换。[S2]

- Button、Switch、CheckBox、RadioButton、ComboBox、Slider、SpinBox、TextField、Menu、Dialog 等优先使用风格提供的控件。
- 不替换标准控件主体的 `background` / `contentItem` 去重画 Fluent 外观；不按截图硬编码其内边距、描边、圆角和状态颜色。明确例外：共享 AppComboBox 可替换原 `popup.background` 为统一纯色主题表面，消除官方图片噪点的大尺寸拉伸；原 popup、列表 contentItem、输入框和选项状态继续使用官方实现。
- 保留默认的 hover、pressed、disabled、checked、selected、focus 状态和键盘行为。突出动作使用已有语义属性或受支持的调色接口，不另画一套“主按钮”。
- 自建组件优先组合标准控件。只有导航标记、设置容器表面等缺失的组合视觉允许集中绘制，并纳入状态、主题与无障碍检查。
- Qt FluentWinUI3 基于部分图片资源，不保证所有像素都能通过 palette 修改；不承诺任意换肤。[S1]

### 官方回退边界

Qt 6.11 文档列出部分控件仍由 Fusion 回退，包括 Drawer、StackView、SplitView、SwipeView、TreeViewDelegate 等；项目内 FluentWinUI3 `qmldir` 也声明了 Fusion 依赖。[S1]

官方内部回退是平台实现边界，不等同于允许业务代码任意混搭。新增此类控件前应：

1. 核对锁定 SDK 的实际支持及回退。
2. 对可见控件记录位置和视觉差异；无明显视觉外观的容器核对交互行为即可。
3. 优先用标准布局与已支持控件组合；仍有必要时保留官方回退并纳入验收，不复制 Qt 私有样式实现。
4. 发布时允许带上官方需要的 Fusion 插件，禁止为“只用 Fluent”误删传递依赖。

## 3. 主题、颜色与材质

- 默认跟随 Windows 系统主题；若提供应用主题设置，只提供“跟随系统 / 浅色 / 深色”，并持久化用户选择。系统主题变化应即时反映。[S3][S4]
- 基础控件继承风格 palette/font，不在窗口顶层写死整套浅色 palette，也不在页面散落白色背景、黑色文字或固定品牌蓝。
- 自建容器集中使用语义角色：窗口底色、内容表面、主/次文字、边框、强调、禁用、选中、错误。角色映射由一个主题适配层维护；未直接暴露的 WinUI token 只能作为项目内部命名，不伪造 `FluentWinUI3.xxx` API。
- 优先使用 Qt 的系统调色板和 `QStyleHints::colorScheme`；用户覆盖使用该 SDK 可用的公开接口，恢复“跟随系统”要撤销覆盖。监听变化后刷新自建组件，而非只在启动时读取。[S9]
- 强调色服务于选中状态与主要动作，不用于大面积装饰；错误和警告同时提供文字或图标，不只依赖颜色。[S4]
- 高对比度独立于浅色/深色。主题层必须响应系统对比度偏好并保留系统色，不能把高对比度处理成普通暗色皮肤。Qt 6.11 的公开对比度入口是 `QStyleHints::accessibility` 下的 `QAccessibilityHints`；具体效果必须实测。[S10][S11]
- 首版保留系统标题栏。Mica/Acrylic 不是启用 Fluent 风格后自动获得的能力，也不是首版必备项；未来如接入，放在 Windows 适配层，支持关闭透明效果、高对比度及平台不可用时的纯色回退。不能用整窗透明度或逐帧模糊冒充材质效果。[S12]
- 不把控制窗口的主题底色应用到透明弹幕覆盖窗。

## 4. 布局、排版和图标

| 项目 | 规则与依据 |
|---|---|
| 尺寸单位 | 使用 QML 逻辑尺寸，验证 DPI 和文本缩放；不额外重复乘以 DPR。微软指南使用 effective pixels，移植到 Qt 后仍须核对实际显示。[S5] |
| 间距 | 自建布局用 4 的倍数组织边距和间距。项目常用 token 为 4/8/12/16/24/32；不可据此强制修改标准控件内部尺寸。[S5] |
| 页面边距 | 参考窄窗口 12、宽窗口 24 的官方建议，统一由容器控制；不在每页自定 18、28 等无依据数值。[S5] |
| 尺寸自适应 | 优先 implicit size、Layout、可滚动内容；文字换行或增大时允许行高增长，禁止固定高度截断说明和错误信息。 |
| 内容宽度 | 设置页为单列滚动，参考 1000–1100 逻辑宽度上限；项目初值 1040，后续按实际中文和缩放验证。[S3] |
| 字体 | 标准控件继承系统/风格字体。自建文本优先 Windows UI 字体及系统中文回退，不打包商业字体或强制所有 UI 使用微软雅黑。[S6] |
| 字阶 | 项目采用 Caption 12、Body 14、BodyStrong 14 Semibold、Subtitle 20 Semibold、Title 28 Semibold 的语义角色；参考 Windows 字阶，不把这些名称当成 Qt 内置样式。标准控件仍保留自身默认字体。[S6] |
| 强调与对齐 | 使用 Semibold 强调；正文和标题默认起始边对齐，不靠大面积加粗、全大写或居中装饰制造层次。[S6] |
| 图标 | 同一套 Fluent 语义图标体系，明确来源。可使用系统 Segoe Fluent Icons 或许可证合适的向量资源；无字体时有回退。禁止 emoji、随意拼 Unicode 或混用不一致图标库作为功能图标。[S7] |

自定义字号须通过统一文本样式层应用，不能假定固定 `font.pixelSize` 会自动完整遵守 Windows 文本大小设置。需要在 100%、150%、200% DPI 和增大系统文本的场景验证。

## 5. 少量自建组件的边界

以下名称描述项目组合边界，不是 Qt 内置控件。当前实现 Main、NavigationPane、SettingsSection、SettingsRow 和 Ui 语义单例；未创建无实际用途的 Expander。

| 组件 | 允许职责 | 禁止职责 |
|---|---|---|
| `AppShell` | 窗口内容布局、导航区域、页面承载、标题 | 每页一套标题栏、主题或路由系统 |
| `NavigationPane` / `NavigationItem` | 标准 delegate 组合、选择状态、收起展开、页面入口 | 用纯 MouseArea 假冒按钮、加载隐藏业务、复制整套 WinUI 库 |
| `SettingsSection` | 标题、说明和同类设置的垂直分组 | 强制每组巨大卡片、重复导航或业务存储 |
| `SettingsRow` | 标题/描述/可选图标/标准操作控件、窄宽度重排 | 自绘开关或输入框、重复提交状态 |
| `SettingsExpander` | 单层折叠的低频设置组合 | 多层嵌套、默认隐藏错误或关键操作 |

**项目决策：**组件位于 `qml/components/`，页面位于 `qml/pages/`，颜色/尺寸/字体语义集中在 `qml/theme/` 或对应 C++ 主题适配层。按实际复用提取，不预先生成通用组件库和空占位组件。

Windows 设置指南推荐的 SettingsCard / SettingsExpander 来自 Windows Community Toolkit。本项目只参考其布局和交互模式，用 Qt 自带控件完成上述少量组合，不引入 Toolkit 或假设这些类型存在于 Qt。[S3]

## 6. 导航规范

- 项目采用左侧导航：主要工作页与日志页放主体区域，“设置”固定在底部。页面数量少时保持浅层结构，不为占位功能制造空入口。[S3][S8]
- 导航内容、顺序、选中状态和返回行为在 `AppShell` 统一管理；切页保留必要页面状态，重复点击当前页不重启播放器或重载文件。
- 参考 NavigationView 的自适应行为：宽度 ≥1008 展开、641–1007 紧凑图标栏、≤640 菜单入口。以上为采用的参考初值，实际支持的窗口范围、中文和文字缩放须验证；如果缩放或内容需要提前收起，集中调整，不按设备名称判断。[S8]
- 紧凑导航仍提供工具提示和可访问名称；选中状态不只靠颜色，焦点与选中不可混为一谈。
- 键盘能够到达、遍历并激活导航项；页面切换后的焦点有明确位置。收起菜单支持 Escape 关闭、恢复触发按钮焦点，不将焦点困在不可见区域。
- 不把全局导航包成只有鼠标点击的装饰列表。交互底层优先保留 Qt delegate/button 的输入与无障碍能力。

## 7. 设置分组与操作语义

- 设置用于相对稳定的偏好：外观、弹幕显示、同步行为、窗口行为、诊断等。选文件、开始/停止、连接播放器等常用操作留在工作页。[S3]
- 每行使用简短明确的标题，必要时附说明；宽窗口标签在左、操作控件在右，窄窗口操作控件下移，不压扁控件或省略关键解释。
- 开关表示立即生效的二元偏好；复选框表示选择/确认；互斥少量选项可用 RadioButton，较多或需紧凑显示时用 ComboBox。按钮表示执行动作，不假装是持续状态。[S3]
- **设置默认即时应用并保存，不设置统一的“应用全部”按钮。**输入框在完成编辑且校验通过后提交；滑块等高频操作可实时预览、节流写盘。需要重建缓存时由应用层处理，不无条件重启整个弹幕系统。[S3]
- 保存失败必须保留可解释的状态和重试入口，不能界面显示成功但磁盘没有保存。确需重启/重新加载的选项就地说明，属于少数明确例外。
- 导航型设置行可以整行点击；含开关、输入框等独立操作控件的行不得再用外层点击触发第二次操作。
- 无效或不可用设置保留位置并显示原因；输入错误在对应字段附近提示，焦点和用户输入尽量保留。[S3]
- 低频高级项最多使用一层折叠；不通过深层折叠隐藏关键设置。“关于”位于设置页底部。[S3]
- 恢复默认等批量覆盖操作提供明确范围说明和确认/撤销策略；普通主题切换、滚动速度修改不弹确认框。

## 8. 反馈、动效与无障碍

- 优先保留官方控件已有动效，自建导航/展开仅添加必要的状态过渡；不引入装饰性循环动画、大范围弹跳或另一个动画框架。[S13]
- 减少动画遵循系统偏好；若 Qt 未直接暴露所需信号，由平台层查询并在主题层传递，不虚构 QML API。关闭过渡不能影响焦点、状态或操作完成。该规则针对 UI 动效，不自动改变用户明确启用的弹幕播放功能。
- 长任务显示真实忙碌/进度状态；加载、空结果、错误、成功有不同文案。无法估计进度时用忙碌指示，不编造百分比。
- 控件通过 Tab/Shift+Tab 可达，焦点清晰可见；Enter/Space/Escape 遵循所用控件语义。模态对话框正确限制并恢复焦点，普通通知不抢焦点。[S14]
- 图标按钮必须有 Accessible 名称，输入框关联标签，组合组件公开角色、选中/展开/禁用状态及对应动作。优先使用标准控件语义，必要时使用 `Accessible` 附加属性。[S15]
- 标题、说明和错误均可被辅助技术理解；不能只依赖 tooltip 或颜色传达关键含义。高频动画时间和 FPS 不逐帧向读屏器播报。
- 用 Narrator 或等价辅助技术检查自建组件；不因 Qt 标准控件具有基础支持而宣称整个组合界面自动无障碍。

## 9. 评审与验收

每次新增/修改界面按影响范围验证，不为纯文档变更运行完整 GUI 测试。

- 风格：项目 QML 的 Controls 导入一致；没有显式混搭、自绘基础控件或未经记录的可见回退。
- 主题：浅色、深色、跟随系统及运行中切换；高对比度单独验证，截图不是唯一证据。
- 状态：正常、hover、pressed、focus、disabled、selected；错误、空结果、加载状态有真实可操作反馈。
- 布局：导航三种模式中应用实际支持的范围、窄窗口、长中文、字体回退、DPI/文本缩放；无裁剪和遮挡。
- 交互：键盘全流程、焦点恢复、读屏器名称/角色/状态；减少动画模式下功能完整。
- 部署：`windeployqt` 包含 FluentWinUI3 及官方传递依赖，在去除 SDK 路径的环境中加载成功；不手工删除 Fusion 依赖。
- 性能：控制面板主题变更不触发逐条弹幕对象或图形资源无意义重建，不把 UI 材质效果叠加到弹幕渲染热路径。

禁止在仅替换 import、截图通过或 offscreen smoke 通过后，宣称“全面符合 Fluent/WinUI 无障碍规范”。报告必须区分设计遵循、代码实现与实机验证。

## 10. 当前迁移清单

- Main/播放/日志/设置已使用 FluentWinUI3；导航为标准 ItemDelegate，页面为 StackLayout，未使用需要单独可见回退的 Drawer/TreeView。
- `qml/theme/Ui.qml` 集中语义字阶和导航阈值；标准控件保留官方样式，中文使用系统 Microsoft YaHei UI 回退，无附带字体。
- 系统/浅色/深色通过 QStyleHints 选择；高对比度偏好变化撤销应用主题覆盖，保留系统色。该代码路径尚未在 Windows 高对比度模式下人工验收。
- 设置使用标准控件组合、窄屏重排；数值编辑即时更新、200 ms 合并写盘；原子保存失败显示重试。
- 已检查浅/深色、520/800/1080 逻辑宽度、1/1.5/2 倍缩放截图。环境变量 DPI 模拟不能替代跨显示器、Windows 文本缩放和辅助技术测试。
- 导航 Accessible 名称/选中状态、菜单通过标准可切换按钮 checked 状态及文字说明表达展开/收起，Escape 返回焦点已实现（不使用该版本不存在的 Accessible.expanded 属性）。未添加自建过渡动画；官方控件减少动画行为、高对比度、Narrator 和完整键盘流程仍待人工验收。
- 部署保留 FluentWinUI3 及官方 Fusion 等传递依赖；不声称像素级原生 WinUI 或全面无障碍达标。

## 官方参考

- [S1：Qt 6.11 FluentWinUI3 Style](https://doc.qt.io/qt-6.11/qtquickcontrols-fluentwinui3.html)
- [S2：Qt 6.11 Styling Qt Quick Controls](https://doc.qt.io/qt-6.11/qtquickcontrols-styles.html)
- [S3：Microsoft Guidelines for app settings](https://learn.microsoft.com/en-us/windows/apps/design/app-settings/guidelines-for-app-settings)
- [S4：Microsoft Color in Windows](https://learn.microsoft.com/en-us/windows/apps/design/style/color)
- [S5：Microsoft Alignment, margin, and padding](https://learn.microsoft.com/en-us/windows/apps/design/layout/alignment-margin-padding)
- [S6：Microsoft Typography in Windows](https://learn.microsoft.com/en-us/windows/apps/design/style/typography)
- [S7：Microsoft Icons in Windows apps](https://learn.microsoft.com/en-us/windows/apps/design/style/icons)
- [S8：Microsoft NavigationView](https://learn.microsoft.com/en-us/windows/apps/design/controls/navigationview)
- [S9：Qt 6.11 QStyleHints](https://doc.qt.io/qt-6.11/qstylehints.html)
- [S10：Microsoft Contrast themes](https://learn.microsoft.com/en-us/windows/apps/design/accessibility/high-contrast-themes)
- [S11：Qt 6.11 QAccessibilityHints](https://doc.qt.io/qt-6.11/qaccessibilityhints.html)
- [S12：Microsoft Mica](https://learn.microsoft.com/en-us/windows/apps/design/style/mica)
- [S13：Microsoft Animations in Windows apps](https://learn.microsoft.com/en-us/windows/apps/design/motion/motion-in-practice)
- [S14：Microsoft Accessibility overview](https://learn.microsoft.com/en-us/windows/apps/design/accessibility/accessibility)
- [S15：Qt 6.11 Accessible QML Type](https://doc.qt.io/qt-6.11/qml-qtquick-accessible.html)

## 10. 整体布局与工作流（2026-10-08）

- 三页共用窗口/内容/分组表面与语义文字。颜色取自系统调色板，普通主题的辅助文字和细边框集中混合；高对比度直接使用系统文字色。Windows 的 alternateBase 可能带明显色相，不作为中性卡片底色。
- PageFrame 管理标题、单列滚动与 1040 内容宽度；SectionCard 用于播放功能分区；Disclosure 使用标准 Button 组合单层折叠，收起时恢复内部焦点到触发按钮。设置行按可用宽度重排。
- 播放页按来源、方式、控制组织。默认跟随播放器；待启动时选择模式并点击开始，运行时锁定模式，独立模式可暂停/跳转，同步进度只读。停止按钮明确标注“停止并卸载”。应用 ID 和运行指标默认折叠。
- 在线配置入口通过 Main 导航并定位服务地址。未配置时不展示搜索表单；搜索结果、剧集与下载操作按状态出现，忙碌时取消始终可见。
- 设置分为应用外观、弹幕样式、弹幕播放、同步与窗口、在线弹幕、高级设置、配置与关于；高级设置内文字缓存和诊断各自单层折叠。配置键与保存语义不变。
- 日志保持精确级别筛选与全量导出；显示总数、匹配数及空状态。新增 LogModel.countForLevel 只读接口，不改变日志存储。
- 本地 XML 原有行为是在准备前停止卸载，失败不会恢复旧来源；在线准备失败保留当前播放。此次布局调整没有统一这两种后端语义。
- 可重复验证入口：动态构建的 ui_workflow CTest，以及 scripts/validate-ui.ps1 对指定静态部署 EXE 的三页浅深主题、520/800/1080 宽截图。系统高对比度、Narrator 与真实跨屏 DPI 必须另行验收。

## 11. 滚动与下拉弹出层（2026-10-08）

- 所有业务下拉框复用 AppComboBox（官方 Fluent ComboBox 派生组合）。使用公开 popup/ListView 属性与 ScrollBar 附加属性，保留原始列表与输入框 contentItem；字体组件只维护编辑、补全和提交，不重复实现弹出层。
- 默认弹出高度上限 360 逻辑单位，随语义文本缩放；向下空间不够时优先向上，尺寸及位置受窗口四边 8 单位安全边距约束。宽度跟随输入框并限制到窗口，打开后窗口/模型变化重新计算；选中和键盘高亮项保持可见。
- 下拉选项使用官方 ItemDelegate，单行省略、完整文本 tooltip 和可访问名称；预留固定滚动条宽度及 8 单位间隙。空/短列表隐藏滑块，长列表显示并可拖动，不因显示/隐藏改变选项宽度。
- 唯一背景例外：下拉 popup.background 使用语义纯色不透明表面、细边框、8 单位圆角。高对比度直接使用系统窗口/文字色；不能将例外扩展到按钮、输入框主体或其他弹出层。不得修改 SDK、依赖私有 Config/StyleImage 或显式导入另一种 Controls 风格。
- 页面滚动条距内容面板右边 8 单位，内容至少让出滑块宽度和 8 单位间隔；列表各自在所属区域预留空间。使用 ContentScrollBar 统一公开 palette/显示策略，保留官方 Fusion 回退的形状和拖动行为；记录其视觉差异，不宣称原生 WinUI 滚动条。
- 截图/交互验收必须包括页面滚动中部/底部和下拉展开，覆盖空短长列表、末尾当前项、上下展开、动态模型、窗口缩小、鼠标拖动/滚轮、键盘及浅深/DPI/文字缩放。初始页面截图不足以验收。
- 共享下拉框补充 F4 / Alt+↓ 展开或关闭；其余方向键、Enter、Escape、字体编辑/补全仍交给官方控件。字体框不能用 Space 作为展开测试，因为它是可编辑输入。

### 在线服务列表

在线弹幕设置使用官方 TextField/Button 组合有序地址列表，每项提供上移、下移、删除，另设添加按钮。空白新增项仅为草稿，完成编辑后校验，非法或重复地址保留输入并显示文字原因；列表支持删除为空。删除后焦点转到相邻地址或添加按钮，窄窗口按钮通过 Flow 换行。普通页面滚动负责容纳长列表，不增加嵌套滚动区域。运行状态显示实际服务及回退序号。当前验收及缩放/弹出层限制见 FRAMEWORK_STATUS.md 的多服务章节。
