# DanmaX 配置文件参考 (settings.ini)

本文档介绍 DanmaX 配置文件 `settings.ini` 的字段字典、手动编辑语法、原子持久化与错误恢复机制。

> [!NOTE]
> DanmaX 支持视频同步与独立弹幕两种任务。旧版 `targetSession`、`automaticDanmaku` 字段会被忽略；独立播放须通过 **更多 → 独立播放弹幕 XML** 显式启动，`lastFile`、缓存、样式与最近记录均会保留但不在启动时自动播放。详见 [播放中心](../architecture/now-playing.md)。

DanmaX 的常规设置保存在 UTF-8 编码的 `settings.ini` 中；外部播放器预设路径与默认项另存于同目录的 `player-presets.json`，详见 [外部播放器预设](../playback/external-players.md)。静态便携版默认将配置文件保存在 `DanmaX.exe` 同级目录下；开发运行配置与日志位于 `out/run/<预设>/`。首次运行会自动生成带完整中文注释的 INI 模板，参考模板见 [settings.example.ini](settings.example.ini)。

> [!IMPORTANT]
> DanmaX 0.2 及以上版本不再读取或迁移旧版 `settings.json` 或早期 `config.ini`。新版本从干净的 `settings.ini` 或内置默认值启动。

## 1. 配置项字典

配置按功能分区（Section）组织，实际字段与默认值以应用 Schema 为准：

| 分区 | 键名 | 类型 | 取值范围 / 候选值 | 默认值 | 说明 |
|---|---|---|---|---|---|
| `[Meta]` | `formatVersion` | 整数 | `1` | `1` | 配置格式版本。非 `1` 或缺失版本时不自动迁移。 |
| `[Online]` | `danmakuServers` | JSON 数组字符串 | 有效 HTTP/HTTPS URL 列表 | `["https://danmaku-api.152468.xyz"]` | 兼容弹弹play 协议的服务端地址列表。按顺序回退，空数组 `[]` 禁用在线搜索。 |
| `[Appearance]` | `theme` | 字符串 | `"system"`, `"light"`, `"dark"` | `"system"` | 界面主题。系统启用高对比度时优先跟随系统。 |
| `[Appearance]` | `fontFamily` | 字符串 | 1–4096 字符 | `"Microsoft YaHei"` | 弹幕渲染字体名称。优先匹配系统已安装字体，缺失字形由系统回退。 |
| `[Appearance]` | `fontSize` | 整数 | 10–72（逻辑像素） | `24` | 弹幕基准字号。XML 源字号以 25 为基准按比例缩放。 |
| `[Appearance]` | `strokeWidth` | 整数 | 0–6（逻辑像素） | `1` | 弹幕文字外描边宽度。`0` 表示无描边。 |
| `[Appearance]` | `opacity` | 浮点数 | 0.05–1.0 | `0.85` | 弹幕覆盖层整体不透明度。不影响主控制面板。 |
| `[Danmaku]` | `speed` | 整数 | 30–1500（逻辑像素/秒） | `180` | 滚动弹幕移动速度。播放中调整保留当前位置与进度。 |
| `[Danmaku]` | `fixedSeconds` | 浮点数 | 1.0–30.0（秒） | `5.0` | 顶部与底部固定弹幕停留时长。暂停时不消耗寿命。 |
| `[Danmaku]` | `maxActive` | 整数 | 50–5000 | `500` | 同屏渲染的最大活动弹幕数量上限。调低时优先保留较早对象。 |
| `[Danmaku]` | `maxTracks` | 整数 | 1–60 | `18` | 纵向最大轨道数量上限。实际可用行数受屏幕高度与行高约束。 |
| `[Danmaku]` | `lineSpacing` | 浮点数 | 0.0–2.0 | `0.2` | 轨道间距占行高的比例。比例越大，同屏可容纳轨道越少。 |
| `[Danmaku]` | `overlap` | 布尔值 | `true`, `false` | `false` | 轨道占满时是否允许重叠。关闭时无安全空位的新弹幕将跳过。 |
| `[Rendering]` | `textureBudgetAuto` | 布尔值 | `true`, `false` | `true` | 文字纹理显存自适应模式。启用后按活跃文字需求分档扩缩。 |
| `[Rendering]` | `textureBudgetMiB` | 整数 | 32–1024（MiB） | `512` | 文字纹理显存预算上限（自动模式为上限，手动模式为固定值）。非预分配。 |
| `[Sync]` | `timeOffset` | 浮点数 | -120.0–120.0（秒） | `0.0` | 当前视频弹幕时间轴偏移。正值提前，负值延后。 |
| `[Window]` | `screenIndex` | 整数 | 0–32 | `0` | 覆盖层所在显示器索引。越界时自动回退至主显示器。 |
| `[Window]` | `onTop` | 整数 | `0`, `1` | `1` | `0` 为不置顶，`1` 为置顶。统一使用窗口事件恢复与 100 ms 周期检查；旧值 `2`/`3` 读取后迁移为 `1`。 |
| `[Window]` | `foregroundOnly` | 布尔值 | `true`, `false` | `false` | 仅在目标播放器位于前台时显示覆盖层。 |
| `[Diagnostics]` | `debug` | 布尔值 | `true`, `false` | `false` | 是否在覆盖层显示调试与性能统计信息。 |
| `[Diagnostics]` | `debugPosition` | 字符串 | `"top_left"`, `"top_right"`, `"bottom_left"`, `"bottom_right"` | `"top_left"` | 调试信息在覆盖层上的锚定角落位置。 |
| `[Logging]` | `logLevel` | 字符串 | `"DEBUG"`, `"INFO"`, `"WARNING"`, `"ERROR"` | `"INFO"` | 日志输出过滤级别。 |
| `[Logging]` | `logToFile` | 布尔值 | `true`, `false` | `true` | 是否写入 `app.log` 磁盘日志。单文件上限 2 MiB 并保留 1 份轮转备份。 |
| `[Playback]` | `lastFile` | 字符串 | 任意路径字符串 | `""` | 最近一次加载的本地 XML 路径。启动时不自动加载播放。 |

