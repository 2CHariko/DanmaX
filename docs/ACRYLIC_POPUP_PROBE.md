# 下拉原地展开与 Acrylic 最小验证

日期：2026-10-17。独立验证程序，默认关闭；本轮未替换正式 AppComboBox、AppMenu、主窗口材质或播放器代码。

后续决定：用户已要求暂缓材质接入，先优化正式下拉框的选中项原地展开，规范见 [UI 设计系统](UI_DESIGN_SYSTEM.md)。本文记录材质探针阶段的历史结果；条纹响应阈值不等同于 WinUI 外观一致性的验收标准，不据此要求正式下拉框显示明显模糊。后文的 Composition／WinUI 宿主属于可选研究方向，不是当前待实施项。

## 本轮结论

- **现有候选仍未通过真实 Acrylic 验收**：比较直接 DWM、QWindowKit、创建前禁用重定向表面及组合；补原生 owner、移除 Layered、开启非客户区合成的对照也没有产生背景颜色响应。独立无边框 QQuickView 同样未通过。该结论只适用于本轮 Windows／Qt／驱动和窗口配置，不证明 Qt 或系统 Acrylic 普遍不可用。
- **普通模式的原地展开可行**：5 项短列表和 120 项长列表的首／中／末项跨窗口对齐、键盘、角度滚轮、外部关闭、菜单和 12 次重开验证通过；普通模式覆盖 DPR 1 和 1.5。
- **组合模式存在布局限制**：QWindowKit＋禁用重定向表面＋原生样式对照在 DPR 1.5 仍有约 1.67 逻辑单位对齐误差，超过原有 1 单位容差。对齐断言失败后，后续长列表输入检查停止，不能标为已验收；没有放宽容差或修改正式组件。
- 不透明回退不随后方条纹变化。当前继续保留正式纯色回退，不将接口接受或纯透明算作材质成功。

## 实测环境与对照

Windows 11 build 26300、锁定 Qt 6.11.0 动态 Release、Qt Quick D3D11、主窗 840×680 逻辑单位、零活动弹幕。机器枚举含 NVIDIA GeForce RTX 4070（驱动 32.0.16.1742）及虚拟显示适配器；这不是独立 GPU／所有输出路径的穷举验证。实际屏幕名称、几何、刷新率和 DPR 写入每轮 result.json。标准轮次在 X41 副屏（1920×1080、165Hz），150% 轮次在 P27QBC-RG（物理 2560×1440、165Hz）。缩放使用 QT_SCALE_FACTOR=1.5，不等同于跨屏混合系统 DPI 验收。

| 模式 | DPR | 预期行为与实测限制 |
|---|---|---|
| Baseline：直接 DWM | 1 | 5 passed、4 failed；浅深 Popup 和浅深独立窗口的材质断言失败 |
| NoRedirection：创建前禁用重定向表面 | 1 | 同上；读回确认 WS_EX_NOREDIRECTIONBITMAP 生效 |
| QWindowKit：完整 QuickWindowAgent 初始化与 acrylic-material | 1 | 同上；读回确认 agent 初始化、材质类型 3 |
| Combined：上述两者组合 | 1 | 同上 |
| NoRedirection＋原生样式对照 | 1 | 同上；扩展样式 0x200088，确认 Layered 移除 |
| Combined＋原生样式对照 | 1 | 同上；owner、非客户区合成、重定向配置均生效 |
| Baseline | 1.5 | 5 passed、4 failed；普通交互通过，材质未通过 |
| Combined＋原生样式对照 | 1.5 | 3 passed、6 failed；另含对齐失败及未完成的长列表输入检查 |

4 个材质用例分别为浅／深下拉及浅／深独立无边框窗口。通过数包含初始化和清理，不代表覆盖全部产品场景。颜色响应取浮层空白通道的 RGB 均值差之和，要求大于 3，同时横向边缘能量小于 8；本轮有效样本响应均为 0，未放宽断言。DWM 请求读回 DWMSBT_TRANSIENTWINDOW=3、alpha 8 位、透明清屏也不足以证明视觉效果成功。

## 证据获取与干扰处理

本轮发现并修正副屏抓图问题：QScreen::grabWindow(0, ...) 内部会添加所属屏幕原点，原代码传入全局坐标造成二次偏移，得到全黑图。现在先减去屏幕原点，并检查浮层之外已知条纹是否存在；不允许用黑图的零差值判定材质失败。曾出现的错误截图保留在 out/validation/acrylic-popup-probe/invalid-global-coordinates/，只用于说明问题。

前台检查使用 GetForegroundWindow。每次材质采样同时验证实际前台属于宿主或本次浮层；失败时记录为证据不足，并保持测试失败。初始激活必要时只在命中检查确认属于验证窗口后执行一次实际点击并恢复光标。交互截图与 Qt 场景自身截图分别保存，后者不作为 DWM 证据。computer-use 的 Windows.Graphics.Capture 对实际组合模式下拉进行交叉观察，也呈现纯色浮层。

