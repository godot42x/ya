# App / GUI 边界迁移 Session Checklist

> 更新时间：2026-08-14
> 作用：保证这条迁移线每轮都按 no-behavior、单批次、可验证的方式推进。

## 每轮开工前

1. 先读：
   - `plan.md`
   - `todo.md`
   - `progress.md`
   - `feature_matrix.json`
2. 再读本计划的输入工件：
   - `owner-model.md`
   - `directory-charter.md`
   - `capability-appform-mapping.md`
   - `nativewindow-api-triage.md`
   - `directory-target-include-audit.md`
3. 看工作区状态：
   - `git status --short`
   - 必要时 `git log --oneline -n 10`
4. 确认当前只推进一个迁移批次或一个 file-level audit；不要一轮里同时开两个批次。
5. 若本轮要动目录或 target，先确认不会把 window 语义重新塞回 `App`，也不会把共享能力重新塞回 `Game / Editor`。

## 每轮进行中

1. 先审计，再迁移；没有 file-level consumer 证据时不要直接整目录搬迁。
2. 严守 no-behavior：只做移动、改名、include 修正、target/name 收口与最小 compatibility 过渡。
3. 任何 compatibility 头或 alias 都必须同时写清删除条件。
4. 若发现某目录内部职责并不稳定，先回写计划工件，再决定是否拆 batch。
5. `GUIWorkbench` 始终当作 GUI-only closure sentinel；每次迁移都要检查它是否被回灌了 `Product/Host` / `Game` 语义。

## 每轮收尾前

1. 留下至少一条结构证据：batch 表、audit 表、owner 对照或 include-root 对照；
2. 若本轮动了目录或 target，至少留下一条构建或 `xmake show -t` 证据；
3. 更新至少一个执行工件：`todo.md` / `progress.md` / `feature_matrix.json`；
4. 若本轮改变了 owner 归属口径，同步检查 `plan.md` 与输入工件是否一致；
5. 不要把本计划的新结论回埋到旧的 `gui-architecture-convergence` 活跃待办里。

## 当前默认推进顺序

1. 第一轮 move/rename batch 设计；
2. `Product/Host` 与 `Framework/AppServices` file-level consumer audit；
3. Batch 1 no-behavior 迁移；
4. Batch 2 Product/Host / AppServices 拆分；
5. 过渡清理与闭环验证。
