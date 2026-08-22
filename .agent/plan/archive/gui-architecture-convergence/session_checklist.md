# GUI 架构收敛 Session Checklist

> 更新时间：2026-08-14
> 作用：后续每次推进 `.agent/plan/gui-architecture-convergence` 时，默认先走这里的开工/收尾步骤，避免长期任务靠记忆推进。

## 每轮开工前

1. 先读以下工件，确认当前主线与上一轮停点：
   - `plan.md`
   - `todo.md`
   - `progress.md`
   - `feature_matrix.json`
2. 看当前工作区状态：
   - `git status --short`
   - 必要时看最近 `git log --oneline -n 10`
3. 确认当前只推进一个 phase 的一个最小切片；若已有未收口切片，优先继续而不是开新项。
4. 运行与当前切片相关的最小验证，确认基线未坏：
   - 文档工件改动：检查工件之间是否互相一致；
   - GUI/代码改动：至少跑一个最小 smoke / scenario / dump / target build。
5. 明确本轮预期产出：
   - 结构证据是什么；
   - 视觉或 scenario 证据是什么；
   - 完成条件是什么。
6. 若本轮涉及目录/App/GUI/Game/Editor 命名，先检查是否把“共享能力轴”和“应用形态轴”混成了一层；若混了，先回到 plan 修正语义再动手。
7. 若本轮涉及 window、present、swapchain、RHI 边界，先检查调用者到底需要的是 `INativeWindow` 还是最小 present bridge；不要默认把完整窗口对象继续透传给 RHI。
8. 若本轮是 Phase A 的目录/owner 规划，不要只把 capability/app-form 映射、directory/target/include 审计、API triage 埋在 `plan.md` 的 bullet 里；必须保留为独立工件，供后续 move/rename 直接引用。

## 每轮进行中

1. 优先让改动服务当前计划，不混入无关重构。
2. 若发现当前 phase 的前提不成立，先更新 `todo.md` / `progress.md`，再调整实现。
3. 若发现稳定规则已经超出阶段计划范畴，应该考虑把规则上收到 skill / AGENTS，而不是只留在本目录。
4. 若需要新建验证入口，优先复用现有 GUIWorkbench scenario / dump / gpu-shot / smoke 机制。
5. 若出现“当前能用就行”的修法，暂停并回到 plan 中检查是否违反架构收口方向。
6. 若本轮影响目录/target/App 主链，必须额外检查：windowless app 是否仍能只停在 App/Kernel（按需加 App/Control），而不会被 GUI/Host 或窗口假设污染。
7. 若本轮影响共享能力归位，必须额外检查：physics / scripting / scene / render runtime 是否被错误塞回 `Game` / `Editor` 语义桶。

## 每轮收尾前

1. 确认当前切片已经收口：
   - 没有半套新旧模型并存；
   - 没有来源不明的临时状态；
   - 工作区保持可接力。
2. 至少留下一条结构证据：
   - tree/layout/slot/route dump；或
   - 职责图 / 去重表 / 生命周期表。
3. 至少留下一条视觉或 scenario 证据：
   - golden / screenshot / gpu-shot / scenario diff / smoke log。
4. 更新至少一个执行工件：
   - `todo.md`
   - `progress.md`
   - `feature_matrix.json`
5. 若本轮形成了长期稳定规则，补写到对应 skill 或 AGENTS。
6. 若本轮改动了 owner 或目录语义，确认 `plan.md`、`owner-model.md`、`directory-charter.md` 三者口径一致。

## 计划默认推进顺序

1. `Phase 0`：Rendering correctness gate
2. `Phase A`：主链路命名与 owner 收口
3. `Phase B`：Layout / Slot 内核
4. `Phase C`：事件路径与状态模型
5. `Phase D`：Workbench 与测试迁移
6. `Phase E/F`：specialized layout 与多窗口留口

## 当前下一刀（当前 Phase A 默认接力点）

1. 基于三份审计工件，写第一轮 no-behavior move/rename design；
2. 做 `Product/Host` file-level consumer audit，拆清共享能力消费者与 app-form shell；
3. 拍板 target rename / forward-header transition 策略；
4. 持续把 `GUIWorkbench` 当作 GUI-only closure sentinel，不允许迁移后反向依赖 `Product/Host` / `Game` 语义。
