# WinUI 3 XAML Islands 接入评估

日期：2026-10-16。范围：独立 C++20 / Qt 6.11.0 / Windows x64 原型；**不属于生产 UI 迁移或正式验收**。

## 结论

技术路线可行，已经在真实 Qt Quick 窗口中运行微软的 `Microsoft.UI.Xaml.Controls.ComboBox`，不是 QML 仿制。动态 Qt `/MD`、现有静态 Qt `/MT`、依赖已安装运行库及项目目录内自包含部署均已运行成功。

生产接入成本主要在两套 UI 的窗口层级、裁切、焦点、生命周期和发布管理。当前原型已经复现 QML 模态遮挡与滚动裁切问题，不能直接替换全部 AppComboBox。建议保留该原型用于后续决策，暂不扩大生产依赖。

若继续推进，优先比较“页内完整原生控件”和“只在展开时使用独立 WinUI 宿主”。后者有望缩小页内裁切问题，但需要验证浮层锚点、关闭语义与收起状态的一致性；目前只是候选方案，尚未实现。

## 实现与依赖边界

- 根 CMake 增加默认关闭的 `DANMAKU_BUILD_WINUI_ISLAND_PROBE`；源码位于 `tools/winui-island-probe/`。生产 QML、应用服务、覆盖层和时间轴未改动。
- `QQuickWindow::winId()` → `GetWindowIdFromWindow()` → `DesktopWindowXamlSource.Initialize()`。使用公开 Qt / Windows App SDK API，在 GUI 的 STA 线程创建和操作 XAML。
- 一个原生岛承载 3 个 ComboBox：3 项短列表、120 项长列表、3 项可编辑列表。Qt 页面含前后输入框、官方 Fluent ComboBox、滚动区域与两种 QML 对话框。
- 用 Qt 原生消息过滤器调用 `ContentPreTranslateMessage`，只在消息预处理阶段调用一次；焦点入口使用 `NavigateFocus`，出口使用 `TakeFocusRequested` 和宿主 HWND 的 `SetFocus`。
- 当前尺寸桥接将 QML 场景坐标乘 Qt DPR 后传给 `SiteBridge.MoveAndResize`。没有实现原生裁切、页面隐藏策略、完整混合 DPI 或通用模型适配。
- Windows App SDK 总体基线为 1.8.260921001；各拆分组件版本、来源规则、SHA-256、许可证和排除理由在 `toolchain/winui-island.lock.json`。
- 下载 7 个 NuGet 包，压缩缓存约 100.40 MiB，位于 `.cache/downloads/winui-island/`；校验后解压到 `.deps/winui-island/packages/`。C++/WinRT 从明确配置的系统 Windows SDK 元数据和锁定包生成投影，位置为 `.deps/winui-island/include/`。
- 不使用 NuGet restore、全局包缓存、.NET 运行时或 XAML 编译任务。控件用 C++ 创建，根容器用运行时 XAML 绑定官方主题背景资源。常规配置与构建不联网。
- MSVC / Windows SDK 使用 `.local/toolchain.local.json` 已有的显式外部工具链例外。脚本报告真实路径，TEMP/TMP 与 QML 缓存留在项目，原型关闭 Qt 自动着色器磁盘缓存；没有安装系统组件或修改永久 PATH。Windows/驱动缓存仍属系统边界，WinUI 内部缓存写入范围未做完整审计。

## 已验证与未通过的场景

