# TODO

> 一眼看清进度。细节在 `plan.md` / `progress.md`。

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

- [ ] A3.1 出逐节对照表（原 861 行每一节的唯一归属）
- [ ] A3.2 `SKILL.md` 瘦身至 ≤300 行
- [ ] A3.3 `references/` 收纳控件 / 布局 / 动画 / DSL / Style / Host
- [ ] A3.4 `GameEditor chrome` + Dock 权责迁出为 Application 层 skill
- [ ] A3.5 全库引用改写同批提交
- [ ] A3.6 顺带处理另两个超预算件：`ya-build`（367 行）、`render-arch`（311 行），
      并清空 lint 的 `SIZE_EXEMPT` 表

## A4 定点核对

- [x] A4.1 清除 skill 内硬编码行号（唯一真命中在 `speedscope-analysis`，属示例输出，已按代码块豁免）
- [x] A4.2 修正已改名符号：`RenderImage` → `RenderTexture`（3 处，`cpp-style` ×2、`gui-framework` ×1）

## 验收

- [x] `python3 Script/ya_agent_docs_lint.py` 退出码 0（剩 3 项已知体量债，见 A3.6）
- [ ] `gui-framework/SKILL.md` ≤300 行，`references/*.md` ≤300 行
- [x] 目录 ↔ 索引双向 100% 覆盖（`plan/` 除外）
- [x] 入口文档不再出现第二份优先级列表
