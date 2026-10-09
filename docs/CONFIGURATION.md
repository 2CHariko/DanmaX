# DanmaX 配置文件说明 (settings.ini)

DanmaX 采用纯 UTF-8 编码的 `settings.ini` 文件作为统一配置存储。静态便携版默认将配置文件保存在与 `DanmaX.exe` 同级的目录下；开发运行位置由 `scripts/run.ps1` 显式重定向至 `.local/`。首次运行应用会自动生成带有完整中文注释的配置文件模板。默认完整配置模板请参考 [settings.example.ini](settings.example.ini)。

> **格式注意**：DanmaX 0.2+ 不再读取或迁移历史版本的 `settings.json` 或旧版 `config.ini`。新版本从干净的 `settings.ini` 或代码内置默认值启动。

---

## 1. 全量配置项速查字典 (21 项)

配置分为 9 个主要分区 (Section)，字段与默认值严格对照应用架构 Schema：

| 分区 (Section) | 键名 (Key) | 类型 | 允许取值区间 / 候选值 | 默认值 | 用途与核心行为说明 |
|---|---|---|---|---|---|
| `[Meta]` | `formatVersion` | 整数 | `1` | `1` | 配置文件格式版本。非 1 版本或缺失版本不自动迁移。 |
| `[Online]` | `danmakuServers` | JSON 数组字符串 | 有效 HTTP/HTTPS URL 列表 | `["https://danmaku-api.152468.xyz"]` | 弹弹play 兼容服务器地址列表。按顺序回退，空数组 `[]` 禁用在线搜索。 |
| `[Appearance]` | `theme` | 字符串 | `"system"`, `"light"`, `"dark"` | `"system"` | UI 主题。高对比度系统模式下优先跟随系统，不强制浅深色。 |
| `[Appearance]` | `fontFamily` | 字符串 | 1~4096 字符字符串 | `"Microsoft YaHei"` | 弹幕渲染字体名称。优先匹配系统已安装字体，缺失字形由系统回退。 |
| `[Appearance]` | `fontSize` | 整数 | 10 ~ 72 (逻辑像素) | `24` | 弹幕基准字号。XML 源字号以 25 为基准按比例动态缩放。 |
| `[Appearance]` | `strokeWidth` | 整数 | 0 ~ 6 (逻辑像素) | `1` | 弹幕文字外描边宽度。`0` 表示无描边。 |
| `[Appearance]` | `opacity` | 浮点数 | 0.05 ~ 1.0 (5% ~ 100%) | `0.85` | 弹幕层整体不透明度。仅作用于弹幕，不改变控制面板透明度。 |
| `[Danmaku]` | `speed` | 整数 | 30 ~ 1500 (逻辑像素/秒) | `180` | 滚动弹幕移动速度。播放中调整保留当前位置与进度，不重新加载。 |
| `[Danmaku]` | `fixedSeconds` | 浮点数 | 1.0 ~ 30.0 (秒) | `5.0` | 顶部与底部固定弹幕的显示时长。暂停时不计入寿命消耗。 |
| `[Danmaku]` | `maxActive` | 整数 | 50 ~ 5000 | `500` | 屏幕同时渲染的最大弹幕数量上限。调低时按先来先到原则保留较早对象。 |
| `[Danmaku]` | `maxTracks` | 整数 | 1 ~ 60 | `18` | 纵向最大轨道数量上限。实际可用行数还受屏幕高度与行高物理限制。 |
| `[Danmaku]` | `lineSpacing` | 浮点数 | 0.0 ~ 2.0 (0% ~ 200%) | `0.2` | 轨道间距占行高的比例。比例越大，同屏容纳轨道数越少。 |
| `[Danmaku]` | `overlap` | 布尔值 | `true`, `false` | `false` | 轨道占满时是否允许重叠。关闭时无安全空位的新弹幕直接丢弃。 |
| `[Rendering]` | `textureBudgetAuto` | 布尔值 | `true`, `false` | `true` | 文字图片预算自适应模式。`true` 依据活跃文字按需分档自动扩缩。 |
| `[Rendering]` | `textureBudgetMiB` | 整数 | 32 ~ 1024 (MiB) | `512` | 图片显存预算上限（自动模式为上限，手动模式为固定值）。非预分配。 |
| `[Sync]` | `timeOffset` | 浮点数 | -120.0 ~ 120.0 (秒) | `0.0` | 弹幕与播放器时间偏移。正值提前出现，负值延后出现。 |
| `[Sync]` | `targetSession` | 字符串 | 任意字符串 (SMTC 会话 ID) | `""` | 选定的 Windows 媒体会话 ID。由播放页选择播放器后自动保存。 |
| `[Window]` | `screenIndex` | 整数 | 0 ~ 32 | `0` | 覆盖层所在显示器索引。索引超出实际屏幕数时自动回退主显示器。 |
| `[Window]` | `onTop` | 整数 | `0`, `1`, `2`, `3` | `1` | 覆盖层置顶策略。0=不置顶，1=常规置顶，2=定时保持，3=撤销再恢复兼容。 |
| `[Window]` | `foregroundOnly` | 布尔值 | `true`, `false` | `false` | 仅播放器处于前台时显示覆盖层。适用于同步模式；无法识别时建议关闭。 |
| `[Diagnostics]` | `debug` | 布尔值 | `true`, `false` | `false` | 是否在覆盖层展示性能与调试统计信息（更新率、内存、图片节点等）。 |
| `[Diagnostics]` | `debugPosition` | 字符串 | `"top_left"`, `"top_right"`, `"bottom_left"`, `"bottom_right"` | `"top_left"` | 调试统计信息在屏幕上的锚定角落位置。 |
| `[Logging]` | `logLevel` | 字符串 | `"DEBUG"`, `"INFO"`, `"WARNING"`, `"ERROR"` | `"INFO"` | 日志输出的最低过滤级别。 |
| `[Logging]` | `logToFile` | 布尔值 | `true`, `false` | `true` | 是否写入 `app.log` 磁盘日志。单文件限 2 MiB 并保留 1 份轮转备份。 |
| `[Playback]` | `lastFile` | 字符串 | 任意路径字符串 | `""` | 最近一次加载的本地 XML 文件绝对/相对路径。启动时不自动加载播放。 |

