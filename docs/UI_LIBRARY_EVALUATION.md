# Qt/QML Fluent 控件库验证（2026-10-08）

当前状态：用户随后授权迁回 Qt 官方 FluentWinUI3，采用默认控件和简单布局。本文保留历史选型过程，不再作为生产依赖或实施指令；当前规范见 UI_DESIGN_SYSTEM.md。

## 历史结论

**后续决策：用户已选择 QML FluentUI。下述两库比较保留为首轮记录，后续以本文“QML FluentUI 第二轮”结果为准，不再推荐改选 RinUI。**

本轮在项目的 Qt 6.11.0 / MSVC x64 Release 环境真实构建并运行了两个独立样例，未替换生产 UI，也未改变正式依赖清单。

**RinUI 的现成 SettingCard、SettingExpander、下拉菜单更接近用户的 Windows 11 设置参考图，适合作为后续候选，但不能直接验收接入。QML FluentUI 的 C++ 构建和基本键盘选择可用，默认视觉与依赖组成不足以支持直接迁移。**

“能运行”与“可替换正式 UI”是不同结论。两个库都不是微软 WinUI 运行时，不能保证像素、可访问性与系统行为一致。

## 版本、隔离与复现

- FluentUI：`7e33a2f672d18239ef49ab960075b1137a1d18e7`，未修改上游控件源码。
- RinUI：`36ad74c888a39c86568b1b8b566103f216c3aff5`，未修改上游控件源码。
- 源码 URL、SHA-256、许可线索、Qt 扩展模块锁定于 `tools/ui-library-probe/dependencies.lock.json`。
- 下载/解压在 `.cache/ui-library-probe/`；额外 Qt 6.11.0 模块在 `.deps/ui-library-probe/qt-extra/`；构建在 `out/build/ui-library-probe-release/`；报告在 `out/validation/ui-library-probe/`。
- 系统 MSVC/SDK 使用 `.local/toolchain.local.json` 的显式配置；真实路径由脚本输出。没有修改永久 PATH、正式 Qt SDK 或用户配置。
- 额外准备 Qt5Compat 与 ShaderTools，以满足第三方库的图形效果路径。未给正式静态 Qt 构建增加模块。
- 样例保留原生标题栏，使用不透明背景，专门比较控件；本轮未评价第三方库 Mica 实现。

在项目根目录执行；脚本内部路径从自身位置推导，也可从其他工作目录使用绝对脚本路径：

```powershell
./scripts/prepare-ui-library-probe.ps1       # 独立联网准备步骤；校验后解压
./scripts/probe-ui-libraries.ps1 -Mode Build # 离线配置、构建
./scripts/probe-ui-libraries.ps1 -Library Rin -Theme Dark
./scripts/probe-ui-libraries.ps1 -Library Fluent -Theme Light -Scale 2
./scripts/probe-ui-libraries.ps1 -Library Rin -Theme Dark -Interactive
```

`-Offline` 可用于准备脚本校验已有缓存。交互模式需要手动关闭样例窗口。程序退出码 0 表示样例执行完成，不表示所有交互检查通过；实际结果见各目录的 `results.json`。

## 实测结果

| 项目 | QML FluentUI | RinUI |
|---|---|---|
| Qt 6.11.0 接入 | 上游 CMake 动态插件构建成功，约 5.22 MiB DLL（不是完整部署大小） | QML 直接加载成功；使用最小 C++ ThemeManager 桥接，没有引入 Python |
| 设置卡片 | 采用库的 FluFrame + 标准布局；边框仍明显，没有直接同等的 SettingCard 组合 | 现成 SettingCard，图标、标题/说明、右侧控件；边框和分层更接近参考 |
| 折叠 | FluExpander 可展开 | SettingExpander + SettingItem 可展开，等待动画稳定后两行内容完整 |
| 长下拉列表（40 项，当前项末尾） | 当前项可见，但默认弹出高度接近整窗；列表仅配置 ScrollIndicator，不是可拖动 ScrollBar | 当前项可见；默认上限 300，具有自适应位置和选中标记；仍需核验滚动条拖动 |
| Space 展开 → Up → Enter | 索引 39 → 38，关闭 popup | 索引仍为 39，popup 未关闭；方向键焦点已移动但 Enter 未提交 |
| 真实桌面交互 | 观察了深色页面、短菜单打开/关闭 | 实际复现长菜单方向键/Enter 问题；Escape 可关闭；短菜单鼠标选择正常；滚轮和拖动页面滚动条均能到设置项目 12 |
| 折叠键盘（Qt Test） | Tab 可到达折叠按钮，Space 可切换 | Tab 可到达折叠标题，Space 可切换 |
| 页面滚动条拖动（Qt Test） | contentY 从 0 到 741，达到底部 | contentY 从 0 到约 739，达到底部 |
| 菜单打开时缩窗（900×700 → 520×500） | 内容仍在窗口内，popup 仍偏高 | 菜单偏移到窗口左侧并被裁切；边界检测失败，截图可见 |
| 运行日志 | 最终样例本轮未输出 QML 警告 | enabled 覆盖、重复颜色 interceptor、SettingExpander RowLayout recursive rearrange 警告 |

