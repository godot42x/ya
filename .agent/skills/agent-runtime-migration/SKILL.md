---
name: agent-runtime-migration
description: 把本仓库的 agent 工作流（subagent 规则、角色定义、派发门禁、构建锁）从 Cursor 迁到别的 agent 应用（Codex、zcode、dsh 等）的规格与验收清单。只在用户明确说要切换 agent 应用时使用；日常工作不要读。
---

# Agent 运行时迁移（Cursor → 其他 agent 应用）

**这是迁移规格，不是现行流程。** 用户明确说要切换 agent 应用时才读。
现在的 Cursor 环境（`.cursor/`）必须保持可用：迁移是**增加**适配层，不删除、不改动
Cursor 侧文件的行为。

## 谁来做

- **执行者**：新应用里的主 agent，从第 0 步开始照本文走。
- **复核者**：另一个只读 agent（或用户本人）核对第 2 步的探针结果。探针结论决定后面用
  硬阻断还是降级，不能由执行者一个人自说自话。
- 执行者不得凭对该应用的记忆填能力矩阵：每一格要么有探针证据，要么写「未验证」。

## 0. 先决条件

1. 读根 `AGENTS.md`、`.agent/skills/subagent-delegation/SKILL.md`、
   `.agent/skills/ya-build/SKILL.md` 的「并行 agent 的独占构建锁」一节。
2. `git status`：用户的脱手改动不碰不暂存（清单见 `subagent-delegation` 与当时的任务交接）。
3. 约定分支：迁移工作单独提交，每个提交一个可验收目标，格式 `[agent] ...`。

## 1. 现状盘点（Cursor 3.23.12）

### 已经中立，直接可用

| 内容 | 位置 | 说明 |
| --- | --- | --- |
| 总规则 | `AGENTS.md`、`CLAUDE.md`（`@AGENTS.md`）、`.agent/skills/*` | 纯 markdown。新应用若不读 `AGENTS.md`，在它自己的入口文件里指向这里。 |
| 独占构建锁 | `Script/agent/ya_lock.sh` | 纯 shell，靠环境变量 `YA_AGENT_NAME` 区分调用者；`run -- <cmd>` / `status`。 |
| 派发判定逻辑 | `.cursor/hooks/subagent-reuse-gate.py` 中的 `decide()` | 输入「subagent 类型、prompt 文本、会话记录路径」，输出「放行 / 违规 + 说明」。 |

### Cursor 专用，迁移时要重写

| 内容 | 位置 | Cursor 专用点 |
| --- | --- | --- |
| 角色定义 | `.cursor/agents/ya-worker.md`、`ya-reviewer.md` | frontmatter 的 `model: grok-4.7[context=256k,reasoning_effort=high,fast=false]` 是 Cursor 方括号参数语法；`readonly` 等字段也是 Cursor 的。 |
| 钩子接线 | `.cursor/hooks.json` | 事件名（`subagentStart` / `stop`）、payload 字段、输出格式（`permission` / `followup_message`）。 |
| 门禁适配 | `.cursor/hooks/subagent-reuse-gate.py` 的 `main()`、`.cursor/hooks/subagent-reuse-followup.py` | 读 Cursor payload 字段 `subagent_type` / `task` / `transcript_path` / `tool_call_id`；用 `agent-transcripts/<会话>/subagents/<id>.jsonl` 校验续接 id。 |
| 续接 / 分叉 | skill 里的 `resume: <id>` / `resume: "self"` | Task 工具参数；agent id 与会话目录布局由应用决定。 |
| 模型回落问题 | skill 里的「禁止 fast」一节 | Cursor 的 subagent 可能静默回落到 Fast 变体；别的应用有没有同类问题要探针确认。 |

### 不迁移

线表（线 → agent id）、agent id、会话转写——都只在当前会话有效，新应用自己维护。

## 2. 能力探针（先探测，后设计）

对目标应用逐项实测，结果写进下面第 3 节的矩阵。每项给出**怎么测**和**通过标准**。
临时探针脚本放 `/tmp`，不进仓库；探针派出的 subagent 一律用无副作用的 prompt
（例如「只回复 OK，不读写任何文件」）。