| 场景 | 结果与边界 |
|---|---|
| 构建 | 动态 Qt Release 与静态 Qt Release `/MT` 均成功 |
| 实际渲染 | 真实原生短列表、120 项长列表、可编辑框显示；浅深主题及原生浮层已目视检查 |
| 鼠标与滚轮 | 实际鼠标打开长列表、滚轮滚动、选择 112；Qt 标签接收到原生选择事件 |
| 末尾当前项 | 当前项 119 打开后保持可见；浮层位置由 WinUI 管理 |
| 数据往返 | 原生选择 → Qt 属性与信号；Qt 按钮/程序 → 原生 SelectedIndex 均通过 |
| 键盘 | 从 Qt 前输入框 Tab 进入短列表，Tab 进入长列表、可编辑框，再 Tab 返回 Qt 后输入框；F4 展开、Escape 收起、Up 改变长列表选择均通过 |
| 反向焦点 | Shift+Tab 从 Qt 后输入框进入原生末项已通过；完整反向循环未验收 |
| 编辑提交 | 注入 Unicode 文本“测试字体 Test”，Enter 触发原生 TextSubmitted；不等于真实拼音输入法组合态验收 |
| 缩窗 | 缩到 560×460 后原生控件宽度随 Qt 场景更新；底部裁切问题仍存在 |
| 生命周期 | 程序检查中每次重建岛 10 次；真实标题栏关闭与程序 close 路径均已成功退出。必须先关闭岛，再让 Qt 销毁父 HWND |
| QML `Popup.Item` 模态 | **未通过**：原生子窗口覆盖对话框及遮罩；原生焦点持有时 Escape 未能关闭该对话框 |
| QML `Popup.Window` | 独立窗口可以显示在岛之上，Escape 关闭已观察到；该原型中的浮层背景、布局与窗口装饰表现异常，未作为完整替代方案验收 |
| QML 滚动裁切 | **未通过**：原生 HWND 跟随坐标移动，但不遵守 Flickable 的 `clip`，可覆盖顶部按钮及留白 |
| 可访问性 | UI Automation 能枚举原生 ComboBox 和文本；跨 Qt / WinUI 的焦点描述曾出现滞后，未做 Narrator 验收 |
| 缩放 | `QT_SCALE_FACTOR=2` 下几何 DPR=2、程序检查通过；它只改变 Qt 缩放，不代表 WinUI 字体同步、真实 DPI 或跨屏验证 |

未验收：滚动条逐区域命中与拖动、真实触控板惯性、完整键盘矩阵、真实 IME 组合、高对比度、系统文字放大、减少动画联动、多显示器混合 DPI、主面板/工具栏/弹幕的真实层级、媒体运行时场景、长时间稳定性及净机部署。

## 部署结果与体积

两种部署模式显式选择，不相互静默回退：

1. **Framework-dependent**：调用 `MddBootstrapInitialize2`，使用已安装的稳定版 Windows App Runtime 1.8 x64。本机实际加载版本为 8000.994.2142.0；原型不安装、不修复运行库。
2. **Self-contained**：从独立部署目录显式加载 `Microsoft.WindowsAppRuntime.dll`，调用 `WindowsAppRuntime_EnsureIsLoaded`。不调用 bootstrap；事件报告确认 XAML、Controls、Windowing、AppRuntime 四个关键 DLL 都来自该部署目录。

自包含准备按照包内 `Microsoft.WindowsAppSDK.SelfContained.targets` 的规则处理：复制对应架构原生文件与元数据；从锁定包的 `package.appxfragment` 生成 WinRT 激活及 COM 代理清单，用显式系统 SDK 的 `mt.exe` 合入评估 EXE；使用 `makepri.exe` 合并三个原生 PRI 索引。单独复制 DLL 会出现“没有注册类”，缺少合并资源索引会出现无法定位 `themeresources.xaml`，本轮均已定位并修正。

| 静态 Qt 评估目录 | 文件数 | 未压缩体积 |
|---|---:|---:|
| 依赖已安装运行库 | 15 | 约 32.31 MiB |
| 自包含原生载荷 | 245 | 约 86.21 MiB |
| 自包含相对增量 | 230 | 约 53.90 MiB |

以上是**该原型**的实测目录，不是完整 DanmaX 发布包体积。包含多语言和未裁剪原生载荷、评估许可证材料；没有证明这些文件是最小集合。静态 Qt EXE 可以继续 `/MT`，但 WinUI 的 DLL / PRI / 元数据仍需部署，当前“单 EXE、不释放外部运行库文件”的发布形态不能直接沿用。正式发布还需要整合现有 Qt 分发与重链接材料流程；本目录仅供内部评估。

四种组合（动态/静态 Qt × framework/self-contained）的独立部署目录程序检查均通过。运行时移除了开发 SDK 的 PATH、插件及 QML 导入路径。自包含关键 DLL 的本地加载证据不等于在没有 Windows App Runtime、开发工具与 VC 运行库的净机上验证。

## 运行开销初步记录

环境：Windows 11 build 26300；Ryzen 5 5600、约 64 GiB 内存；显卡库存 RTX 4070（驱动 32.0.16.1742，活动显示 1920×1080 / 165 Hz，另有虚拟显示驱动）。Qt 6.11.0 静态 Release，Qt Quick 默认 D3D11 与 WinUI 合成器，920×760 逻辑窗口、DPR=1，零活动弹幕。没有独立核对实际 RHI 适配器 LUID。

`measure-winui-island.ps1` 顺序运行同一 Qt 页面，分别不初始化 WinUI、使用已安装运行库、使用目录内运行库，每种 3 个短样本。内存在页面加载后 800ms 采样，随后做重建检查并退出。