自动化采用 Qt Test 事件输入和 Qt Quick 抓图。最终等待时间覆盖上游 667 ms 菜单及 333 ms 折叠动画，避免把过渡帧误判为最终布局。覆盖以下 8 组运行，每组保存页面、popup、展开、底部、窄窗口五张截图：

- 每库浅色 100%、深色 100%、深色 150%、浅色 200%。
- 初始 900×700 逻辑尺寸，最后缩小至 520×600。
- Qt 环境变量缩放，不等同于真实跨显示器 DPI / Windows 放大文字验收。
- 首轮自动化展开折叠通过属性完成，滚动到底通过 contentY 完成。补充轮使用 Qt Test 的 Tab/Space 和鼠标拖动事件验证操作路径，新增 `expanderReachableByTab`、`expanderSpaceToggles`、`contentYAfterScrollbarDrag` 字段；未验证折叠焦点恢复。
- 补充轮在浅深色 100% 下保存 `popup-resized.png`，共增加四张截图；150%/200% 首轮结果保持原验收范围。截图几何结果反映真实 Qt Quick 窗口 resize，不是图片缩放。

## 依赖及许可发现

### FluentUI

根目录 License 为 MIT，但默认 `src/CMakeLists.txt` 递归纳入 C++ 源码，同时链接 Widgets 和 PrintSupport。其中 `src/qmlcustomplot/qcustomplot.h` 文件头明确标注 GPLv3 或更高版本。**此前仅按根许可证归类为 MIT 不完整。** 这是一项源码和默认构建组成发现，不代表已完成整库许可审计。

此外 `FluScrollBar.qml` 等引用 `QtQuick.Controls.impl` 内部模块。正式采用前需审查传递代码许可、裁剪可行性与 Qt 升级风险；本次没有替上游维护裁剪分支。

