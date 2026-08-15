# GUI 失效架构 Session Checklist

> 更新时间：2026-08-15  
> 作用：每轮推进 `.agent/plan/gui-invalidation-architecture` 时的固定开工、执行和收尾步骤。

## 开工前

1. 按顺序读取：
   - `plan.md`
   - `todo.md`
   - `progress.md`
   - `feature_matrix.json`
   - 当前任务需要时再读 `audit.md` / `framework-lessons.md`
2. `git status --short`，确认不覆盖无关工作区改动。
3. 确认 `todo.md` 只有一个 `[-]`；没有则领取当前切片。
4. 确认任务依赖已完成，未满足则不绕过。
5. 跑最小基线：

```bash
xmake b ya-gui-closure-test
xmake r ya-gui-closure-test
```

6. 写清本轮：
   - correctness evidence；
   - performance evidence；
   - 非目标；
   - 回退策略。

## 进行中

1. 先写失败测试/固定复现，再改 contract。
2. 每个 property mutation 都确认：
   - equality；
   - Paint/Layout/Subtree impact；
   - binding edge；
   - reflection/authoring path。
3. 每个 reactive 改动都确认：
   - consumer identity；
   - paint-collected/persistent lifetime；
   - rebind/unbind；
   - 双向析构；
   - mixed Paint/Layout consumer。
4. 每个 cache 改动都确认：
   - build context；
   - inherited clip/context；
   - resource generation；
   - hierarchy/order；
   - conservative invalidation fallback。
5. 不在 Phase 0~2 混入 batching、boundary 或 VDOM。
6. 不破坏 immutable snapshot 和 frame-boundary GPU 资源规则。
7. 优先复用现有 closure test、Workbench scenario、snapshot 和 GPU diff。

## 收尾前

1. 跑当前任务定向测试。
2. 跑完整 GUI closure：

```bash
xmake b ya-gui-closure-test
xmake r ya-gui-closure-test
```

3. 受影响时补跑：
   - GUIWorkbench smoke/scenario；
   - headless/windowed snapshot parity；
   - GPU/offscreen zero-diff；
   - resize/uiScale/offset；
   - validation。
4. `git diff --check`。
5. 验证 `feature_matrix.json` 可解析。
6. 更新：
   - `todo.md` 状态；
   - `progress.md` 结果与下一接力点；
   - `feature_matrix.json`；
   - 总设计变化时更新 `plan.md`。
7. 检查没有：
   - 半套 public field + setter 无删除条件；
   - persistent edge 被 paint clear；
   - raw pointer 延迟队列；
   - 无 reason 的 dirty；
   - 无 context contract 的 cache reuse。

## 当前下一刀

`GI-001`：

1. 找到 `GuiPerfStats`、profiling metrics 和 WidgetTree dump 的现有扩展点；
2. 定义最小 `EUIInvalidationReason`；
3. 先覆盖单 Paint、单 Layout、同值 no-op 三个测试；
4. 再接 notify/dependent/transition 计数；
5. 不在这一刀改 Reactive edge 存储结构。
