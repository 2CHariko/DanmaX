# Qt 官方 FluentWinUI3 界面规范

2026-10-08：用户授权返回 Qt 官方 FluentWinUI3，并以官方默认控件和简单布局替代第三方 FluentUI 与仿 Windows 设置卡片。本决定取代此前第三方库迁移方案。

## 控件与组合边界

- 页面编译期导入 `QtQuick.Controls.FluentWinUI3`，不显式导入 Basic/Fusion 或第三方 FluentUI。Qt 官方模块内部的 Fusion 回退保留，尤其 ScrollBar；不宣称原生 WinUI 或像素级一致。
- 按钮、开关、输入框、下拉主体、标准选项、滑块、标签页、对话框、滚动条保持官方默认背景、内容实现、字体、圆角和交互状态。不访问 Qt 私有样式 API。
- Qt 6.11 的纯文字 TabButton 通过 icon.color 取得标签颜色，而默认值透明；允许将公开 icon.color 绑定 palette.buttonText，保留默认内容与背景。
- 导航侧边栏采用标准 ItemDelegate 并在所有窗口宽度下常驻显示（窄屏收缩为纯图标模式），通过 1 像素垂直分隔线与主内容区清晰隔离；选中的导航项呈现醒目的左侧主题色胶囊指示条（Pill Indicator）。
- 内容分组与设置面板统一以 WinUI 3 原生卡片体系为基准：SettingsRow 升级为独立设置条目卡片（SettingsCard），ActionCard、SettingsRow 统一对接 Ui.cardMinHeight（70px）与 Ui.cardPaddingX/Y（16px 内衬），拥有独立的圆角、半透明背景与微细描边，卡片间以 4px 微间距（Ui.cardGap）垂直排列；SettingsSection 作为轻量分组容器，提供外置分组标题与说明（card 为 false 时无外层大底板，仅在复合控制面板上显式设为 true 提供整体卡片底板）。
- 文字缓存、诊断和关于默认展示；仅手动应用 ID、运行详情等可选内容使用标准 checkable Button 控制显隐。Disclosure 不模拟 Expander 外观，收起内部焦点返回按钮，隐藏内容不接受操作。
- SettingsRow/ActionCard 共同构成全站规范的 Fluent 卡片项；SettingsSection 统一负责分组标题、说明与卡片流编排；PageFrame 负责页面标题、自适应可用宽度与统一滚动机制。保留公共布局组件避免页面重复代码，不以文件数量衡量自定义程度。

## 主题、材质和布局

- 系统/浅色/深色由 Qt styleHints 与官方 palette 管理；不维护第二套控件颜色或覆盖控件字体。Ui 只集中应用标题字阶、页面间距、图标路径和 Mica 窗口背景等语义。
- Mica 和沉浸式标题栏通过 QWindowKit 与 WindowBackdropController 协同管理，仅作用于主窗。主窗口布局遵循 WinUI 3 经典的分层材质对比：整窗作为单层 Mica 底板，左侧导航栏保持完全透明直接透出原生 Mica 并自顶部（y=0）一通到底，右侧主内容区叠加半透明基底色（Ui.contentAreaSurface）自顶部（y=0）一通到底，中间以 1px 细微描边分割，形成左轻右实、两边皆具 Mica 质感的分层视觉。通过 QWindowKit 原生挂接 Windows 11 Snap Layouts 贴靠菜单与窗口移动/最大化动画。全站卡片容器采用统一的 Ui.cardBackground（深色模式透光率 5%，浅色模式 70%）与 1px Ui.cardBorder，既保持与底层 Mica 或纯色底板的良好分层呼吸感，又确保高对比度模式下无障碍可读性。
- 页面最大宽度 1040、组间距 24、宽窗边距 32、窄窗 16。设置编辑器按宽度移至说明下方；长文本可换行。标准控件自身尺寸由官方风格决定。
- 所有设置继续即时生效与保存，编辑完成校验提交，保留错误、重试与恢复默认。弹幕覆盖层、时间轴和轨道逻辑不参与界面迁移。

## 下拉与滚动

- AppComboBox 在 ComboBox 基础上规范了 WinUI 3 风格的弹出浮层（8px 圆角、微细描边、深浅色浮层材质、4px 内嵌胶囊条 delegate），并附加标准 ScrollBar，限制尺寸、窗口内定位与当前项可见性，彻底解决 Qt 官方对 ComboBox 弹窗回退至旧式 Fusion 黑框的问题。
- 长列表最高 360×文本缩放，受窗口可用空间约束，距窗口边缘至少 8。保留 F4/Alt+Down、末尾当前项、缩窗和上下展开的回归检查。
- 页面和列表预留滚动条空间；保持官方默认 active/hover/policy，不强制常驻粗滑块。无溢出时判断滑块状态，不要求 ScrollBar 对象本身隐藏。
- Qt 官方纹理、Fusion 回退等可见差异记录为边界，不以新增自绘补丁消除。

## 验收与依赖

- 正式构建/部署不依赖第三方 FluentUI、其单例或 GraphicalEffects。现有静态 Qt SDK 的额外模块无需删除，但不能作为应用依赖宣传。库调研工具仅供历史复现，不作为正式构建前置步骤。
- Qt Test/CTest 验证配置、Mica 策略、导航、按钮折叠焦点、原播放/服务编辑/日志、长下拉滚动与键盘。软件纯色截图只用于布局。
- 三页浅深主题、520/800/1080 宽度、放大文字与 DPI、页面实际滚到底和弹出长列表应验证；Mica 使用桌面合成截图。Narrator、高对比度、跨屏、净机和性能需独立记录，未验收不得宣称完成。