- [锁定版本构建文件](https://github.com/zhuzichu520/FluentUI/blob/7e33a2f672d18239ef49ab960075b1137a1d18e7/src/CMakeLists.txt)
- [锁定版本 QCustomPlot 文件头](https://github.com/zhuzichu520/FluentUI/blob/7e33a2f672d18239ef49ab960075b1137a1d18e7/src/qmlcustomplot/qcustomplot.h)
- [锁定版本滚动条](https://github.com/zhuzichu520/FluentUI/blob/7e33a2f672d18239ef49ab960075b1137a1d18e7/src/Qt6/imports/FluentUI/Controls/FluScrollBar.qml)

### RinUI

根目录许可证 MIT，作者 README 明确称仍在开发、不适合生产使用。QML 可以在 C++ 宿主中运行；当前主题接口期望 ThemeManager 对象，样例提供主题名称、强调色及信号的最小实现。未实现系统主题/高对比度/减少动画桥接，不能把这个样例当作完整迁移层。

- [锁定版本 README](https://github.com/RinLit-233-shiroko/Rin-UI/blob/36ad74c888a39c86568b1b8b566103f216c3aff5/README.md)
- [锁定版本主题接口](https://github.com/RinLit-233-shiroko/Rin-UI/blob/36ad74c888a39c86568b1b8b566103f216c3aff5/RinUI/themes/theme.qml)
- [锁定版本下拉菜单](https://github.com/RinLit-233-shiroko/Rin-UI/blob/36ad74c888a39c86568b1b8b566103f216c3aff5/RinUI/components/ContextMenu.qml)

## 未验收与决策建议

未验收：完整 Tab 流程、折叠焦点恢复、Narrator、高对比度、减少动画、系统主题动态切换、弹出层上下边界全矩阵及 popup 内滚动条拖动、长中文和 Windows 文本放大、Mica 结合、正式静态部署、GPU/CPU 性能。

建议保持正式 UI 不变。如果继续选型，优先对 RinUI 做有限的阻塞问题验证（Enter 提交、缩窗越界、布局警告、可访问性与主题桥接），先评估修复是否能由上游接受以及后续维护范围。只有这些问题解决、且视觉收益得到确认后，才制定正式迁移。若要求真正使用原生 WinUI 控件，则另行评估 WinUI 3 控制面板路线，不能把换一个 QML 风格库视为等价实现。

## QML FluentUI 第二轮：精简构建与适配验证

用户明确选择 QML FluentUI 后，验证继续聚焦该库。本轮不迁移正式三页，验证目录允许使用选定第三方库；原项目对生产 UI 的约束尚未被静默改写。

### 精简构建

`tools/ui-library-probe/slim-fluent.cmake` 使用显式文件清单，编译 6 个原库 C++ 实现及其头文件、枚举、21 个原库 QML 控件和原库图标字体。未修改上游源文件。排除 QCustomPlot、二维码、全局热键、无边框窗口、表格/树模型等无关组件；不再链接 Widgets、PrintSupport。

- 库主体 `fluent_probe.dll` 实测 1,206,272 字节；插件 17,408 字节。对照整库插件 5,469,696 字节。不是完整包大小，也不是性能测量。
- `fluent-source-manifest.txt` 记录真实目标源文件；`fluent-dependencies.txt` 为 dumpbin 导入检查。Qt Quick/Gui/Qml/Core 和 VC 运行库等依赖保留。
- 已证明本子集不编译 QCustomPlot；**不把这等同于整库/字体/Qt 组件的完整分发许可审计**。
- 部分原库 QML 仍引用 `QtQuick.Controls.impl`，字体剪裁仍使用 Qt5Compat 图形效果；需要锁定 Qt 版本和维护兼容性验证。
- 原库 FluTheme 自带 1 秒计时器和主题处理，不能宣称精简后零轮询/零开销。Mica 继续应由项目平台控制器负责，尚未在此样例组合验收。

### 公开接口适配

- `ProbeComboBox.qml` 继承 FluComboBox：保留原 popup、ListView、背景与原 FluItemDelegate 类型，仅增加窗口边界/360 高度上限和原库 FluScrollBar；选项预留滚动条空间。没有重画下拉主体、菜单背景或滑块。
- `ProbeExpander.qml` 继承 FluExpander：原库收起内容时仍可能让内部复选框保留活动焦点，实测 `collapsedContentRetainsFocus=true`。适配层通过公开 focus/Accessible/Keys 返回焦点到标题，并使用库内 FluFocusRectangle；修复后内部焦点不再残留。
- 运行中主题切换使用 FluTheme，放大文字使用公开 FluTextStyle.Body。13→21 像素是应用字阶适配测试，不代表 Windows 文本缩放联动已实现。

### 检查项

Qt Test 驱动真实 Qt Quick 窗口，自动报告包含以下结果；适配样例对关键字段断言，失败退出码为 5，不再以“进程运行完成”代替通过：

1. Space 打开，Up/Enter 从末项 40 选择 39 并关闭菜单。
2. 360 高度限制、靠下向上/靠上向下展开、菜单保持打开时缩到 520×500 均在窗口内。
3. popup 滑块向上拖动，contentY 从约 993 降至 0；滚轮下滚后增至 72（DPR 2 的初始列表尺寸有所不同）。
4. 页面滚动条拖到底；Tab/Space 展开折叠；收起后焦点返回标题。
5. 运行时浅深主题互换，Body 字阶放大到 21 像素，窄窗截图检查。

验证命令：

```powershell
./scripts/probe-ui-libraries.ps1 -Mode Build -Slim
./scripts/probe-ui-libraries.ps1 -Library FluentAdapted -Theme Light -Slim
./scripts/probe-ui-libraries.ps1 -Library FluentAdapted -Theme Dark -Scale 1.5 -Slim
./scripts/probe-ui-libraries.ps1 -Library FluentAdapted -Theme Light -Scale 2 -Slim
./scripts/deploy-fluent-probe.ps1
```

动态部署脚本使用锁定 Qt 的 windeployqt，额外模块通过项目内临时 SDK 视图供工具解析，未修改正式 SDK。部署后清除 SDK PATH/QML_IMPORT_PATH/QT_PLUGIN_PATH，以包内路径运行浅深色检查。部署仍依赖已安装的 Microsoft VC++ x64 运行库；不是静态单 EXE，也不是已完成公开分发材料的正式发布包。

最新成功部署路径记录于 `out/validation/ui-library-probe/latest-deployment.txt`。每个包内有 verification-Light/Dark 报告；开发态报告位于 `FluentAdapted-slim-*`。脚本提供 `preview.ps1` 供本机打开交互样例。

### 下一阶段边界

QML FluentUI 可以继续作为正式迁移的基础；已证明所需控件子集能够运行和部署，基础交互问题可由小型适配层处理。下一阶段再接入真实业务三页、现有 Mica 控制器、系统主题/高对比度/减少动画桥接，以及原配置/播放/日志回归。本轮未验收 Narrator、跨显示器 DPI、完整 Windows 文本缩放联动、高对比度、Mica 合成、GPU/CPU 开销、静态发布。长文本选项省略、编辑型字体选择器及折叠内部原按钮的读屏命名仍需正式接入时补齐。
