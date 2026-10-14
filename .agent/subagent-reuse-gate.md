# Subagent 复用与派发门禁规则

> **目的**：避免重复派发新 agent 导致缓存失效，确保同领域任务复用已有 agent 的上下文。
> **触发场景**：2026-10-14 错误——派发 G2/G3/G4/编辑器本地化时派了 4 个新 agent，而应该续接已有的 `agent_d74dcea0`（RPG 线）和 `agent_3cf3d092`（本地化线），白白浪费了前面建立的上下文缓存。

---

## 门禁规则（强制执行）

### Rule 1：派发前必须完成的检查清单

在调用 `Agent` 工具前，**必须**回答以下问题：

#### 1.1 领域识别
这个任务的**领域**是什么？
- RPG 内容（战斗/成长/UI/内容）
- 本地化（游戏侧/编辑器侧）
- UX 设计（战旗 HUD/菜单）
- 渲染管线（合成/后处理/阴影）
- 构建工具（XMake/测试/自动化）
- GUI 框架（控件/布局/绑定）
- 资源系统（加载/生命周期）
- 其他

#### 1.2 缓存检查
本会话是否已有该领域的 agent？
- **查看方式**：回顾本会话之前的 `Agent` 工具调用结果，记录 `agentId`
- **判断标准**：该 agent 是否已读过相关的 plan/skill/代码？

#### 1.3 决策
- **若有可复用 agent** → **必须用 `SendMessage` 续接**，**禁止**派新 agent
- **若无** → 才允许派新 agent

---

### Rule 2：续接 agent 的 prompt 模板

```
你是 agent_XXXXXXXX，之前已完成 [任务A的简述]。

现在继续同领域的新任务：[任务B]

你已经读过的上下文（**不要重读**）：
- `.agent/plan/XXX/plan.md` §Y
- `.agent/skills/XXX/SKILL.md`
- `Engine/Source/.../XXX.cpp`（已分析过的代码）

只需要增量工作：
1. [步骤1]
2. [步骤2]
...

文件范围（用于锁与并行）：
- [本次任务会改的文件]

提交格式：
[module] message
```

**关键点**：
- 明确告诉 agent "你已经读过 X，不要重读"
- 只给增量指令，不重复背景
- 文件范围仍需明确（用于并行冲突判断）

---

### Rule 3：新 agent 的触发条件（白名单）

**只有以下情况才派新 agent**：

#### 3.1 领域完全不同
- 示例：之前只有 RPG 线，现在要做**渲染管线**（完全不同的 plan/skill/代码）
- 判据：两个领域的 plan 目录不同、skill 不重叠

#### 3.2 现有 agent 已完成且会话结束
- 示例：收到 `<task-notification>` 显示 agent completed
- 判据：agent 的 output 已返回最终结果

#### 3.3 文件范围有冲突需要隔离
- 示例：G2 与 G3 都改 `Party.lua`，需要各自在独立分支工作
- 判据：两个任务的文件范围有交集，且不能串行

#### 3.4 探索性任务（调研/设计/评估）
- 示例：本次的三个初始 subagent（战旗 UX 调研、本地化调研、AI Native 探索）
- 判据：任务是"调研并产出方案"，不是"实施已有方案"

---

### Rule 4：复用场景示例

| 场景 | 错误做法（❌） | 正确做法（✅） |
| --- | --- | --- |
| G4 任务系统（RPG 线） | 派新 agent 从头读 plan | 续接 `agent_d74dcea0`（已读过 rpg-prototype/plan.md） |
| 编辑器 P0-P3（本地化线） | 派新 agent 重新调研 | 续接 `agent_3cf3d092`（已有完整 P0-P3 方案） |
| 战旗 UX 方案 C 实施 | 派新 agent 重读代码 | 续接 `agent_17fe88af`（已分析 BattleHUD/BattleFx） |
| C1 内容（RPG 线） | 派新 agent 读 plan | 续接做 G4 的 agent（已读过 Battle.lua） |
| 渲染管线 D3（新领域） | ✅ 派新 agent（与 RPG 无关） | — |

---

## 实施方式

### 方式 1：在派发前手动检查（当前）
- 主 agent（我）在调用 `Agent` 工具前，先回顾本会话的 agent 列表
- 逐一对照 Rule 1 的检查清单
- 决策后再调用工具

### 方式 2：工具级强制（理想，需要平台支持）
- `Agent` 工具增加 `reuse_agent_id` 参数
- 若本会话已有同领域 agent，工具**拒绝**派发新 agent，返回错误提示"请用 SendMessage 续接 agent_XXX"
- 需要平台侧改动，当前不可用

---

## 违反规则的后果

### 代价
- **token 浪费**：新 agent 从零读文件，浪费 prompt cache
- **一致性风险**：新 agent 可能读到不同版本的 plan/code，产生不一致的理解
- **时间成本**：新 agent 的"读上下文"阶段可能占总时长的 30-50%

### 案例
- **2026-10-14 本次错误**：派发 4 个新 agent，预估浪费 ~50k tokens（每个 agent 重读 plan/skill）
- 若正确续接，只需 ~10k tokens（增量指令）

---

## 记忆固化

**写入位置**：`.agent/skills/subagent-delegation/SKILL.md` 的"复用与缓存"小节（已有 skill，追加本规则）

**触发条件**：任何需要派发 subagent 的场景

**验收**：下一次派发 subagent 时，主 agent 必须先口述"检查是否有可复用 agent"，再决策派发或续接。

---

## 立即行动

- [x] 建立本规则文档
- [ ] 追加到 `.agent/skills/subagent-delegation/SKILL.md`
- [ ] 下一次派发前测试（口述检查清单 → 决策 → 派发/续接）

---

## 附：当前会话的 agent 列表（供后续复用）

| agentId | 领域 | 已读上下文 | 状态 |
| --- | --- | --- | --- |
| `agent_17fe88af` | 战旗 UX 设计 | BattleHUD/BattleFx/业界 SRPG 调研 | 已完成（产出方案 C） |
| `agent_3cf3d092` | 本地化 | LocalizationManager/EditorSurface/P0-P3 方案 | 已完成（产出完整方案） |
| `agent_d74dcea0` | AI Native + RPG 梳理 | rpg-prototype/plan/progress/feature_matrix | 已完成（产出报告） |
| `agent_c01d2c53` | RPG G4 任务系统 | 运行中 | 后台运行 |
| `agent_731284c0` | RPG G2 装备背包 | 运行中 | 后台运行 |
| `agent_598ae6be` | RPG G3 队伍同伴 | 运行中 | 后台运行 |
| `agent_f1028ba7` | 编辑器本地化 P0-P3 | 运行中 | 后台运行 |

**后续任务派发建议**：
- **C1 内容**（RPG 线）→ 等 G2/G3/G4 完成后，续接其中一个（它们都已读过 Battle.lua）
- **战旗 UX 方案 C 实施** → 续接 `agent_17fe88af`（已有完整设计）
- **docs lint 符号检测** → 续接 `agent_d74dcea0`（已分析 .agent/ 体系）
