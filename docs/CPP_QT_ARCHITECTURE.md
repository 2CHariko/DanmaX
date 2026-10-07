# C++ / Qt 架构与项目内开发环境

状态：0.2 实现基线。XML、SMTC、时间轴/轨道、公开场景图文字节点、设置及 Fluent 控制面板已落地，验证范围见 `FRAMEWORK_STATUS.md`。

本方案取代 Flutter 迁移方向。旧 Python 版本已归档至 `archive/python-qt/`，Flutter 文档已归档至 `archive/plans/`，内容保留。目标是高效的 Windows 弹幕覆盖层，同时控制依赖数量和构建复杂度。

## 1. 决策

| 项目 | 选择 | 原因 |
|---|---|---|
| 语言 | C++20 | 使用标准库模型、RAII 和明确所有权；兼容现有 MSVC 工具链 |
| UI | Qt 6 Quick / QML / 官方 FluentWinUI3 | 控制面板使用统一 Fluent 规范；弹幕内容独立渲染 |
| 渲染 | Qt Quick 公开场景图能力 | 优先复用文字布局、GPU 后端和线程基础设施 |
| 平台 | Windows x64 首发 | 验证 SMTC、窗口穿透和多显示器；核心保留可移植性 |
| 构建 | CMake + Ninja + MSVC | 单一构建路线，不同时维护 qmake、MinGW 和多套生成器 |
| 测试 | CTest + Qt Test，核心可用普通 C++ 测试程序 | 无额外测试框架依赖 |
| 依赖管理 | 版本清单 + PowerShell 准备脚本 | 首版不引入 vcpkg、Conan 或运行时包管理器 |
| 分发 | 用户授权的静态 Qt 单 EXE；保留动态部署目录 | 不增加安装器框架、更新服务或 MSIX |

不设定“换框架即提速”的承诺。先验证覆盖层合成和文字负载，再扩展完整控制面板。

UI 的具体约束以 [UI_DESIGN_SYSTEM.md](UI_DESIGN_SYSTEM.md) 为准：统一编译期 FluentWinUI3，少量导航与设置组合，不增加第三方 Fluent 依赖；官方未覆盖控件的 Fusion 回退要记录和验证。当前页面已迁移；完整无障碍验收仍未完成。

## 2. 目录规划

以下为架构目录；当前实现状态以根 README 和源码为准，尚未使用的资源目录不预建空文件。

```text
项目根/
├── AGENTS.md
├── CMakeLists.txt                  # 根构建入口
├── CMakePresets.json               # 共享配置，不写本机绝对路径
├── CMakeUserPresets.json           # 可选本地覆盖，不提交
├── cmake/                         # Qt、编译器校验与部署辅助
├── scripts/
│   ├── bootstrap.ps1               # 显式下载与准备，独立于构建
│   ├── doctor.ps1                  # 只读诊断，列出所有实际工具路径
│   ├── build.ps1                   # 配置、编译、测试的统一入口
│   ├── run.ps1                     # 设置项目内运行目录和缓存
│   ├── package.ps1                 # 收集 Release 部署依赖
│   └── clean.ps1                   # 仅清理校验过的产物目录
├── toolchain/
│   ├── dependencies.lock.json      # 精确版本、URL、哈希与许可证
│   └── local.example.json          # 外部系统工具例外的格式示例
├── src/
│   ├── core/                      # 纯 C++：模型、时间轴、轨道和生命周期
│   ├── application/               # 会话编排、设置应用与状态输出
│   ├── infrastructure/            # XML、配置、日志、任务执行
│   ├── platform/windows/          # SMTC、前台窗口、覆盖窗策略
│   ├── renderer/                  # Qt Quick 项、场景图、文字资源
│   └── app/                       # main、QML 注册和对象装配
├── qml/                           # 控制面板、设置、会话列表、调试 UI
├── resources/                     # 图标、翻译等受版本控制的资源
├── tests/                         # 单元、集成、渲染与性能入口
├── docs/
├── .tools/                        # 忽略：qt/<版本>/msvc2022_64、cmake、ninja
├── .deps/                         # 忽略：确有需要的额外依赖
├── .cache/                        # 忽略：downloads、tmp、qml 等
├── .local/                        # 忽略：开发配置、日志、toolchain.local.json
├── out/                           # 忽略：build、test-results、reports、stage、packages
└── archive/                       # Python 实现、样本和历史方案，不参与构建
```