## 2. 手工编辑语法规则

在所有 DanmaX 进程退出的前提下，你可以使用支持 UTF-8 的文本编辑器修改 `settings.ini`：

1. **退出实例后再编辑**：程序运行期间不监听外部文件变更，界面保存操作会覆盖你在外部修改的内容。
2. **编码格式**：使用 **UTF-8**（无 BOM 或带 BOM 均可）保存。
3. **布尔值**：推荐写为 `true` 或 `false`（读取时兼容 `1` / `0`），不接受 `yes` / `no` / `on` / `off`。
4. **数值格式**：浮点数使用英文半角句点 `.`，禁止使用千分位或区域化逗号。
5. **字符串与路径**：
   - 建议使用英文半角双引号 `""` 包裹字符串。
   - 文件路径推荐使用正斜杠 `/`，例如 `lastFile="D:/Danmaku/sample.xml"`。
   - 遵循 Qt INI 规则：若字符串首字符为 `@`，必须转义为 `@@`。
6. **JSON 数组字符串**（以 `danmakuServers` 为例）：
   - 内部双引号需使用反斜杠转义，例如 `danmakuServers="[\"https://danmaku-api.152468.xyz\"]"`。
   - 清空列表写为 `danmakuServers="[]"`。

## 3. 持久化与错误恢复

1. **防抖合并写入**：界面中的常规修改即时在内存中生效，高频输入（如滑块拖动）经 200 ms 节流合并后触发原子写盘；应用退出时自动刷盘未保存修改。
2. **原子替换（`QSaveFile`）**：先写入临时文件，校验完整后原子替换目标 `settings.ini`，防止意外断电或崩溃导致半截损坏文件。
3. **破损备份**：若启动时检测到非法 UTF-8、语法严重损坏、文件超过 1 MiB 或版本冲突，应用不会直接覆盖原文件，而是在首次触发保存前将原内容备份至 `settings.ini.backup-<UTC时间戳>` 并在界面提示错误。
4. **模板重建**：每次保存均根据内置 Schema 重建完整中文注释与标准分组；人工添加的额外注释或未知字段在保存时不会保留。

## 4. 进阶特性配置说明

### 4.1 自适应文字纹理预算（`[Rendering]`）

- `textureBudgetAuto=true`：根据当前在屏活跃文字排版动态计算 RGBA 纹理需求，在 64 MiB 基线基础上保留 25% 缓冲，按 128 / 256 / 512 MiB 分档递增；密度持续回落超过 10 秒后自动收缩释放显存。
- `textureBudgetMiB=512`：约束纹理缓存扩张的硬上限。超出上限的弹幕回退至 `QSGTextNode` 矢量渲染，不会丢弃弹幕内容。

### 4.2 数据与缓存目录规则

- 缓存根目录默认位于数据目录下的 `cache/`，可通过 `--cache-dir` 参数覆盖。
- 开发运行按预设隔离在 `out/run/windows-debug/`、`out/run/windows-release/` 与 `out/run/windows-static-release/`；动态部署与静态便携包默认使用可执行文件同级的 `settings.ini`、`app.log` 与 `cache/`。
- 历史 `.local/data/`、`.cache/qml/<Qt版本>/` 与早期 `data/` 目录保留原状，不自动迁移或覆盖。

## 相关内容

- [DanmaX 用户指南](user-guide.md)
- [示例配置文件](settings.example.ini)
- [外部播放器预设](../playback/external-players.md)
- [C++ / Qt 核心架构](../architecture/cpp-qt-architecture.md)
