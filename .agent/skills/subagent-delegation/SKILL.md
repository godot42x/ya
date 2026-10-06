---
name: subagent-delegation
description: 派发 subagent 的规则：新开 / 续接（resume）/ 分叉（self）怎么选、模型怎么选、交接 prompt 写什么、与父 agent 并行时的文件归属、断线恢复与结果核对。当需要用 Task 工具委派任务、续接已有 subagent、或 subagent 中断 / 结果待核对时使用。
---

# Subagent Delegation

## 三种启动方式

| 方式 | 参数 | 上下文 | 适用 |
| --- | --- | --- | --- |
| 新开 | 不带 `resume` | 空白，只看得到 prompt | 换了一块代码、换模型、旧上下文里有被推翻的方案 |
| 续接 | `resume: <agent id>` | 保留它读过的文件和做过的判断 | 同一块代码上的连续任务：返工、补一处、收尾 |
| 分叉 | `resume: "self"` | 继承父 agent 完整对话 | 背景很难转述、且父上下文不算太长 |

续接的限制：
- 只能续接**已结束**的 subagent；运行中的要 `interrupt: true`，只在用户明确要打断时用。
- 沿用原模型，不能换。要换模型只能新开。
- 目标必须在当前会话里还能找到。断线 / 跨会话的 agent 会报 `Composer not found for resume`，这时只能新开并做状态交接（见下文）。

默认：同一条线上的后续任务优先续接已结束的 subagent，不要每次都新开。

续接必须由当初派发它的会话发起。侧聊续接主聊天的 subagent 时，Cursor 会拒绝（`Subagent parent mismatch for resume`），要在侧聊里新开。

## 按线复用（subagent 池）

| 线 | 典型范围 |
| --- | --- |
| Editor | `Applications/GameEditor`、设计器、面板、调试页 |
| 渲染 | RHI、render graph、2D/精灵 pass、相机投影与提取、字体资源（图集、栅格化） |
| GUI 底层 | WidgetTree、布局、文本栅格、compose |
| Gameplay | 脚本绑定、Lua、场景生命周期、Game UI host、示例项目 |

跨线任务（字体缓存碰渲染资源 + GUI + 编辑器调试页）归给改过其主体的那个 agent。

续接时：
- prompt 开头先让它跑 `git log <它上次看到的提交>..HEAD --stat` 和 `git status`：别人（用户、其他 agent）改过的文件，它记忆里的内容已过期，要重新读。
- 本轮的决策、禁区、验证基线仍要写全，不要假设它还记得。
- 池子里同一条线同时只有一个任务在跑；有构建的任务本来就串行（一个仓库一份 xmake 构建）。

换新的而不是续接：
- 同一个 agent 已连续做过 4～5 个任务，或上下文明显很长；
- 它的方案被推翻过，旧结论会干扰；
- 要换模型；
- 报 `Composer not found`（断线或跨会话），按「断线恢复」交接。

表只在当前会话有效，agent id 不会跨会话保留。

## 缓存

提示词缓存按前缀匹配，并有过期时间（通常分钟级）。以下是按机制推断：
- 新开：公共前缀（系统提示、工具定义）可能命中，任务上下文要重新读、重新算。
- 续接：前缀不变，间隔短时大概率命中；间隔超过过期时间同样失效。

所以连续的小任务趁热续接；隔了很久、或上下文已经很长时，新开一个干净的反而更省。

## 模型