不把 `src/core` 拆成大量微型库。建议初始构建目标：`danmaku_core`（纯 C++ 静态库）、`danmaku_runtime`（Qt/平台服务）、`danmaku_app`（QML 与渲染）；测试按职责建立可执行目标即可。这些目标已建立并承载当前业务。

## 3. 模块与数据流

```text
QML UI → Application Service → Core
                  ↓             ↑
             XML / Config    媒体快照、帧时间
                  ↓             ↑
            后台任务执行    Windows Media Adapter
                                ↓
                  调度结果 → Renderer → Qt Quick Scene Graph
```

### 领域核心

- `DanmakuItem`：媒体时间、模式、RGB/ARGB 整数、文本、源记录索引。实现中时间为 double 秒，跨平台边界使用 chrono 转换；接口和测试固定单位。
- `TimelineEngine`：排序索引、游标、跳转与切换语义；按区间产生批次，不逐条跨线程发消息。
- `TrackAllocator`：按真实文字尺寸、速度、轨道边界分配；明确三种弹幕是否共享可视空间。
- `ActiveSet`：预留存储、活动状态、剩余寿命、回收和丢弃统计。
- 核心只接收文字测量结果和可视区域，不调用字体、文件、窗口或系统媒体 API。
- XML 源字号保存在标准库模型中，25 对应用户设置的基准字号，其他字号按比例缩放；渲染层回传宽高，核心按实际占位高度检查相交轨道。布局/纹理缓存必须区分字体和字号。
- 轨道优先选择对应边缘起的首条安全轨道，滚动尾部进入可视区并留足间距后可复用；允许重叠只作为安全轨道满时的回退，跳转重置其分配顺序。

### 基础设施与应用服务

- XML 通过后台 `QXmlStreamReader` 解析，限制异常文件、超长文本和非有限时间；按批次报告进度。完成后一次性交接不可变数据。
- 配置采用带详细中文注释的 UTF-8 INI（settings.ini），版本校验和 Qt `QSaveFile` 原子保存；首次运行生成完整默认配置。旧 JSON/INI 不读取、不迁移；程序保存时重建自带注释，不保留未知字段或额外注释。
- 日志使用 Qt 消息处理能力，提供有界 UI 队列和文件轮转，避免渲染线程同步写盘。
- 应用服务统一管理 `Idle / Loading / WaitingForSession / Playing / Paused / Error`，每个状态有可解释的 UI 输出。
- 配置分为即时属性、文字缓存失效、窗口重新配置和文件重载，不统一“停止后延时重启”。
- 设置更新通过保留状态的核心 `reconfigure()`，不调用时间轴跳转。保留投放游标、活动 ID、滚动横坐标及已显示动画时间；字号/轨道/视口变化局部重排，缩容移除超额较新对象，放不下的对象单独退场并计入 `retiredBySettings`。同一 GUI 事件循环批次合并连续修改，暂停时也更新快照；真实跳转和文件加载继续清空。
- 设置 UI 默认即时生效并保存；提交时机、校验、持久化失败和需重启例外由应用服务明确反馈，不提供统一“应用全部”按钮。

### Windows 适配

- 用 Windows SDK 提供的 C++/WinRT 访问 SMTC；以编译探针验证实际 SDK 头文件和接口，不预先增加 NuGet 依赖。
- 枚举并选择目标会话，不依赖系统 `current session` 恰好是目标播放器。
- 快照包含会话身份、媒体身份线索、播放状态、位置、时长、可用播放速率、单调采样时间；无法准确识别媒体切换时明确降级策略。
- AUMID、进程名、PID 和 HWND 分开建模；建立最佳可用关联并允许明确选择，不能假定它们字符串相等。
- 覆盖窗的显示器、可视区域、焦点、穿透和置顶由适配层管理。首版提供明确的显示器选择；跟随播放器窗口须另行验证识别和边界行为。
- 订阅句柄、WinRT apartment、窗口钩子和线程有 RAII 生命周期；停止时合作取消并等待，不使用强制 terminate。

