# DSH 工具层

DSH（DeepSeek Harness）**没有 markdown agent 文件**：它不解析 `.cursor/agents/*.md`，也不解析 `*.agent.md`。角色在 DSH 里的落地方式是把 `dsh-tool-subagent` 的一个实例（Cordis YAML 行）挂进组合，用 `persona` / `toolFilter` / `agentOptions` 配置它。

**因此 DSH 派发时必须在 prompt 里指路**：`先读 .agent/agents/common/worker.md 的角色规则与仓库硬规则`——正文不会自动进入子 agent 的上下文。

## 角色映射

| 角色 | DSH 落地 | 现状 |
| --- | --- | --- |
| worker | `subagent` 工具（provider `spawn`，`backgroundMode: continuable`） | base 组合已挂，可直接用 |
| reviewer | 需新增一个实例：`toolName: ya_reviewer`，`toolFilter` 去掉写类工具，`persona` 指向 `common/reviewer.md` 摘要 | 未配置，现在只能靠 prompt 约束「只读」 |
| explorer | 直接可用：`subagent` + prompt 指路 `common/explorer.md`；要机制只读就照下面的示例加 `toolName: ya_explorer` 实例 | 未配置专属实例 |
| plan-harness | 不派发：它是主会话自身的角色，读 `common/plan-harness.md` 即可 | — |

## 默认组合里有什么

base 组合（`@deepseek-ai/dsh-base` 的 `cordis.patch.yml`，行 `subagent` / `subagent-spawn-in-process` / `subagent-fork-in-process` / `tool-subagent-control` / `tool-subagent` / `tool-subagent-fork`）实际挂出：

| 工具 | provider | 形态 | 语义 |
| --- | --- | --- | --- |
| `subagent` | `spawn` | `continuable` | 默认后台，返回 childId；结束时父级收到结算通知；可 `send_message` 续接 |
| `subagent_fork` | `fork` | `one-shot` | 继承父级已完成轮次作初始内容，阻塞等结果 |
| `send_message` / `interrupt_agent` | —（服务） | — | 给直接可继续 child 发消息 / 只停当前轮次 |
| `list_agents` | —（服务） | — | `children` / `descendants`，不打开子日志 |

spawn 子 agent 拿到**空对话**：只继承 cwd、谱系、provider、model、推理强度、输出 token 上限与工作目录，不继承父级任何消息。fork 拿父级已完成轮次的快照作初始内容（进行中的轮次看不到，此后父级新记录也不会到达它）。

## 指定模型

三条路径，按优先级从具体到通用：

1. **每次调用自选**：需要 `modelSelectionSettings: true`（工具实例配置）+ Host `subagent-model-selection`（`enabled` + `allowedModels` 精确 `provider/model` 白名单）。开启后工具 schema 多出 `provider` / `model` / `reasoning_effort`，并注册 `list_subagent_models` 做发现。策略在组合顶层 Session 时读入并**记录进 Session、由子 Session 继承**，之后改设置不影响已记录会话。
   GUI 开关：**插件 → 子智能体 → 模型选择**（与「最大递归深度」「并行数量上限」同页，一次保存）。
2. **实例静态默认**：`agentOptions: { provider, model, reasoningEffort, maxTokens }`，要求 provider 声明 `agentOptions` 能力。
3. **继承**：不指定就继承父级最新已记录请求的 provider / model / 推理强度 / maxTokens。改路由但没显式给强度，会清掉继承来的强度，让新模型解析自己的默认值。

**能力差异（会明确报错，不静默忽略）**：`spawn` / `fork` / DSH SDK 支持 `agentOptions`；ACP、Codex、Claude Code 拒绝。`fork` 后端刻意不暴露子级路由选择——换模型会让继承的父级前缀失去 KV Cache 复用资格。

⚠️ 本仓库当前会话**未开启**模型选择，`subagent` / `subagent_fork` 的 schema 里没有 model 参数。

## 续接与缓存

- `send_message` 是唯一续接口，**没有模型参数**：目标还在驻留时走 inbox（工作目标在最近 step 边界 Steer，空闲目标开新轮次）；目标已释放则按 childId 冷恢复，路由取自持久化 descriptor（provider / model / reasoning effort 是会话事件，不进模型历史）。
- **续接不能换模型**，要换只能新开。反过来，DSH 不会像 Cursor 那样在续接时静默把模型降级成 fast 变体。
- 续接同一条线时，子 agent 的会话前缀只追加不重写 → 命中提示缓存的主要来源就是它。会被破坏的因素：`persona`、`toolFilter`、生成 SDK、provider / model 变化，以及给 fork 子级覆盖路由。
- 用 `cacheReadTokens` / `cacheWriteTokens`（每轮 usage）核对，不要凭感觉。

## 能力边界

- `maxDepth` 默认 `1`（Host 设置，`0` 禁止委派）；数值深度要求 provider 有 `depthLimit`。
- `maxActiveSubagents` 默认 `8`：统计同一主 agent 下所有层级存活的子 agent，容量耗尽时新建 / 冷恢复以 `ACTIVATION_LIMIT_REACHED` 拒绝，**不排队**。
- 驻留只在进程内协调：两个 harness 进程不共享；child → parent 投递要求直接 parent 在线（没有持久化 parent mailbox）。
- 委派权限在创建那一刻快照固定，子 agent 运行时上下文里带声明「不能从内部扩大自己的权限范围」；被拒的操作不要重试，写进回报让父 agent 处理。

## 发现路径

DSH 自动扫描的 skill 根目录是项目 `.dsh/skills` 与 `.agents/skills`，用户 `~/.dsh/skills` 与 `~/.agents/skills`。本仓库的 `.agent/skills/` **不在**扫描列表里，靠根 `AGENTS.md` 的路由进入——派发 prompt 里要写明确的路径，别指望子 agent 自动发现。要让某个 skill 变成自动发现，在 `.dsh/skills` 或 `.agents/skills` 放软链，不要复制正文。

## 挂一个只读 reviewer 实例（示例）

base 组合只挂了可写的 `subagent`；要一个机制上只读的角色，加一行（字段含义按 `dsh-tool-subagent` 的配置目录填，不要照抄这里的工具名）：

```yaml
- id: tool-subagent-reviewer
  name: '@deepseek-ai/dsh-tool-subagent'
  config:
    provider: spawn
    toolName: ya_reviewer
    backgroundMode: continuable
    modelSelectionSettings: true        # 需要每次调用自选模型时
    agentOptions: { provider: <p>, model: <slug> }   # 固定模型时（provider 与 model 一起给）
    persona: <只读审查员的补充提示；正文仍让子 agent 读 .agent/agents/common/reviewer.md>
    toolFilter: [<要从子 agent 移除的写类工具名>]
```

`toolFilter` 是机制保证：被移除的工具不会出现在子 agent 的 schema 里，比在 persona 里写「请不要改文件」可靠。`list_subagent_models` 是全局名，同一个 tool scope 内只能有一个实例拥有模型选择。


## 坑

- 子 agent 空上下文：prompt 必须自足，别写"如上所述"。唯一例外是 `subagent_fork`。
- 结算通知会进父级上下文（一条 user 消息 + 子级结尾文本）；子级先 `send_message` 再结算，父级要同时承担两者。
- 可继续子级默认后台：不要在还依赖结果时省掉 `run_in_background: false`。
