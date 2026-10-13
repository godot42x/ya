# Agent Workspace

先读仓库根 `AGENTS.md`，这里只负责二级路由。默认不要一次性加载全部内容。

## 入口原则

- 任务明确：直接进入对应单个 skill。
- 任务不明确：先读 `./skills/soul/SKILL.md` 做澄清。
- 只有遇到历史坑、回归、相似故障时，才读 `./memories/*.md`。
- `agents/` 是特定工作模式说明；只有确实需要时才打开。
- `prompts/` 是模板资产，不属于默认上下文。

## 目录

- `./skills/AGENTS.md`：**skill 索引与路由唯一真源**（含优先级顺序）
- `./memories/AGENTS.md`：memory 索引
- `./agents/AGENTS.md`：可选代理角色索引
- `./prompts/AGENTS.md`：提示模板索引
- `./plan/`：阶段性计划与进度，先读 `./plan/AGENTS.md`

## 路由

按 `./skills/AGENTS.md` 的索引与优先级选一个主 skill，不要在这里另立列表。
只有遇到历史坑、回归、相似故障时，才按 `./memories/AGENTS.md` 补读 memory。

## 维护规则

- 稳定规则优先写 skill。
- 历史坑优先写 memory。
- 阶段性状态优先写 plan。
- 路由与 skill 清单只在 `./skills/AGENTS.md` 维护，其他入口只留指针。
- 若某条信息不是“当前默认路径”，不要留在顶层入口。
- 分层：L1 入口只路由 / L2 索引是唯一真源 / L3 `SKILL.md` 只讲契约 /
  L4 `references/*.md` 放细节。`SKILL.md` 与 `references/*.md` 各不超过 300 行。
- 只写符号名与路径，不写行号；相对链接必须能从本文件所在目录解析。
- 改动后用 `python3 Script/ya_agent_docs_lint.py` 自检。

## 计划执行与提交诚信

- 不得通过频繁小提交、占位实现、无关清理或重复拆分制造虚假进度。
- 每个计划 checkpoint 必须绑定一个明确的验收目标，并同时包含对应代码、测试/验证和 progress 记录；仅修改 progress 不算完成。
- 发现实现偏离用户目标时，必须停止继续扩散改动，先做审计并报告；不得用新的提交掩盖偏离。
- 提交前必须核对 `git diff --stat`、实际测试结果、计划条目和未纳入范围的工作区改动。