## 4. 渲染和时间规则

已实现 `QQuickItem` 和公开 `QSGTextNode`，使用 `QTextLayout` 缓存布局；节点按弹幕生命周期创建/回收，逐帧只更新坐标。若使用文字位图纹理，必须测量纹理上传成本、缓存容量与 DPI 清晰度。不提前承诺一次绘制调用或跨不同字体/纹理无条件合批。

- 单一帧节奏，以真实 delta 推进状态，不假定定时器恰好 60 Hz。
- 媒体时间决定投放，动画时间决定移动和寿命。首版定义媒体速率变化时动画是否跟随；推荐跟随已知速率，未知时按 1 倍并显示能力限制。
- 暂停同时冻结位置、寿命、轨道释放条件；播放恢复不补偿暂停期间的墙钟时长。
- 小幅倒退、跳转、会话切换不能只靠绝对差大于 2 秒判断。结合预测媒体位置、状态及容差处理抖动；跳转重置所有相关状态。
- 默认跳转策略为清空在屏弹幕并定位到新时间开始投放，不追补整个历史区间；若未来支持重建仍应可见的弹幕，需独立定义算法和测试。
- 隐藏/未暴露时停止持续绘制，使用低频维护推进媒体跟踪、已有弹幕的位置/寿命及轨道回收；暂停仍冻结。新弹幕只推进游标，不测量投放，以 `suppressedWhileHidden` 单独统计。恢复可见时刷新仍有效的活动对象并保留 ID，不通过跳转清空，也不批量补发隐藏期间记录；真实跳转/媒体切换仍重置。
- GUI 线程拥有应用/核心可变状态；Qt Quick 同步阶段向渲染侧交接批次。场景图节点和 GPU 资源只在规定的渲染阶段创建、修改、释放，禁止后台线程访问。
- 文字缓存键包含文本、字体回退相关设置、字号、颜色/描边策略、DPR；缓存设字节预算与回收规则。Qt 自身缓存与项目缓存分开统计。
- 轨道释放依据尾部位置和最小间距；不能用固定 0.8 系数提前释放。不同速度时还需防追尾；固定弹幕与滚动弹幕交叉避让规则必须可测试。

## 5. “全部放在项目目录”的实施边界

### 可严格控制的部分

Qt SDK、CMake、Ninja、下载文件、可选依赖、项目临时文件、QML 磁盘缓存、构建中间文件、测试结果、打包暂存和开发日志全部放入上述项目内目录。任何后续引入的工具都必须先确认缓存路径可配置。

脚本在进程环境中设置 `TEMP`、`TMP` 为 `.cache/tmp/<任务>`，`QML_DISK_CACHE_PATH` 为 `.cache/qml/<Qt版本>/<配置>`；使用显式项目数据根处理配置和日志，而不是直接依赖默认 AppData 路径。Qt/工具的其他缓存若未提供重定向接口，必须在诊断文档中披露，不能宣称已完全隔离。

### 系统边界

MSVC、Windows SDK 和 Visual Studio Installer 不是完整的便携包。即使把允许配置的安装路径和下载路径放入项目，一些共享组件、注册信息和安装器状态仍留在系统。Windows 图形驱动缓存、系统临时行为也无法由应用保证全部重定向。

因此本项目承诺“项目可控制的依赖和产物全部本地化”，不承诺“操作系统零写入”。若用户要求连编译器及系统安装器都不得引用/写入项目外部，当前 MSVC 方案不满足该条件，必须重新讨论隔离环境，不能悄悄放宽规则。

当前机器的 MSVC、CMake、Ninja 和 Flutter 位于另一个项目 `ririchord/toolchain` 下。它们只能作为环境调查结果，不能成为本项目默认路径，也不直接复制既有 MSVC 安装。

Qt/CMake/Ninja 应使用项目内独立版本。MSVC/SDK 后续可选择：