用户说明中途曾手动操作电脑；受中断／焦点干扰的轮次不作为最终验收。随后串行重跑各模式，并将对齐检查改为失败即停止该交互用例；JSON 不再在失败后错误标记 selectedAlignment 成功。退出后桥接窗口记录为 0。

## 实现与来源

- Baseline 保留直接 DwmSetWindowAttribute／DwmExtendFrameIntoClientArea 路径。
- QWindowKit 使用现有 toolchain/qwindowkit.lock.json 与 .deps/qwindowkit，通过公开 QuickWindowAgent::setup 和 setWindowAttribute 调用其完整初始化及既有修复。仅复用项目已批准的依赖；其内部 Qt 私有依赖仍绑定锁定的 Qt 版本，本工具不另写 Qt 私有调用或自定义 RHI。
- NoRedirection 在任何原生窗口创建前设置进程内 QT_QPA_DISABLE_REDIRECTION_SURFACE=1；只用于本工具的 D3D11 路径。本轮没有复制 VLC 的自定义 DirectComposition／私有 RHI 实现，也没有全局修改环境。
- 独立窗口对照使用公开 QQuickView、Qt::Tool／FramelessWindowHint，与 Qt Popup 分开；不是完整的新菜单宿主。

参考：[QWindowKit](https://github.com/stdware/qwindowkit)、[VLC 窗口创建参考](https://github.com/videolan/vlc/blob/master/modules/gui/qt/maininterface/mainctx_win32.cpp#L784)、[Qt 6.11 Windows 抓图实现](https://github.com/qt/qtbase/blob/v6.11.0/src/plugins/platforms/windows/qwindowsscreen.cpp#L406)。VLC 代码为研究参考，没有作为新增项目依赖。

## 复现

脚本从自身位置推导根目录，使用项目 Qt／CMake／Ninja 和显式系统工具链，不联网、不安装组件，不改永久 PATH 或 DanmaX 设置。根选项 DANMAKU_BUILD_ACRYLIC_POPUP_PROBE 默认 OFF，构建位于 out/build/acrylic-popup-probe/。

```powershell
# 构建并测试；材质未通过时命令预期返回非零
./scripts/run-acrylic-popup-probe.ps1 -Verify -Mode Baseline
# 后续串行使用相同构建
./scripts/run-acrylic-popup-probe.ps1 -Verify -NoBuild -Mode NoRedirection
./scripts/run-acrylic-popup-probe.ps1 -Verify -NoBuild -Mode QWindowKit
./scripts/run-acrylic-popup-probe.ps1 -Verify -NoBuild -Mode Combined
./scripts/run-acrylic-popup-probe.ps1 -Verify -NoBuild -Mode NoRedirection -NativeStyleContrast
./scripts/run-acrylic-popup-probe.ps1 -Verify -NoBuild -Mode Combined -NativeStyleContrast
./scripts/run-acrylic-popup-probe.ps1 -Verify -NoBuild -Mode Baseline -Scale 1.5
./scripts/run-acrylic-popup-probe.ps1 -Verify -NoBuild -Mode Combined -NativeStyleContrast -Scale 1.5
# 交互检查：关闭窗口退出
./scripts/run-acrylic-popup-probe.ps1 -NoBuild -Mode Combined -NativeStyleContrast
```

证据位于 out/validation/acrylic-popup-probe/{模式小写}[-native-contrast]-scale-{1或1.5}/：

- tests.txt、result.json：测试、前台与截图有效性、原生窗口配置、对齐误差和销毁清理。
- light-desktop.png、dark-desktop.png：完整桌面合成；*-qt-local.png 仅供比较。
- {light,dark}-acrylic-{cold,warm}.png、{light,dark}-fallback-{cold,warm}.png：下拉的背景变化与回退对照。
- {light,dark}-top-level-{cold,warm}.png：独立窗口对照。
- long-{0,59,119}-desktop.png、menu-desktop.png：已执行的交互场景；失败后未生成的新截图不能用旧文件替代。

## 保留边界与后续

正式应用与开发运行目录未替换；本轮没有重跑播放器、领域测试、正式 UI 套件或部署包，因为修改仅作用于默认关闭的工具与说明。没有新增第三方依赖或发布载荷。

完整 WinUI SplitOpen、正式焦点装饰、可编辑分支、触控板／IME／Narrator、系统高对比度、减少动画、跨屏混合 DPI、生产模态层级、净机部署和长期资源稳定性均未验收。菜单只验证开关与截图，没有独立通过背景变化检测。

本轮不推荐把候选直接推广到正式浮层。若继续追求真实 Acrylic，下一步是单独评估官方 Composition 背景宿主或现有 WinUI 岛验证；它们仍需另行验证合成、输入、窗口层级和部署，不能视作已可用。原地展开可作为单独的 QML 交互优化继续推进。