- 只能用会话 `<available_subagent_models>` 列表里的 slug；用户说的模型不在列表里，就告诉用户，不要替换。
- 默认用非 fast 版本（如 `grok-4.7-high`），`-fast` 只在用户明确要时用。用户反馈过 fast 版本产出的实现偏脏。
- 用户点名模型后，同一任务的后续 subagent 沿用该模型。
- **Task 的 `model` 传 `grok-4.7-high` 也会得到 fast（2026-10-03 查实）。** Cursor 日志 `fixupModelConfig: populating selectedModels from modelName` 显示 slug 的 `-high` / `-high-fast` 后缀被丢掉，只剩 `grok-4.7`，composer 的参数数为 0；请求时用目录默认值补成 3 个参数，随后 `Healed stale per-composer parameters` 把它们写进 composer。state.vscdb 里所有经 Task 创建的 grok-4.7 subagent 都是 `reasoning_effort=high, fast=true`，`-fast` 与否无区别；用户自己开的 composer 才是 `fast=false`。界面标签在第一次运行结束、heal 之后才变成 "High Fast"，所以新开的 agent 刚创建时看起来是对的。
- **续接为什么变 fast（2026-10-05 实验，结论分「已证实」和「待证实」）。**
  - 已证实（日志）：subagent 首次运行时 composer 存档参数是空的，请求用保存的偏好补全（`buildRequestedModel` 此时 `composerModelName=grok-4.7`、`resolvedParams=3`）；首次运行结束约 4 秒后 `Healed stale per-composer parameters`（`beforeCount=0 afterCount=3`）按模型目录默认变体写入 `fast=true`，**不看用户偏好**；之后每次续接 `buildRequestedModel` 读到的是这份已写入的值（`composerModelName` 同时变成 `grok-4.7-high`）。新开与续接在本地可见状态上的差别只有这一步，与用户「返工前无 fast、续接后有 fast」的观察吻合。
  - **已证实（2026-10-05 用量 CSV）：续接一定变 fast，显式传 `model` 救不了。** 探针结果：两个新开的 subagent 首次运行都是 `grok-4.7-high`（02:29:45 / 02:29:46）；一小时后两次续接都是 `grok-4.7-high-fast`（02:30:51），包括显式传了 `model: grok-4.7-high` 的那一个。与自愈把 `fast=true` 写入存档、续接读存档的时间线吻合。
  - **续接还会打穿缓存。** 变体从非 fast 换到 fast，缓存不共享：当天 18 条 fast 记录里有 5 条几乎没有 cache read（例如 14:35 是 19 万 uncached 输入对 5 千 cache read），合计约 69 万 uncached 输入；32 条非 fast 记录里只有 1 条这样。也就是说续接不但单价更高，连「复用上下文」本身的缓存收益也没了。（这是由用量数据推断的因果，不是 Cursor 官方说明。）
  - 撤回：先前「在 agent 文件里加 `context=256k` 后新 worker 不再 fast」的结论不成立——当时读到的空参数是自愈前的状态。`.cursor/agents/*.md` 括号里的参数（`fast=false` 等）看起来没有进入 composer 存档（首次运行后存档仍是空），但这是读存档的推断，没有直接实测。
