# agent files 维护（A1–A4）

> 建立：2026-10-13
> 关联：`./.agent/AGENTS.md`、`./.agent/skills/AGENTS.md`、`./.agent/memories/AGENTS.md`、
> `Script/ya_module_lint.py`（守卫形态参照）
> 状态：A1 开工中

## 0. 问题

`.agent/` 的三类问题已经量出来了，都不是"内容写错"，而是**组织**问题：

1. **体量与分层不成比例**：`skills/gui-framework/SKILL.md` 一个文件 861 行 /
   98.5 KB / 约 2.8 万 token，约等于全部 28 个 memory 之和；它没有
   `references/` 分层，读者要么整载 2.8 万 token，要么漏读。20 个 skill 里
   只有 `image-gen` 用了完整分层（`SKILL.md` + `references/` + `scripts/`）。
2. **路由真源有三份且互不一致**：根 `AGENTS.md:45` 的优先级串 12 项、
   `.agent/skills/AGENTS.md:7` 10 项（缺 gui-framework / scene-object-boundary）、
   `.agent/AGENTS.md:25-33` 第三个 6 项列表。"该读哪个文件"的成本直接来自这里。
3. **索引与内容腐坏**：目录里有、索引里没有；相对链接层深错；同一主题两份文件。
   量得的结果见 `progress.md`。

一个必须先说清的边界：**符号层的腐坏大多是误报**。逐条对源码做词边界核验后，
所谓"13.6% / 16.4% 失效"里，`C7595`/`C3861` 是 MSVC 错误码、`AEAUApp`/`AEAVApp`
是修饰名、`FD_SET` 是 POSIX 宏。真实腐坏是个别改名（如 `RenderImage`）加上
易腐的硬编码行号。所以本轮**不重写概念内容**，只修组织、索引与可解析性。

## 1. 边界（不做的事）

- **不修 `plan/archive/` 的腐坏**：`plan/AGENTS.md` 自己声明它是历史工件，
  21% 的路径失效率是设计如此。249 文件 / 4 MB 不按文档标准治理。
- **不做符号级 API 清单**：手写签名必腐（`.agent/misc/COMPLETE_CODE_INDEX.md`
  就是先例，它引用的 `Engine/Source/Scene/Scene.h` 已迁走）。
- **不用一个巨型 markdown 替代现有结构**：那只是换个形式重演同一个问题。
- **不改 `plan/` 里活跃线的状态描述**：各线由自己的 owner 维护。
- **不碰用户的未提交改动**：本仓库在会话期间有并行 agent 活动（HEAD 会移动），
  提交一律用显式 pathspec。

## 2. 四个批次

### A1 结构一致性（便宜、低风险、立刻降入口噪音）

- 三张路由表合一：以 `.agent/skills/AGENTS.md` 为唯一真源（它是 skills 索引），
  另两处只留指针不重复列表。
- 索引补全：`skills/AGENTS.md` 20/20；根 `AGENTS.md` 的 Skill Routing 补齐；
  `memories/AGENTS.md` 补 3 条漏登。
- 修 4 处相对链接层深错误（`../memories/` 应为 `../../memories/`）。
- 消除 `windows-dll-boundary` 双份（skill 21 行 vs memory 225 行）。
- 处置死重：`.agent/misc/`（10 文件 55 KB，最后修改 2026-04-16，内含已腐的
  代码索引）与 `.agent/prompts/`（8 个 speckit 模板，无引用链）。

### A2 立规矩（便宜、决定长期）

- 体量预算：`SKILL.md` 与 `references/*.md` 各 ≤300 行 / ≤12 KB，超了下沉。
- 引用纪律：只写符号名与路径，**不写行号**；相对链接必须能从当前文件解析。
- 新增 `Script/ya_agent_docs_lint.py`：机械检查（索引双向覆盖、路径可解析、
  相对链层深、体量预算、memory 索引覆盖），接进 `Script/ci.sh`。
- 分层规则写进 `.agent/AGENTS.md` 维护规则（L1 入口只路由 / L2 索引唯一真源 /
  L3 `SKILL.md` 只讲契约 / L4 `references/` 放细节）。

不修机制就一定会重新长回来——本次审计挖出的失效引用就是这么攒出来的。

### A3 拆 `gui-framework/SKILL.md`（最大件）

- `SKILL.md` 只留模块地图 / 责任规则 / 路由（≤300 行）。
- WidgetTree 模型 + 基础控件权责 + 交互契约 + 布局契约 → `references/`。
- 动画 + 静态 DSL + Style/Theme → `references/`。
- **`GameEditor chrome`（166 行 / 16 KB，最大单节）迁出**：它是
  `Engine/Source/Applications/` 层的产品内容，住在 framework skill 里是作用域越界。
- Host + render surface + Render2D pass slot → `references/`。
- 工作流 / 验收单独成节。

### A4 定点核对（很小）

- 清除硬编码行号（`subagent-delegation/SKILL.md` 2 处、
  `pointer_session_lost_release.md` 2 处等）。
- 修正已改名符号（`RenderImage` 家族）。

## 3. 验收判据

- A1：目录与索引双向 100% 覆盖（`plan/` 与 `plan/archive/` 除外）；
  `python3 Script/ya_agent_docs_lint.py` 退出码 0；
  `AGENTS.md` + `.agent/AGENTS.md` 合计不再出现第二份优先级列表。
- A2：lint 脚本能对**故意植入**的坏引用报错（正反用例都验）。
- A3：`gui-framework/SKILL.md` ≤300 行；每个 `references/*.md` ≤300 行；
  原 861 行的每一节都能在新结构里找到唯一归属（逐节对照表）；
  GameEditor 内容不再出现在 `skills/gui-framework/` 下。
- A4：`rg` 全库无 `SKILL.md` 内的裸行号引用。

## 4. 回归风险

- 拆分会改变 skill 的读取路径，`AGENTS.md` 与各 skill 里的交叉引用要同步，
  否则产生新的悬空链接。A3 必须与引用改写同批提交。
- 路由表合一后，旧的 `.agent/skills/AGENTS.md` 若被外部（其他工具 / 用户剪贴板）
  依赖，需保留文件名不变——本轮只改内容结构，不改文件名。