---

## 2. 手工编辑语法规则

在未运行 DanmaX 实例时，用户可使用任意文本编辑器（推荐 VS Code, Notepad++ 等支持 UTF-8 的编辑器）直接修改 `settings.ini`：

1. **实例互斥**：在手动编辑前**必须退出所有 DanmaX 进程**。程序在运行期间不会主动监听磁盘文件的外部变更，且界面保存时会覆盖外部改动。
2. **编码与字符集**：必须保持 **UTF-8（无 BOM 或带 BOM 均可）** 保存。
3. **布尔类型**：推荐写作 `true` / `false`，解析时亦兼容 `1` / `0`。不接受 `yes` / `no` / `on` / `off`。
4. **数值类型**：浮点数必须使用英文句点 `.`，禁止使用千分位逗号或本地化逗号（如德语环境逗号）。
5. **字符串与路径**：
   - 字符串值建议包含在英文半角双引号 `""` 中。
   - 文件路径推荐使用 POSIX 正斜杠 `/`，例如：`lastFile="D:/Danmaku/sample.xml"`。
   - 若使用 Windows 反斜杠，必须进行双重转义，例如：`lastFile="D:\\Danmaku\\sample.xml"`。
   - 遵循 Qt INI 规则：若字符串首字符为 `@`，需转义为 `@@`。
6. **JSON 数组字符串**（以 `danmakuServers` 为例）：
   - 该字段在 INI 中以转义的 JSON 数组字符串存储，例如：`danmakuServers="[\"https://api1.example.com\",\"https://api2.example.com\"]"`。
   - 清空列表写作 `danmakuServers="[]"`。

---

## 3. 保存机制、错误恢复与安全备份

1. **防抖合并写入**：界面中的普通滑块或输入框在编辑时即时生效，同时延迟合并 200 ms 后触发原子写盘，退出应用时自动刷新未落盘的修改。
2. **原子写入 (QSaveFile)**：写入操作先生成临时文件，在数据完整无误后通过原子系统调用替换目标 `settings.ini`，避免由于突然断电或程序崩溃产生半截破损文件。
3. **破损备份机制**：若程序在启动读取配置时检测到非法 UTF-8、语法严重破损、文件大于 1 MiB 或版本冲突，程序**不会直接盲目覆盖原文件**，而是在第一次触发保存前，将破损的原内容完整备份至 `settings.ini.backup-<UTC时间戳>`，并在界面上向用户展示错误提示。
4. **配置重置与重建**：程序每次保存时均会根据内置的 Schema 自动重建完整的中文注释说明与标准分组，用户自行追加的手动注释或未知扩展字段在保存时将被过滤。

---

## 4. 关键特性配置进阶

### 4.1 自适应文字图片预算 (`[Rendering]`)
- `textureBudgetAuto=true`：当屏幕上有弹幕流动时，系统根据当前在屏文字排版计算 RGBA 纹理内存需求，在 64 MiB 基线基础上保留 25% 缓冲，并按 128 / 256 / 512 MiB 等分档动态递增。当在屏弹幕密度持续回落超过 10 秒后，自动收缩缓存释放显存。
- `textureBudgetMiB=512`：约束纹理缓存扩张的硬上限。超出此上限的弹幕会自动降级回退至 `QSGTextNode` 矢量文字节点渲染，确保永远不会因为显存超限而丢失弹幕内容。

### 4.2 在线弹幕与本地隔离缓存 (`[Online]`)
- 在线弹幕下载后长期保存在 `cache/danmaku/` 目录下，文件名带有来源服务器与剧集参数摘要。
- 换机或备份时，仅需将 `settings.ini` 与 `cache/danmaku/` 文件夹一同复制至新电脑，即可离线保留所有已下载的弹幕剧集。
