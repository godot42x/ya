# Copilot / VS Code 工具层

## 入口

角色文件是 `*.agent.md`，frontmatter 用 Copilot 风格字段。本仓库历史上它们放在 `.github/agents/`，`b665317a4` 迁到 `.agent/agents/`。迁走后仓库里不再有 `.github/agents/`：**若你的客户端只扫默认目录，需要二选一**——把 agent 文件目录指到 `.agent/agents`，或把这些文件放回 `.github/agents/`；不要两边都放。

## frontmatter 字段（本仓库在用的）

| 字段 | 用途 |
| --- | --- |
| `description` | 面向模型的选择依据 |
| `tools` | 工具白名单（数组或方括号列表） |
| `name` | 显示名 |
| `argument-hint` | 调用时的参数提示 |
| `user-invocable` | 是否出现在用户可调用列表 |
| `agents` | 可派发的子 agent 类型 |

`tools:` 里两种 id 风格在本仓库并存，按你的客户端补齐：

- `file_search`、`grep_search`、`semantic_search`、`read_file`、`create_file`、`replace_string_in_file`、`multi_replace_string_in_file`、`get_errors`、`run_in_terminal`（`gamedev.agent.md`）
- `read`、`edit`、`search`、`execute`、`todo`（`plan-harness.agent.md`）

## 角色映射

| 角色 | 文件 | 现状 |
| --- | --- | --- |
| gamedev | `.agent/agents/gamedev.agent.md` | 有，正文在 `../common/gamedev.md` |
| plan-harness | `.agent/agents/plan-harness.agent.md` | 有，正文在 `../common/plan-harness.md` |
| worker | — | 还没有：需要时按本层模板加 `ya-worker.agent.md` |
| reviewer | — | 还没有：需要时按本层模板加 `ya-reviewer.agent.md` |
| explorer | — | 还没有：需要时按本层模板加 `ya-explorer.agent.md`（只读白名单，去掉写类工具） |

## 权限靠白名单

Copilot 没有 DSH / Cursor 的沙箱与审批参数，"只读"由 `tools:` 决定：

- 实现型（worker / gamedev）：需要 `create_file` / `replace_string_in_file` / `run_in_terminal`。
- 只读型（reviewer / explorer）：**不要**把这些写类工具放进白名单；只留读取、搜索与只读终端。这样 `../common/reviewer.md` 与 `../common/explorer.md` 的「不改文件」才有机制保证，而不只是 prompt 约定。

## 该层没有的东西

没有 `model` slug 约定（本仓库的项目 agent 没在 frontmatter 标模型）、没有续接语义、没有 `send_message`；派发与续接策略看 `../skills/subagent-delegation/SKILL.md`。
