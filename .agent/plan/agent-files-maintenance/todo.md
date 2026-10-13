# TODO

> 一眼看清进度。细节在 `plan.md` / `progress.md`。
> **状态：A1–A4 全部完成（23/23）**，落地于 `5187d22c` + `f2684e06`；
> 下方"仍然成立的遗留"两项目标（语义复核 / `plan/archive`）明确不在本计划范围。

## A1 结构一致性

- [x] A1.1 三张路由表合一（`skills/AGENTS.md` 为唯一真源，另两处留指针）
- [x] A1.2 索引补全：`skills/AGENTS.md` 20/20、`memories/AGENTS.md` 27/27
- [x] A1.3 修相对链接层深错误（原 4 处 + lint 又查出 6 处，共 10 处）
- [x] A1.4 消除 `windows-dll-boundary` 三处重复（归并进 `cross-platform` 规则 6）
- [x] A1.5 处置死重：删除 `.agent/misc/`（11 文件）与 `.agent/prompts/`（9 文件）

## A2 立规矩

- [x] A2.1 `Script/ya_agent_docs_lint.py`（索引覆盖 / 链接可解析 / 体量预算 / 行号引用 / 路由唯一真源）
- [x] A2.2 接进 `Script/ci.sh`
- [x] A2.3 lint 正反用例验证（注入 4 类缺陷必须全部报错）
- [x] A2.4 体量预算与引用纪律写进根 `AGENTS.md` Documentation Policy 与 `.agent/AGENTS.md`
- [x] A2.5 L1–L4 分层规则成文

## A3 拆 `gui-framework/SKILL.md`

- [x] A3.1 逐节对照表（按 H2 机械抽取，18 节各有唯一归属）
- [x] A3.2 `SKILL.md` 861 → 158 行
- [x] A3.3 `references/` 7 份：widget-tree / layout-and-controls / animation-and-dsl /
      style-theme / dock / host-and-render / interaction-contract
- [x] A3.4 `GameEditor chrome` + Dock 产品侧迁出为 `skills/game-editor/SKILL.md`（Application 层）
- [x] A3.5 引用改写（`AGENTS.md`、`skills/AGENTS.md`、跨 skill；references 内层深度 +1 已改）
- [x] A3.6 顺带处理另两个超预算件：`ya-build` 366 → 107 行（+3 refs）、
      `render-arch` 310 → 65 行（+3 refs）；`SIZE_EXEMPT` 已清空为空 dict

## A4 定点核对

- [x] A4.1 清除 skill 内硬编码行号（唯一真命中在 `speedscope-analysis`，属示例输出，已按代码块豁免）
- [x] A4.2 修正已改名符号：`RenderImage` → `RenderTexture`（3 处，`cpp-style` ×2、`gui-framework` ×1）

## 验收

- [x] `python3 Script/ya_agent_docs_lint.py` 退出码 0（**0 violation / 0 debt**）
- [x] 全部 21 个 skill 的 `SKILL.md` ≤300 行；最大 243 行（`code-reorganize`）
- [x] 目录 ↔ 索引双向 100% 覆盖（`plan/` 除外）
- [x] 入口文档不再出现第二份优先级列表
- [x] 内容零丢失：三个拆分对象逐行比对，唯一差异是刻意修正的链接层深
