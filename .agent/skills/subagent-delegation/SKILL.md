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

## 缓存

提示词缓存按前缀匹配，并有过期时间（通常分钟级）。以下是按机制推断：
- 新开：公共前缀（系统提示、工具定义）可能命中，任务上下文要重新读、重新算。
- 续接：前缀不变，间隔短时大概率命中；间隔超过过期时间同样失效。

所以连续的小任务趁热续接；隔了很久、或上下文已经很长时，新开一个干净的反而更省。

## 模型

- 只能用会话 `<available_subagent_models>` 列表里的 slug；用户说的模型不在列表里，就告诉用户，不要替换。
- 默认用非 fast 版本（如 `grok-4.7-high`），`-fast` 只在用户明确要时用。用户反馈过 fast 版本产出的实现偏脏。
- 用户点名模型后，同一任务的后续 subagent 沿用该模型。

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
