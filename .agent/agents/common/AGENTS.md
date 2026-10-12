# common —— 工具无关层

只放角色的**正文**：这个角色负责什么、开工前做什么、仓库硬规则、怎么验证、怎么回报、不能做什么。

这一层**不写**：

- 工具名或工具参数（`Task`、`subagent`、`send_message`、`resume` …）
- 模型 slug 与推理强度
- 某工具的文件路径（`.cursor/agents/`、`*.agent.md`）
- 某工具的沙箱 / 审批 / 权限参数

这些属于 `../<tool>/AGENTS.md`。判断标准：**换一个 agent 工具就要改的句子，不属于这一层。**

## 文件

- `worker.md`：被委派的实现者
- `reviewer.md`：只读审查者
- `explorer.md`：只读探索者
- `gamedev.md`：通用引擎开发助手
- `plan-harness.md`：长任务计划执行代理

## 写作规则

- 引用仓库资产（`AGENTS.md`、`Script/agent/ya_lock.sh`、`Generated/*`、`.agent/skills/`）用仓库相对路径，不用任何工具的引用语法。
- 角色名用中性的 `worker` / `reviewer` / `explorer`；工具层再映射成 `ya-worker`、`ya_reviewer`、`ya-explorer` 之类的具体名字。