- **Task 的 `model` 参数不接受括号语法**（传 `grok-4.7[...fast=false]` 报 `Invalid model selection`），只接受会话 `<available_subagent_models>` 里的 slug。官方说的「续接时再传一次 `model`，IDE 会尊重」，在我们这里只能传 `grok-4.7-high` 这种 slug；本地实测带与不带，composer 存档和请求构造日志完全一样，没看到差别。
- 官方对应 issue（均为已知 bug、未给修复时间）：[续接回落到 fast](https://forum.cursor.com/t/resume-to-first-party-non-fast-models-redirects-to-fast-variants/166470)（与本问题一致，推理强度也失效）、[续接回落到父会话模型](https://forum.cursor.com/t/resuming-a-subagent-in-cursor-ide-switches-it-to-the-parent-agent-s-model-instead-of-keeping-the-subagent-s-original-model/165649)（官方绕法：续接时显式再传 `model`，或不续接直接新开）、[续接用错模型（3.5.3 修过后回归）](https://forum.cursor.com/t/subagent-resume-runs-an-incorrect-model/160590)、[模型选择不被尊重](https://forum.cursor.com/t/subagent-model-choice-not-respected/163645)、[CLI 变 fast](https://forum.cursor.com/t/cli-subagents-changing-model-to-composer-2-5-fast-mode-by-itself/162752)。文档：[Subagents 的 Model parameters](https://cursor.com/docs/subagents)。
- **对「优先续接」的影响。** 续接已证实是 fast 且缓存失效，在 Cursor 修复前，用户禁止 fast 时**不要续接 Grok 4.7 subagent**，一律新开并在 prompt 里交接（`## 新开时的交接 prompt`）；「按线复用」对 Grok 4.7 暂时停用。只有用量页证实某个模型续接不回落时才恢复。
- Task 工具没有 fast 参数，续接 / 新开都躲不掉。（2026-10-07 拍板：不再因此停派 Grok 4.7——派发显式传 `model` slug、接受其 fast 变体，靠 `## 结果核对` 兜底，并尽量少派。若将来给 `ya-worker`/`ya-reviewer` 配好 `subagentModelOverrides` 并探针确认非 fast，可撤掉这个妥协。）

## 项目内 agent（`.cursor/agents/`）

与本仓库相关的 subagent 定义放在仓库 `.cursor/agents/`，随仓库走；不放用户目录。

| agent | 用途 | 模型 |
| --- | --- | --- |
| `ya-worker` | 实现 / 返工 / 收尾，可改文件、跑构建测试 | `grok-4.7[reasoning_effort=high,fast=false]` |
| `ya-reviewer` | 只读审查：diff 范围、用户改动保留、测试复现、实现质量 | 同上，`readonly: true` |
| `explore`（内置） | 不用：只读探索也走 `ya-reviewer` | 不受 frontmatter 约束 |

### 硬规则：只用标定模型的项目 agent，且只新开（用户 2026-10-07 重申）

- 每一次 `Task`——实现、只读盘点、审查、交接、甚至一句「停止」——只能是 `.cursor/agents/` 里在 frontmatter 标好模型的类型：实现用 `ya-worker`，只读用 `ya-reviewer`。
- 不用 `explore`、`generalPurpose` 等内置类型：它们沿用父 agent 模型或目录默认变体，不受 frontmatter 约束。
- **不续接**（不传 `resume`）：Grok 4.7 续接已证实变 fast（见 `## 模型`）。同一条线的后续任务新开一个 `ya-worker`，在 prompt 里按 `## 新开时的交接 prompt` 交接，写清上次提交号、已完成项、本次方案与禁区。
- 已经在跑的类型不对的 agent：让它自然结束，不续接它去叫停，它的产出不采用。
- 已经在跑但模型不对的（未传 `model` 落到默认模型）：同样让它跑完、不续接去叫停，产出经严格结果核对后可用（用户 2026-10-07：跑完就行，后续避免）。
- 已经由续接（fast）产出的改动：提交前另开 `ya-reviewer` 严格审查，或由新开的 `ya-worker` 重做，由用户决定。

- **派发时必须显式传 `model: grok-4.7-high`（用户 2026-10-07 拍板）。** 旧结论「靠 agent 文件 frontmatter 的括号 `model`、派发时不传 `model`」已证伪：Task 不解析 frontmatter 的括号语法，不传 `model` 会回落到父会话默认模型（2026-10-07 实测 3 个派发全部落在 opus-5.5 上，详见 request 0bef0d03 的调查）。显式 slug 会被自愈成 fast（见 `## 模型`），用户已捏着鼻子接受；产出偏脏的风险由 `## 结果核对` 兜底，同时尽量避免不必要的派发。
- 表中 agent 文件 frontmatter 的 `model` 只作意图记录，实际派发以 Task 的显式 `model` 参数为准。
- 按**角色**分，不按模块分：模块知识在 skills 里，agent 按需读。角色少，前缀稳定，缓存可复用。
- 通用规则（环境坑、提交约定、回报格式、开工前 `git status`）写在 agent 正文里，**派发 prompt 只写本任务的方案、步骤、验证基线、禁区**。
- 不要把每天都在变的内容（用户未提交文件清单）写进 agent 正文。agent 开工前自己跑 `git status`，开工前已存在的改动一律视为不是自己的；prompt 只补「父 agent 正在并行改的文件」。
- 长任务用 `run_in_background: true`，并读 subagent 的 transcript 判断是否在推进；不要阻塞等待（阻塞式派发看不到它是否已出错或被终止）。

## 并行派发

- 能并行的前提：各 worker 的**文件范围互不重叠**（含生成物和共用的场景 / 资产文件）；有重叠的事项合并给同一个 worker 按顺序做，或串行。
- 共用一个构建目录，构建 / 测试 / 运行同一时间只能一份：prompt 里要求 worker 把这些命令包进
  `Script/agent/ya_lock.sh run -- <命令>`，先 `export YA_AGENT_NAME=<任务名>`，用 `Script/agent/ya_lock.sh status`
  看谁占着（规则见 `ya-build` skill「并行 agent 的独占构建锁」）。长驻进程不放进锁。
- 提交仍由父 agent 做：要求每个 worker 报告按「一个可验收目标一个提交」分好的文件清单，父 agent 按路径分别暂存。
- 并行 worker 同时改同一类共享文件（如示例场景）是冲突来源，派发前先拆开顺序。

## 新开时的交接 prompt

subagent 看不到用户消息和父 agent 的历史，prompt 要自足：

1. **仓库与必读**：仓库路径；先读根 `AGENTS.md`，再列出本任务相关的 skill / plan 文件。
2. **背景**：相关提交 hash（让它自己 `git show`）、当前状态、上一个 agent 留下的未提交改动和产物路径。
3. **已定方案**：用户已拍板的决策原样写进去，不要让它重新选。
4. **步骤**：按顺序列出，每步写清验收标准。
5. **验证**：构建、过滤测试、全量测试（上次的通过数作为对照）、`rg` 删除检查。
6. **提交**：一个可验收目标一个提交；只 `git add` 本任务文件；提交前 `git diff --cached --stat` 自检。
7. **禁区**：用户自己的未提交改动逐个列出（路径），以及父 agent 正在并行改的文件。
8. **停止条件**：做不成或发现方案本身有问题就停下来如实报告，不硬做、不拿别的结果凑数。
9. **回报内容**：提交 hash 与文件清单、测试结果、关键数字、偏离项与未完成项。

## 与父 agent 并行

- 开工前按文件划分归属：subagent 改哪些、父 agent 改哪些，写进 prompt。
- 有交叉的路径（比如 subagent 要归档目录，父 agent 的文档引用该目录）：让 subagent 只报告受影响的引用，父 agent 等它完成后统一修，再提交。
- 父 agent 自己的改动在 subagent 完成前不提交，避免和它的提交交错。
- 同一工作区共用一个暂存区：subagent 运行期间父 agent 不要 `git add` / `git mv`（`git mv` 会直接暂存重命名），
  否则会被 subagent 的下一次 `git commit` 带走。需要移动文件时先 `git mv`，再 `git reset -q -- <路径>` 撤出暂存区，提交时再暂存。
- 同一仓库同时只跑一份 xmake 构建；多个要构建的任务交给一个 subagent 顺序做，或者放到独立 worktree。

## 断线恢复

subagent 报 `Connection failed repeatedly` 等错误结束时：
1. `git log` / `git status` / `git worktree list`：哪些已提交、哪些改动留在工作区、有没有遗留 worktree。
2. 读 transcript 末尾：停在哪一步、产物（截图、日志）放在哪。
3. 先尝试续接；失败就新开，把第 1、2 步查到的状态写进交接 prompt（已完成 / 已改未提交 / 已有产物 / 已知问题）。
4. 区分工作区里哪些是它留下的、哪些是用户自己的，后者写进禁区。

## 结果核对

subagent 的结论不直接转述给用户，先核对：
- `git show --stat <hash>`：文件范围是否符合任务，用户的改动是否仍未提交、原样留在工作区。
- 产物亲自看：截图用 Read 打开，数字和日志对得上。
- 删除类目标用 `rg` 复查。
- 实现质量：测试能过不等于干净。看 diff 有没有为 debug 需求加 release 状态、靠约定维持的不变量、每帧分配 / 排序、同一规则复制多份。

## 沙箱注意

- `ps`、全量 `ya-testing`、编辑器 / runtime 截图通常要在沙箱外跑（`required_permissions: ["all"]`），写进 prompt。
- 跑编辑器前确认没有残留进程，否则会撞实例锁（`already running as pid`）。
- 跨提交对比用 `git worktree add /tmp/<name> <hash>`，结束后 `git worktree remove`；子模块目录只读时先改权限再删。
