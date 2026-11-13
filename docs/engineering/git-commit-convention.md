# Git 提交（Commit）规范与 Hook 指南

本规范基于 Conventional Commits 定义 DanmaX 仓库的提交信息格式，并以简体中文作为第一提交语言，确保变更历史清晰、可检索且可直接用于版本发布说明。

> [!IMPORTANT]
> 所有提交至本仓库的 Commit（除 Git 自动生成的合并或回退提交外）均须遵守本格式。推荐使用仓库提供的提交模板与本地 `commit-msg` 校验 Hook。

## 1. 提交信息格式

```text
<type>(<scope>): <简体中文主题>

[可选正文：说明改动动机、影响范围与设计权衡]

[可选脚注：Closes #123 / BREAKING CHANGE: ...]
```

### 字段说明

| 字段 | 必填 | 格式要求 | 说明 |
|---|---|---|---|
| `type` | 是 | 小写英文枚举 | 见下方类型字典 |
| `scope` | 否 | 小写字母、数字或短横线，如 `(player)` | 标明受影响模块 |
| `:` 与空格 | 是 | 英文半角冒号后接一个空格 | 严格区分大小写与半角符号 |
| `主题` | 是 | **简体中文为主**，不超过 72 个字符，末尾不加句号 | 简明描述本次改动结果，可用英文专有名词（如 `libVLC`、`QWindowKit`） |
| `正文` | 否 | 空一行后书写 | 解释“为什么改”而非重复“改了什么文件” |

## 2. Type 与 Scope 字典

### 常用 `type`

| `type` | 适用场景 |
|---|---|
| `feat` | 新增面向用户的功能或开发特性 |
| `fix` | 修复缺陷或行为回归 |
| `docs` | 仅修改文档、注释或说明文件 |
| `refactor` | 重构代码结构（不改变外部功能行为） |
| `perf` | 性能优化与资源预算改进 |
| `style` | 代码格式化、排版调整（不影响逻辑） |
| `test` | 新增或修正 CTest / 脚本测试用例 |
| `build` | 修改构建系统、Presets、依赖锁定或打包脚本 |
| `ci` | 调整持续集成或自动化工作流 |
| `chore` | 仓库维护、配置模板或非业务杂项改动 |
| `revert` | 回退既有提交 |

### 推荐 `scope`

- 核心与业务：`core`、`engine`、`clock`、`danmaku`、`xml`、`online`、`recent`、`config`
- 播放与定位：`player`、`potplayer`、`vlc`、`libvlc`、`seek`、`overlay`、`toolbar`
- 界面与资产：`ui`、`qml`、`fluent`、`assets`、`titlebar`
- 工程与文档：`docs`、`agents`、`scripts`、`package`、`deps`、`hooks`

## 3. 正反示例

### 推荐示例

```text
feat(player): 支持弹幕匹配失败时暂停播放器并弹出手动匹配窗
fix(seek): 拦截延迟媒体采样导致的进度条回弹
docs(agents): 精简代理约束并按专题重建文档索引
build(package): 在静态便携包中附带 QWindowKit 合规材料
test(scripts): 补充文档路径迁移后的打包脚本回归校验
```

### 不规范示例（将被 Hook 拦截）

```text
优化弹窗布局                      # 缺少 type 与冒号前缀
feat: fix bug                     # 未使用中文说明且信息空洞
fix(Player): 修复问题。            # scope 使用了大写且末尾包含句号
WIP: 临时保存                      # 不支持 WIP 作为正式 type
```

## 4. 启用本地 Commit 模板与 Hook

仓库根目录提供 `.gitmessage` 模板与 `.githooks/commit-msg` 校验脚本。在当前仓库中运行以下命令即可启用本地校验（仅写入当前仓库 `.git/config`，不修改全局 Git 配置）：

```powershell
pwsh -NoProfile -File scripts/install-git-hooks.ps1
```

> [!NOTE]
> Hook 允许 Git 自动生成的 `Merge ...` 与 `Revert "..."` 提交通过；其余提交如不符合格式，将在提交阶段中止并输出中文修正提示。

## 相关内容

- [参与贡献指南](../../CONTRIBUTING.md)
- [DanmaX 文档撰写规范](documentation-style.md)
- [仓库开发约束](../../AGENTS.md)
