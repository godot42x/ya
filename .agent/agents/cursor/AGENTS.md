# Cursor 工具层

## 入口

角色文件在 `.cursor/agents/*.md`，随仓库走，不放用户目录。frontmatter 字段：`name`、`description`、`model`、`readonly`。正文只放指针 + Cursor 独有差异，职责与仓库规则在 `../common/`。

## 角色映射

| 角色 | Cursor 名字 | 用途 | 模型 |
| --- | --- | --- | --- |
| worker | `ya-worker` | 实现 / 返工 / 收尾，可改文件、跑构建测试 | `grok-4.7[reasoning_effort=high,fast=false]` |
| reviewer | `ya-reviewer` | 只读审查：diff 范围、用户改动保留、测试复现、实现质量 | 同上，`readonly: true` |
| explorer | `ya-explorer` | 只读探索 / 盘点：定位代码、调用点与消费者、依赖与数据流 | 同上，`readonly: true` |
| — | `explore`（内置） | 不用：探索与盘点走 `ya-explorer`，diff 审查走 `ya-reviewer` | 不受 frontmatter 约束 |

## 硬规则

- **派发时必须显式传 `model: grok-4.7-high`**（2026-10-07 拍板）。frontmatter 里的括号语法（`grok-4.7[...]`）**不被 Task 解析**，不传 `model` 会回落到父会话默认模型（实测 3 个派发全部落在 opus-5.5 上）。frontmatter 的 `model` 只作意图记录。
- **只用标定过模型的项目 agent**：实现用 `ya-worker`，只读探索 / 盘点用 `ya-explorer`，diff 审查用 `ya-reviewer`；不用 `explore` / `generalPurpose` 等内置类型（它们沿用父 agent 模型或目录默认变体）。
- 每一次 `Task`——实现、只读盘点、审查、交接、甚至一句"停止"——都是这三者之一（`ya-worker` / `ya-explorer` / `ya-reviewer`）。
- **不续接**（不传 `resume`）：见下。
- 已经在跑的 agent 类型或模型不对：让它自然结束，不续接去叫停，产出经严格核对后再定去留。
- 长任务用 `run_in_background: true` 并读 transcript 判断是否在推进，不要阻塞等待。

## 续接（已证实有问题，默认不用）

Grok 4.7 的续接**已证实回落成 fast 变体，且缓存被打穿**，显式传 `model` 也救不了：

- 已证实（日志）：subagent 首次运行时 composer 存档参数为空，请求用保存的偏好补全；首次运行结束约 4 秒后 `Healed stale per-composer parameters` 按模型目录默认变体写入 `fast=true`（不看用户偏好）；之后每次续接读到的都是这份已写入的值。
- 已证实（用量 CSV）：两个新开 subagent 首次运行都是 `grok-4.7-high`；一小时后两次续接都是 `grok-4.7-high-fast`，包括显式传了 `model: grok-4.7-high` 的那一个。
- 续接还打穿缓存：变体变化后缓存不共享，当天 18 条 fast 记录里有 5 条几乎没有 cache read，合计约 69 万 uncached 输入。

结论：**同一条线的后续任务新开一个 `ya-worker` 并在 prompt 里交接**（上次提交号、已完成项、本次方案、禁区、验证基线），不要 `resume`。

其他续接消息：

- 只能续接**已结束**的 subagent；运行中的要 `interrupt: true`，只在用户明确要打断时用。
- 续接沿用原模型，不能换。
- `Composer not found for resume`：断线或跨会话，agent id 不跨会话保留 → 新开并做状态交接。
- `Subagent parent mismatch for resume`：侧聊不能续接主聊天派的 subagent → 在侧聊里新开。
- 官方 issue：[续接回落 fast](https://forum.cursor.com/t/resume-to-first-party-non-fast-models-redirects-to-fast-variants/166470)、[续接用错模型](https://forum.cursor.com/t/subagent-resume-runs-an-incorrect-model/160590)、[模型选择不被尊重](https://forum.cursor.com/t/subagent-model-choice-not-respected/163645)。

## 沙箱

- `ps`、全量 `ya-testing`、编辑器 / runtime 截图要在沙箱外跑（`required_permissions: ["all"]`），派发时写进 prompt。
- 跑编辑器前确认没有残留进程，否则会撞实例锁（`already running as pid`）。

## 模型选择的实测细节（2026-10-03 / 10-05 / 10-07）

- **Task 的 `model` 传 `grok-4.7-high` 也会得到 fast（2026-10-03 查实）。** 日志 `fixupModelConfig: populating selectedModels from modelName` 显示 slug 的 `-high` / `-high-fast` 后缀被丢掉，只剩 `grok-4.7`，composer 的参数数为 0；请求时用目录默认值补成 3 个参数，随后 `Healed stale per-composer parameters` 把它们写进 composer。state.vscdb 里所有经 Task 创建的 grok-4.7 subagent 都是 `reasoning_effort=high, fast=true`，`-fast` 与否无区别；用户自己开的 composer 才是 `fast=false`。界面标签在第一次运行结束、heal 之后才变成 "High Fast"，所以新开的 agent 刚创建时看起来是对的。
- **Task 的 `model` 不接受括号语法**：传 `grok-4.7[...fast=false]` 报 `Invalid model selection`，只接受会话 `<available_subagent_models>` 里的 slug。
- **撤回**：先前「在 agent 文件里加 `context=256k` 后新 worker 不再 fast」的结论不成立——当时读到的空参数是自愈前的状态。`.cursor/agents/*.md` 括号里的参数（`fast=false` 等）看起来没有进入 composer 存档（首次运行后存档仍是空），但这是读存档的推断，没有直接实测。
- **官方 issue**（均为已知 bug、未给修复时间）：[续接回落 fast](https://forum.cursor.com/t/resume-to-first-party-non-fast-models-redirects-to-fast-variants/166470)（推理强度也失效）、[续接回落到父会话模型](https://forum.cursor.com/t/resuming-a-subagent-in-cursor-ide-switches-it-to-the-parent-agent-s-model-instead-of-keeping-the-subagent-s-original-model/165649)（官方绕法：续接时显式再传 `model`，或不续接直接新开）、[续接用错模型（3.5.3 修过后回归）](https://forum.cursor.com/t/subagent-resume-runs-an-incorrect-model/160590)、[模型选择不被尊重](https://forum.cursor.com/t/subagent-model-choice-not-respected/163645)、[CLI 变 fast](https://forum.cursor.com/t/cli-subagents-changing-model-to-composer-2-5-fast-mode-by-itself/162752)。文档：[Subagents 的 Model parameters](https://cursor.com/docs/subagents)。
- **妥协边界（2026-10-07 拍板）**：不再因此停派 Grok 4.7——派发显式传 `model` slug、接受其 fast 变体，靠「结果核对」兜底，并尽量少派。若将来给 `ya-worker` / `ya-reviewer` 配好 `subagentModelOverrides` 并探针确认非 fast，可撤掉这个妥协。

## 该层没有的东西

`send_message` / `interrupt_agent` / `list_agents` 是 DSH 的能力，Cursor 侧对应的是 `resume` 与 `interrupt`。