| # | 要确认的能力 | 怎么测 | 判定 |
| --- | --- | --- | --- |
| P1 | 有没有 subagent | 在主 agent 里派一个只回复 OK 的 subagent | 有 / 无 |
| P2 | 能不能续接已结束的 subagent，id 从哪来 | 派一个，记下返回的 id，再用该 id 发第二条消息，让它说出第一条消息的内容 | 能记住上文 = 续接生效 |
| P3 | 能不能分叉（继承父对话） | 用应用的分叉方式派一个，让它复述父对话里的一个暗号 | 复述正确 = 有 |
| P4 | 有没有钩子/事件机制 | 查应用配置；写一个只把 stdin 原样写到 `/tmp` 的钩子 | 钩子被调用 = 有 |
| P5 | 哪些事件能观察到「派发 subagent」 | 对 P4 的钩子挂各类事件，各派一次 subagent | 记录哪个事件触发、payload 全部字段 |
| P6 | payload 里有没有「这次是不是续接」 | 同一 subagent 类型，分别新开和续接各一次，对比两次 payload | 有区分字段 = 可直接判；没有 = 只能靠 prompt 声明 |
| P7 | 钩子返回拒绝时，派发是否真的被拦 | 钩子对每次派发返回拒绝（该应用文档里写的所有拒绝方式都试：JSON 字段、退出码），再派一个 OK subagent | subagent 没有运行 = 拦得住；仍运行 = 拦不住 |
| P8 | 一次派发钩子被调用几次 | 看 P5 日志同一次派发的条数 | 若 > 1，门禁必须按调用 id 去重 |
| P9 | 有没有「结束一轮」的钩子能把消息塞回给 agent | 写一个结束时返回追加消息的钩子，看 agent 是否收到并继续 | 收到 = 可做事后检测 |
| P10 | 模型/强度参数怎么指定，会不会静默回落 | 指定非默认参数派一个 subagent，**首次运行和续接各一次**，在应用的用量/请求记录里核对每次实际用的参数；同时核对本地存档在两次之间有没有被改写 | 每次实际 = 指定 = 无回落 |
| P11 | 能不能读到别的 agent 的会话记录（用来校验续接 id） | 找 P2 里那个 subagent 的转写文件或 API | 有路径/接口 = 可校验 id |
| P12 | 并行 subagent 共享一个仓库时，环境变量能不能给每个 subagent 单独设 | 派两个，各自 `echo $YA_AGENT_NAME` | 能设 = 锁可以区分调用者；不能 = 让 prompt 里要求 subagent 自己 `export` |

## 3. 能力矩阵

「Cursor」列是 2026-10-05 在 Cursor 3.23.12 上的实测。其他列**一律先写「未验证」**，
探针做完再填，并在备注里写日期、应用版本。

| 能力 | Cursor 3.23.12 | Codex | zcode | dsh |
| --- | --- | --- | --- | --- |
| P1 subagent | 有（`Task`） | 未验证 | 未验证 | 未验证 |
| P2 续接 | 有（`resume: <id>`，仅已结束的；运行中需 `interrupt`） | 未验证 | 未验证 | 未验证 |
| P3 分叉 | 有（`resume: "self"`） | 未验证 | 未验证 | 未验证 |
| P4 钩子 | 有（`.cursor/hooks.json`） | 未验证 | 未验证 | 未验证 |
| P5 派发可见事件 | `subagentStart`；`preToolUse` 对 `Task` **不触发** | 未验证 | 未验证 | 未验证 |
| P6 payload 有无续接标记 | **无**（`subagent_id` 等于本次 tool_call_id，新开和续接一样） | 未验证 | 未验证 | 未验证 |
| P7 拒绝能否拦住 | **拦不住**（JSON `permission: deny` 与退出码 2 都试过，subagent 照常运行） | 未验证 | 未验证 | 未验证 |
| P8 钩子每次派发调用次数 | **2 次** | 未验证 | 未验证 | 未验证 |
| P9 结束时塞回消息 | 有（`stop` 钩子的 `followup_message`，实测生效） | 未验证 | 未验证 | 未验证 |
| P10 模型参数回落 | 有：首次运行用保存的偏好，结束后被「自愈」按目录默认变体写成 `fast=true`，续接读到它；用量 CSV 已证实续接变 fast（显式传 model 也无效）且缓存失效。agent 文件里的括号参数似乎不进入 composer 存档。详见 `subagent-delegation`「模型」一节 | 未验证 | 未验证 | 未验证 |
| P11 可读他人转写 | 有（`agent-transcripts/<会话>/subagents/<id>.jsonl`）；侧聊里钩子 payload 的 `transcript_path` 为空，需扫描所有项目转写根目录；续接只能由派发它的会话发起（否则 `parent mismatch`） | 未验证 | 未验证 | 未验证 |
| P12 每个 subagent 单独设环境变量 | 未验证（现行做法：prompt 要求 worker 自己 `export YA_AGENT_NAME`） | 未验证 | 未验证 | 未验证 |