1. 通过受支持安装方式评估项目专用路径，记录仍在系统的共享部分。
2. 显式在 `.local/toolchain.local.json` 声明外部 MSVC/SDK 例外；医生脚本报告，构建日志留存。没有声明则构建预检报错，不回退到 PATH 或其他项目。

当前通过 `.local/toolchain.local.json` 显式声明本机已有 MSVC 的系统工具例外，用于框架构建验证；Qt/CMake/Ninja 使用项目内独立副本。没有重装或搬迁系统工具。

### 移动、备份与运行数据

- 路径相对项目根，目录含空格也必须可用；禁止硬编码盘符。
- 缓存可再生但未必可搬迁；项目移动后重新配置/编译。不得承诺现成 CMake 构建目录可无损复制到任意位置。
- 备份源码和 `.local` 中有价值的用户配置即可，缓存和工具可由锁定清单重建；离线环境应另行保留已校验下载包。
- 开发运行写 `.local/`；静态便携包将设置、日志和 `cache/` 写在 EXE 旁；动态目录/自解压包沿用 `data/`。位置不可写时明确报错，可通过参数指定数据目录，不静默写入 AppData。

## 6. 依赖与构建规范

基础 Qt 模块预计为 Core、Gui、Qml、Quick、QuickControls2；测试加 Test，只有用到相应能力才添加其他模块。流式 XML、JSON 不需要再装第三方解析器。基础 Qt 安装包可能自带其他模块，但链接与部署仅收集实际用到的部分。

锁定清单必须包含 Qt/编译器 ABI、架构、CMake、Ninja、SDK 版本及下载哈希。当前锁定 Qt 6.11.0 MSVC 2022 x64，构建结果记录在框架验证说明中；这不等于完整播放器产品或性能验收。引入具体版本前核对该版官方平台支持与分发条件。

构建预设：

| 预设 | 产物路径 | 用途 |
|---|---|---|
| windows-debug | out/build/windows-debug | 日常调试和正确性测试 |
| windows-release | out/build/windows-release | 性能与部署验证 |
| windows-static-release | out/build/windows-static-release | 静态 Qt/CRT 单 EXE 与独立部署验收 |

已实现的命令入口（使用 PowerShell 7）：

```powershell
.\scripts\doctor.ps1
.\scripts\bootstrap.ps1
.\scripts\build.ps1 -Preset windows-debug -Test
.\scripts\run.ps1 -Preset windows-debug
.\scripts\prepare-qt-static.ps1
.\scripts\build-qt-static.ps1 -Jobs 8
.\scripts\build.ps1 -Preset windows-static-release -Test
.\scripts\package.ps1
```

- `bootstrap` 准备常规动态 SDK、CMake/Ninja；`prepare-qt-static` 独立准备固定 SHA-256 的官方 Qt 源码。只有这两个显式准备步骤下载依赖，配置/构建/打包不联网。
- `build` 初始化已声明的 MSVC 环境，再调用项目内 CMake/Ninja；输出实际路径与版本，失败返回非零退出码。核心 CMake 不承担安装软件的副作用。
- 共享预设不提交个人路径；本地覆盖要遵守目录规则，不能绕过锁定的 Qt ABI。
- 优先简单 Ninja 单配置构建；不引入编译缓存工具，除非测量证明增量构建仍是瓶颈。
- 用户已明确要求无需释放运行库的单 EXE，并允许同级配置、日志、缓存和临时文件，因此增加独立静态 Qt/CRT 构建，不改变常规动态开发 SDK。Qt 6.11.0 官方源码及特性由 `toolchain/qt-static.lock.json` 固定，SDK 位于 `.tools/qt/6.11.0/msvc2022_64-static`，基础模块构建位于 `out/build/qt-6.11.0-static/`，Quick/QML 使用短路径 `out/build/qs/qml/`；移动源码或改变编译器后不得复用旧缓存。
- `package` 默认 `static-exe`，复制静态主程序并检查真实导入表；QML 和静态插件由 Qt 构建系统编入。FluentWinUI3 及官方 Fusion/Basic 回退保留，其他风格在源码配置阶段关闭。选择 Qt 公开 D3D11 后端，关闭 OpenGL/Vulkan，不携带 DXC/DXIL。
- `-Format directory` 使用相同动态 Qt 的 `windeployqt`，输出到 `out/stage`；`-Format single-exe` 保留 Windows CAB 自解压启动器，依赖位于同级 `data/runtime/`。静态产物无该步骤。许可证、匹配 Qt 源码和应用重新编译材料独立输出；具体边界见 [便携打包](PACKAGING.md)。
- 部署目录在无 SDK PATH 的环境下试运行；最终应在没有开发环境的 Windows 环境验证。

