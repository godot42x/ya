# App / GUI 边界迁移进度记录

> 建立日期：2026-08-14
> 作用：记录这条新迁移主线从旧 GUI convergence 计划中拆出后的推进情况。

## 2026-08-14 — 从 `gui-architecture-convergence` 拆出独立迁移计划

### 本轮完成

- 新建 `app-gui-boundary-migration/`，把还未完成的目录 / target / owner 迁移主线单独立项；
- 将活跃输入工件迁入本计划目录：`owner-model.md`、`directory-charter.md`、`capability-appform-mapping.md`、`nativewindow-api-triage.md`、`directory-target-include-audit.md`；
- 旧的 `gui-architecture-convergence` 计划改为历史基线，不再作为活跃迁移待办的默认入口；
- 新计划已补齐最低执行工件：`plan.md`、`todo.md`、`progress.md`、`feature_matrix.json`、`session_checklist.md`。

### 当前结论

- 现在的活跃问题已经不再是 GUI 内核本身，而是如何把已经定下来的 owner / 目录 / target 语义做成真实的 no-behavior 迁移批次；
- 最关键的剩余模糊区仍然是 `Product/Host` 与 `Framework/AppServices`；这两块必须先做 file-level audit，再动目录。

### 下一轮直接接力点

1. 写第一轮 move/rename batch 设计；
2. 做 `Product/Host` / `Framework/AppServices` file-level consumer audit；
3. 再开 Batch 1 迁移 patch。

### 本轮验证

- 文档级验证：新目录最低工件已补齐；
- 文档级验证：活跃迁移输入工件已迁到新目录；
- 代码级验证：本轮未改 `Engine/Source` 行为代码。

## 2026-08-14 — 完成 Batch 1 no-behavior 迁移设计

### 本轮完成

- 新增 `first-batch-move-design.md`，把 Batch 1 的 move/rename 范围落到文件级；
- 明确了 `Foundation/Core/Application/*` -> `App/Kernel` + `App/Control` 的迁移表；
- 明确了 `Framework/AppRuntime/*` + `Framework/GUI/App/*` -> `GUI/Host/*` 的迁移表；
- 定义了 forward-header / compat-target 的唯一过渡形式、删除条件、build checkpoints 与回退点；
- 把当前活跃切片从 Phase A1 设计切换到 Phase A2 file-level consumer audit。

### 当前结论

- Batch 1 现在已经不是抽象口号，而是一份可直接转为 patch 的迁移蓝图；
- 下一步的真实风险不在 `App/*` 和 `GUI/Host/*`，而在 `Product/Host` 与 `Framework/AppServices` 这两个混合桶；
- 因此继续动代码前，先补 `Product/Host` 与 `Framework/AppServices` 的 file-level audit 是必要前置。

### 下一轮直接接力点

1. 输出 `product-host-file-audit.md`；
2. 输出 `appservices-file-audit.md`；
3. 依据两份 audit 开 Batch 1 真实 no-behavior 迁移 patch。

### 本轮验证

- 文档级验证：Batch 1 的目录、target、include root、compat、删除条件、build checkpoints 已落盘；
- 文档级验证：todo 与活跃切片已切到 file-level audit；
- 代码级验证：本轮仍未改 `Engine/Source` 行为代码。