## 4. 按矩阵结果选方案

| 探针结果 | 门禁方案 |
| --- | --- |
| P7 拦得住 | 硬阻断：钩子拒绝没有声明的派发。 |
| P7 拦不住，P9 有 | 检测：钩子记录违规，结束时把清单塞回给 agent（就是现在 Cursor 上的做法）。 |
| P7 拦不住，P9 无 | 事后审计：写一个扫描会话转写、找「没有声明就派发」的脚本，由用户或 CI 定期跑。 |
| 没有钩子（P4 无） | 只靠 `AGENTS.md` / skill 约定 + 事后审计脚本。 |
| P6 有续接标记 | 可以不要求 prompt 里的声明行，直接判「没带续接标记且没有合法新开理由」。 |
| P11 无 | 续接 id 校验退化成格式校验，在文档里写明这点缺口。 |
| P8 > 1 | 违规记录按调用 id 去重（现在 Cursor 适配器已做）。 |

不论哪种方案，**规则本身不变**：同一条线上有已结束的 subagent 就续接；新开只允许
`新线 / 换模型 / 方案推翻 / 上下文过长 / 断线` 且要声明理由。变的只是怎么发现违规。

## 5. 要做的改动（切换时执行，不是现在）

按顺序，每步一个提交，Cursor 侧文件保持可用：

1. **拆门禁。** 把 `subagent-reuse-gate.py` 的 `decide()` 抽到中立位置
   （建议 `Script/agent/dispatch_gate.py`），输入归一化字段、输出判定；
   `.cursor/hooks/subagent-reuse-gate.py` 缩成 Cursor 适配器，行为与测试结果不变。
   验收：用现有手工 payload 重跑 Cursor 上的放行 / 违规 / 假 id 三个用例，结果与拆之前一致。
2. **中立角色源。** 在 `.agent/roles/` 放 `ya-worker`、`ya-reviewer` 的正文（职责、禁区、
   验证基线、锁规则），不含任何模型字段。写生成脚本，从中立源生成各应用格式；先生成
   Cursor 格式并确认与现有 `.cursor/agents/*.md` 逐字节一致（模型行从适配配置合并）。
   生成物视为只读。
3. **新应用适配器。** 按第 4 节选出的方案，在该应用的配置目录写适配器（钩子接线 +
   把它的 payload 转成归一化输入）。适配文件只放在该应用自己的目录里。
4. **探针回归。** 重跑第 2 节全部探针，确认矩阵里每一格都有证据；用无副作用的 OK subagent
   实测一次违规和一次合法续接，确认检测/阻断符合第 4 节预期。
5. **并行规则回归。** 派两个 worker 并行（文件范围不重叠），确认都经 `ya_lock.sh` 排队，
   `ya_lock.sh status` 能看到持有者（要求 P12；不满足则在 prompt 里要求 worker 自己设）。
6. **更新文档。** 把矩阵的新列和适配器位置写回 `subagent-delegation` 的「门禁」一节，
   并在 `.agent/skills/AGENTS.md` 索引里登记本 skill（该文件目前是用户的脱手改动，
   登记前先问用户）。

## 6. 禁区与注意

- 不删除、不改写 `.cursor/` 下现有文件的行为；改动 Cursor 适配器只限于第 5 步 1 的「缩成适配器」，并且要证明行为不变。
- 不把一个应用的拒绝语义当成通用保证。Cursor 上 `deny` 无效是实测结论，别的应用也要实测。
- 探针 subagent 不得读写仓库文件；探针产生的日志只写 `/tmp`。
- 不在没有证据的格子里填「支持」。拿不准写「未验证」，并在备注里写缺什么证据。
- 生成物只读：改中立源和生成脚本，不手改生成出来的各应用文件。
- 每个提交只含一个可验收目标，不要把探针脚本、矩阵更新和适配器混在一个提交里。

## 7. 验收

- 矩阵里目标应用那一列没有「未验证」，或者「未验证」的格子都写明了为什么不影响选定方案。
- Cursor 上三个手工用例（缺声明 / 假 id / 合法续接）结果与迁移前一致。
- 目标应用上同样三个用例按第 4 节选定的方案表现（硬阻断、检测或审计）。
- 根 `AGENTS.md` 的规则没有出现某个应用专有的工具名；专有内容只在适配器和本 skill 里。