## 7. 验收顺序

1. 工具清单、项目内目录、doctor 和最小 CMake/Qt 构建闭环。
2. 原生能力探针：SMTC 会话、透明窗口、穿透、焦点、置顶、显示器/DPI。
3. 渲染原型：真实中文、英文、emoji、描边和长文本；500/2000 条活动弹幕，1080p/4K。先报告真实上限再确定产品目标。
4. 纯 C++ 时间轴、轨道、暂停、跳转和对象回收测试；错误 XML、配置迁移与后台取消测试。
5. 接入完整控制面板、配置保存和有界日志。
6. Release 部署、长期运行与播放器实测。

性能记录至少包括 CPU/GPU、内存、帧时间分布、活动数、缓存占用、丢弃数和同步误差。只统计帧回调频率不能证明实际显示帧率；真实呈现和同步误差需要单独的测量方法。

## 8. 官方依据

- [Qt Quick Scene Graph](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html)：场景图、渲染线程与生命周期。
- [QML Disk Cache](https://doc.qt.io/qt-6/qmldiskcache.html)：QML 缓存路径配置；具体版本下需实测。
- [Qt Windows Deployment](https://doc.qt.io/qt-6/windows-deployment.html)：部署工具及运行库处理。
- [CMake Presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)：共享/本地配置、构建目录与环境。
- [Visual Studio 安装位置](https://learn.microsoft.com/en-us/visualstudio/install/change-installation-locations?view=vs-2022)：部分共享工具和 SDK 仍安装在系统盘的限制。

## 9. 0.2 实现细节与明确降级

- `danmaku::Engine` 合并时间轴、轨道与活动池，避免无必要微型库；滚动统一像素速度，动画随已知媒体速率缩放。固定/滚动共享物理轨道，允许重叠为显式偏好。
- 文本布局缓存采用估算 16 MiB 的 LRU 预算（1 KiB 基础 + 每 UTF-16 字符约 64 B）；这是估算成本，不是 Qt/驱动实际显存上限。活动布局另由最大在屏 5000 上限约束，字体变化清空布局；描边/行距复用布局并重算占位，字体/描边及 DPR 变化重建必要绘制资源。颜色保存在节点中，不污染可复用布局。
- 可见播放通过 Qt Quick `afterAnimating` 驱动 GUI 核心更新，采用实际单调 delta；隐藏/未暴露/暂停时使用互斥的 33ms 维护节奏。媒体时钟分段累计倍速动画时间。P95/P99 为 GUI 调度间隔，`frameSwapped` 为呈现回调计数，均不等同于测得的 GPU 帧时长。
- SMTC 工作线程按 ID 采样，媒体身份暂用标题/作者/时长组合；同名同长媒体的切换可能无法辨别。提供应用 ID 手动输入；前台限制仅对可关联 AUMID/进程名生效，默认关闭，不承诺跟随播放器窗口。
- JSON 原子写入；数值更新 200 ms 合并持久化，析构前刷新。错误配置首次覆盖前备份；保存失败有重试入口。INI 只读手动导入，跳过不合法旧字段。
- XML 读取和排序都可合作取消；加载结果一次移交。UI 队列日志最多 1000，过载统计丢弃；文件 2 MiB 轮转一份。记录媒体标题的本地诊断报告不默认提交。
- 置顶 0/1/2/3 分别为关闭、窗口标志、每秒保持、每秒先撤销再恢复兼容策略；后两项只处理覆盖窗，不激活窗口。独占全屏不保证可见。
