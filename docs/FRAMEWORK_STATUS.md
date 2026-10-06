# 框架交付状态

## 已实现

- 根目录为独立 C++20 / Qt Quick 项目；Python 源码、样本、图片、配置及历史方案归档。
- 30 个旧源文件和资源按 SHA-256 验证，内容未修改。旧虚拟环境和缓存保留但不提交 Git。
- 三个构建目标：纯 C++ `danmaku_core`、Qt/平台 `danmaku_runtime`、`danmaku_app`。
- Debug、Release、无 Qt 核心三个 CMake 预设。
- 项目内 Qt 6.11.0、CMake 3.31.6、Ninja 1.12.1；锁定 URL 和 SHA-256，支持离线重复准备。
- PowerShell 7 准备、诊断、构建、运行、打包和安全清理入口；进程环境退出后恢复。
- 系统 MSVC 14.44.35207 和 Windows SDK 10.0.26100.0 显式本地配置，不自动搜索其他项目。
- 简单控制面板和透明、无边框、置顶、输入穿透标志的预览窗口。
- 单条 Qt Quick 文字循环移动；统一 C++ 驱动，实际 delta，暂停不累计暂停时间。
- 运行数据、QML 缓存与 Qt Quick pipeline 缓存指定到本地目录，不使用 Qt 自动 pipeline 缓存位置。
- 中文 MSVC include-prefix 探测，修复该环境中 Ninja 头文件依赖识别的编码问题。

## 验证记录

- Debug / Release：构建成功；CTest 的时钟边界测试及双窗口加载、动画推进、QML warning 检查通过。
- windows-core：从干净目录构建并测试成功，无 Qt 依赖。
- bootstrap：已有工具时离线重复执行成功。
- package：windeployqt 生成开发部署目录；在本机去除 SDK PATH / QML 路径后，原生窗口 smoke 测试退出码为 0。
- 控制窗口截图检查完成，修复系统深色主题与浅色背景不匹配的问题。
- 归档校验、脚本语法和 Git 忽略规则检查通过。

Qt 配置会提示缺少可选 Vulkan headers；当前 Windows 默认图形路径不要求安装 Vulkan SDK，未因此新增依赖。

## 尚未实现或验证

- XML 加载、媒体会话发现、实时 SMTC 同步、轨道分配、对象池和批量文字渲染。
- 设置持久化、旧配置导入、日志界面和完整诊断指标。
- 真实鼠标穿透、焦点、独占全屏、多显示器/DPI、播放器行为和长时间压力测试。
- 无开发环境的干净 Windows 机器验证及公开分发材料。

当前单条 Text 仅验证窗口与时间链路，不是最终弹幕引擎，不提供吞吐量或 FPS 承诺。

项目内工具约 2.17 GiB，下载缓存约 0.26 GiB（本机文件逻辑大小）。MSVC/SDK 仍属已声明的系统安装例外；图形驱动/系统缓存不在项目完全控制范围内。
