# 本地弹幕播放器 (Local Danmaku Overlay)

一个功能强大的本地弹幕播放悬浮窗，旨在为您本地观看的视频带来如在线视频网站般的实时弹幕体验。

所有代码均由 Gemini 2.5 Pro 生成。

本工具能够读取Bilibili风格的本地XML弹幕文件，并将其精准同步到您正在使用的媒体播放器（如 PotPlayer, MPC-HC 等）上，以一个透明、可交互的悬浮窗形式呈现在屏幕最上层。

![Build Status](https://img.shields.io/badge/build-passing-brightgreen)
![Python Version](https://img.shields.io/badge/python-3.9+-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

---

## 📖 主要功能

* **精准同步**: 深度集成 Windows 系统媒体控件 (SMTC)，实时获取播放器状态、播放/暂停、以及精确到毫秒的播放进度，完美同步弹幕。
* **高兼容性**: 支持 Bilibili 风格的 XML 弹幕文件。
* **高度自定义**:
    * **显示效果**:自由调整弹幕字体、大小、描边宽度和全局不透明度。
    * **弹幕行为**: 自定义滚动速度、顶部/底部弹幕的显示时长、最大同屏弹幕数以及弹幕轨道数量。
    * **性能策略**: 可开启/关闭弹幕重叠，以在“弹幕密度”和“防遮挡”之间取得平衡。
* **智能热重载**: 在控制面板中修改任何设置后，点击“应用”即可立即生效，无需重启程序。
* **高性能渲染**:
    * **对象池技术**: 复用弹幕对象，极大减少运行时开销。
    * **Pixmap 缓存**: 预渲染弹幕为位图，动画过程仅需绘制图片，CPU占用率极低。
* **用户友好的播放器设置**:
    * **AUMID 自动发现**: 无需手动查找播放器的AUMID，点击“发现”按钮即可从当前运行的媒体应用中选择。
* **强大的调试模式**:
    * **实时信息悬浮窗**: 可开启一个美观的调试信息面板，显示FPS、CPU/内存占用、弹幕对象池状态以及当前播放媒体的标题和进度。
* **完善的日志系统**: 所有操作和错误都会被记录，并可在控制面板的“日志”页面查看，同时也支持保存到本地文件。

## 🖥️ 界面截图

#### 控制面板
集成了主控制、详细设置和日志查看三大功能模块，界面清晰，操作便捷。

![控制面板截图](https://raw.githubusercontent.com/Nanako660/LocalPlayerDanmakuOverlay/main/img/1.png)

#### 弹幕与调试信息效果
弹幕清晰地悬浮在播放器之上。开启调试模式后，角落会显示一个带半透明背景的调试信息面板。

![弹幕与调试信息截图](https://raw.githubusercontent.com/Nanako660/LocalPlayerDanmakuOverlay/main/img/2.png)

## 🚀 安装与运行

### 系统要求
* **操作系统**: Windows 10 / Windows 11 (因依赖SMTC媒体接口)
* **Python 版本**: Python 3.9 或更高版本

### 安装步骤

1.  **克隆或下载项目**
    ```bash
    git clone [https://github.com/Nanako660/LocalPlayerDanmakuOverlay.git](https://github.com/Nanako660/LocalPlayerDanmakuOverlay.git)
    cd LocalPlayerDannakuOverlay
    ```

2.  **安装依赖**
    项目依赖的库已在 `requirements.txt` 中列出。使用 pip 安装：
    ```bash
    pip install -r requirements.txt
    ```
    `requirements.txt` 文件内容:
    ```
    PyQt6
    psutil
    pywin32
    winsdk
    ```

3.  **运行程序**
    ```bash
    python main.py
    ```
    程序启动后会显示控制面板。

### 使用说明

1.  在 **“主界面”**，点击 **“浏览...”** 选择一个 `.xml` 格式的弹幕文件。
2.  打开您的本地播放器（如 PotPlayer）并开始播放视频。
3.  切换到本程序的 **“设置”** 页面。
4.  在 **“目标播放器AUMID”** 栏，点击 **“发现...”** 按钮。在弹出的对话框中，应能看到您的播放器。选中它并点击“OK”。
5.  回到 **“主界面”**，点击 **“▶ 开始播放”**。
6.  此时，弹幕应该已经出现在屏幕上，并与您的视频同步。尽情享受吧！

## 🔧 核心功能详解

### AUMID 自动发现
本程序通过 AUMID (Application User Model ID) 来识别目标播放器。这是一个技术性很强的ID。为了简化操作，您无需关心它是什么，只需点击“发现...”按钮，程序会自动列出所有正在播放媒体的程序，您只需根据程序名称和播放内容标题选择即可。

### 调试模式
在“设置”页面勾选“启用调试信息”，弹幕窗口的角落（位置可调）会显示一个信息面板，包含：
* **Title**: 当前播放内容的标题。
* **Time**: 实时播放进度 / 总时长。
* **FPS**: 弹幕渲染的帧率。
* **CPU/Mem**: 程序自身的CPU和内存占用。
* **Danmaku Stats**: 已加载/在屏/对象池空闲的弹幕数量。

## 📂 项目结构

项目代码结构清晰，职责分明。

```
├── monitors/                 # 媒体监控器模块（为跨平台设计）
│   ├── init.py
│   ├── base_monitor.py       # 定义监控器的抽象基类
│   └── windows_monitor.py    # Windows平台的监控器实现
├── main.py                   # 程序主入口
├── config_loader.py          # 配置文件加载与管理 (单例模式)
├── logger_setup.py           # 日志系统配置
├── danmaku_models.py         # 核心数据模型 (DanmakuData, ActiveDanmaku)
├── danmaku_parser.py         # XML弹幕文件解析器
├── danmaku_renderer.py       # 弹幕渲染窗口 (基于PyQt6)
├── danmaku_controller.py     # 核心控制器，应用的“大脑”
├── control_panel.py          # 控制面板UI界面
├── debug_overlay.py          # 调试信息悬浮窗的绘制逻辑
└── config.ini                # 配置文件
```

## 🛠️ 架构设计

* **MVC 模式**: 严格遵循 Model-View-Controller 设计模式，将数据、界面和逻辑解耦。
* **多线程与异步**: 控制器 (`DanmakuController`) 将耗时的媒体监控任务 (`MediaSyncWorker`) 放在一个独立的 `QThread` 中运行，该任务内部使用 `asyncio` 与 `winsdk` 进行异步通信。这保证了即使媒体信息获取有延迟，主GUI界面也绝不会卡顿。
* **性能优化**: 大量使用对象池和Pixmap缓存等关键技术，确保即使在“弹幕雨”场景下也能保持极低的系统资源占用和流畅的动画效果。

## 📝 未来计划

* [ ] **跨平台支持**: 目前已搭建好支持跨平台的监控器架构，未来可添加对 macOS 和 Linux (MPRIS) 的支持。
* [ ] **更多弹幕格式**: 增加对 AcFun JSON 等其他弹幕格式的支持。
* [ ] **UI/UX 增强**: 对设置界面进行进一步的美化和分组，提供更丰富的交互体验。

## 📄 许可证

本项目采用 [MIT License](https://opensource.org/licenses/MIT) 授权。