| 模式 | 私有提交量 | 工作集 |
|---|---:|---:|
| 同页 Qt-only | 约 178–181 MiB | 约 139–140 MiB |
| 加入一个岛/三个原生 ComboBox，framework | 约 222–225 MiB | 约 194 MiB |
| 加入一个岛/三个原生 ComboBox，self-contained | 约 223–225 MiB | 约 194–195 MiB |

按同轮样本比较，本机进程私有提交增量约 42–46 MiB、工作集约 54–56 MiB。它包含 WinUI 框架初始化与额外控件，不是每个 ComboBox 的固定开销，不能按选项或控件数线性外推。日志保留加载阶段耗时，但 Qt-only 与先初始化 WinUI 的图形资源初始化顺序不同，不能将两者相减宣称启动加速或确定启动延迟。未测冷启动、稳态 CPU/GPU、显存、帧时间、功耗和媒体播放干扰，这些数值不构成性能验收。

## 生产接入工作量估算

以下为一位熟悉现有项目的工程人员的估算，允许任务交叉；不是已投入时间或完成承诺。

| 工作 | 估算 |
|---|---:|
| 通用 QML/C++ 模型、角色、选择与编辑提交接口，字体预览等既有功能 | 1–3 人日 |
| 视口裁切、隐藏/销毁、锚点、滚动、DPI 与窗口变更同步 | 2–4 人日 |
| 跨框架焦点、快捷键、模态、主面板与覆盖层层级整合 | 2–4 人日 |
| 依赖准备、CMake、部署、许可证及错误诊断整合 | 1–2 人日 |
| 真实 IME、触控板、混合 DPI、Narrator/高对比度与净机回归 | 2–4 人日 |
| 合计参考 | **8–17 人日，约 2–4 周** |

尤其不能假定设置页的十余个下拉框只需要复制本原型的一段代码：它们位于滚动页、弹窗及复用组件中，而本轮发现的问题恰好跨越这些容器边界。未来还需同步 Qt 与 Windows App SDK 两套版本的行为变化。

建议的下一道决策门槛：在一个独立验证页中完成真实滚动裁切、QML 模态及 IME/混合 DPI，不增加生产页面替换范围。仅为原生下拉外观是否承担约 54 MiB 未裁剪部署增量、另一套 UI 生命周期与上述维护工作，需要按产品取舍决定。

## 复现与证据

以下命令可从任意目录通过脚本绝对路径调用；此处以仓库根的相对写法展示：

```powershell
# 独立、显式联网准备；已有缓存时加 -Offline
./scripts/prepare-winui-island.ps1
./scripts/probe-winui-island.ps1 -Mode Build
./scripts/probe-winui-island.ps1 -Mode Build -StaticQt

# 静态 Qt，自包含原生运行库
./scripts/probe-winui-island.ps1 -Mode Stage -StaticQt -SelfContained
./scripts/probe-winui-island.ps1 -Mode Smoke -StaticQt -SelfContained -Staged
./scripts/probe-winui-island.ps1 -Mode Run -StaticQt -SelfContained -Staged

# 已安装运行库路线、Qt-only 基线及重复采样
./scripts/probe-winui-island.ps1 -Mode Stage -StaticQt
./scripts/measure-winui-island.ps1 -Samples 3
```

证据均在被忽略的 `out/validation/winui-island/`：`environment.json`、`measurements.json`、各变体的 `events.jsonl`、`deployment-size.json`、生成的清单与 MakePRI 日志，以及 `screenshots/` 的原生浅深弹层、模态遮挡与裁切截图。真实路径位于本机报告，不写入受版本控制的工具配置。

另外，默认关闭原型的 `windows-core` 构建与 CTest **4/4** 通过；没有运行全量生产 Qt 测试或重新发布主程序。

## 官方参考

- [WinUI XAML Islands 宿主模型](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/host-controls-existing-desktop-apps)
- [微软 C++ Win32 示例（固定参考提交）](https://github.com/microsoft/WindowsAppSDK-Samples/tree/e179f2da414e0042260c8553a4ee3d6bf54a3258/Samples/Islands/cpp-win32-unpackaged)
- [Windows App SDK 自包含部署](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/self-contained-deploy/deploy-self-contained-apps)
- [Qt 外部 UI 集成](https://doc.qt.io/qt-6.11/mixing-qt-with-other-ui-toolkits.html)
- [Qt WindowContainer 的原生窗口层级限制](https://doc.qt.io/qt-6.11/qml-qtquick-windowcontainer.html)
- [Qt FluentWinUI3 明确为非原生风格](https://doc.qt.io/qt-6.11/qtquickcontrols-fluentwinui3.html)
