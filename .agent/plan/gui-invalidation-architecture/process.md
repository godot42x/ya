# GUI 失效架构实施流程

> 更新时间：2026-08-15  
> 作用：规定这条长期重构每一轮如何选任务、改代码、验证、记录和进入条件优化阶段。

## 1. 工件职责

| 文件 | 职责 |
|---|---|
| `audit.md` | 当前实现问题与跨框架分析依据 |
| `framework-lessons.md` | 可吸收能力、拒绝照搬项、阶段映射 |
| `plan.md` | 稳定目标、phase、架构不变式与总验收 |
| `todo.md` | 当前可执行任务、依赖、提交边界 |
| `process.md` | 本文件；固定推进流程与决策门 |
| `progress.md` | 只记已经发生的事实、验证结果、下一接力点 |
| `feature_matrix.json` | 机器可读状态，不承载长篇设计 |
| `session_checklist.md` | 每轮开工/收尾快速清单 |

若结论变成长期稳定契约，完成阶段后上收到 `gui-framework/SKILL.md`；历史回归根因写 memory。

## 2. 任务领取

1. 读 `todo.md` 的“当前切片”。
2. 确认依赖全部完成。
3. 将唯一任务从 `[ ]` 改为 `[-]`。
4. 在 `progress.md` 开一条本轮记录，写预期：
   - 修改范围；
   - correctness evidence；
   - performance evidence；
   - 明确非目标。
5. 不同时领取第二个架构任务。

## 3. 实施顺序

每个代码任务按以下顺序：

```text
复现/固定测试
→ 最小 contract 改动
→ 迁移一个真实 consumer
→ 跑局部测试
→ 迁移剩余当前切片 consumers
→ 跑 convergence gate
→ 更新工件
```

禁止先大面积改 API，再补测试和 consumer。

### 3.1 Correctness-first

优先检查：

- property change 是否产生正确 dirty reason；
- Layout dirty 是否在本次 snapshot 前可见；
- dependency edge 是否正确收集/移除；
- build/inherited context 是否仍兼容 cache；
- detach/reparent/destructor 是否清理引用；
- snapshot 是否保持 immutable。

性能优化不能改变上述语义。

### 3.2 Property 改动流程

新增或迁移 runtime property 时必须写出：

1. backing value；
2. equality/no-op 规则；
3. `EUIPropertyImpact`；
4. 是否影响 descendants；
5. reflection/serialization 写入路径；
6. binding dirty level；
7. 最小测试。

调用方不能传入 dirty level；impact 属于 property contract。

### 3.3 Reactive 改动流程

必须同时审查：

- edge identity；
- Paint/Layout reason；
- paint-collected 或 persistent lifetime；
- dynamic dependency recollection；
- rebind/unbind；
- widget/reactive 双向析构；
- multiple trees；
- 同一 widget 多 consumer。

不允许用 raw pointer queue 引入新的延期生命周期问题。

### 3.4 Cache 改动流程

任何 cache reuse 都要列出输入：

```text
widget identity
property values
layout rect
build context
inherited paint context
resource generation
hierarchy/order
```

无法证明兼容时先保守 invalidate。只有 profile 证明保守策略昂贵，才增加 fingerprint/generation 粒度。

## 4. 验证层级

### L0 — 静态检查

- `git diff --check`
- direct-write lint/grep
- JSON parse (`feature_matrix.json`)
- 文档 phase/status 一致性

### L1 — GUI closure tests

```bash
xmake b ya-gui-closure-test
xmake r ya-gui-closure-test
```

优先用 filter 运行当前新增测试；收尾跑完整 closure。

### L2 — Workbench 行为

```bash
python3 Script/ya.py run --project Example/GUIWorkbench/GUIWorkbench.yaproject
```

若实际 project 路径/automation 参数变化，以 `ya-build` skill 和当前 target 为准。使用已有 smoke/scenario/dump，不另造平行 harness。

必须覆盖受影响页面，例如：

- Editor presenter/binding；
- Widgets transient state；
- ScrollSplit clip/layout；
- Render baseline。

### L3 — Snapshot / GPU parity

按改动选择：

- CPU snapshot JSON/digest；
- headless/windowed structural parity；
- GPU shot/offscreen zero-tolerance diff；
- resize/scale/offset context change；
- Vulkan validation 无新增错误。

### L4 — 性能证据

优化任务必须提供前后：

- layout/paint time；
- painted/rebuilt/visited widgets；
- draw items；
- notify/dependent visits；
- cache invalidation/reuse；
- 样本、帧号和运行条件。

没有数据不得启动 Phase 3/4。

## 5. Phase 决策门

### Gate A — Phase 1A 完成

- mixed Paint/Layout consumer 正确；
- persistent binding 不被 paint clear；
- AutoSize binding measure/paint 一致；
- Workbench 对应 volatile 已移除。

### Gate B — Phase 1B 完成

- runtime direct write 有静态门禁；
- reflection/document transaction 可用；
- public mutable property 只剩登记的 authoring 例外。

### Gate C — Phase 2 完成

- 三个特殊 host 统一进入 base paint pipeline；
- build context 和 inherited context cache validity 正确；
- snapshot/GPU parity 通过。

### Gate D — Batching

仅当重复 notify/dependent traversal 是热点时开放 GI-402。

### Gate E — Subtree cache

仅当 clean traversal 是主要瓶颈，且有稳定静态 subtree 时开放 GI-502/503。

### Gate F — Reconciliation

仅当至少两个真实 dynamic list consumer 需要 identity/state preservation 时开放 GI-602/603。

## 6. 停止与回退

出现以下任一情况，停止当前实现并回到 plan：

- 需要同时引入 property system、scheduler、VDOM 和 subtree cache 才能工作；
- 新模型和旧模型无法在当前切片结束时收口；
- correctness test 无法解释 dirty/cache reason；
- 性能收益只存在于合成 microbenchmark；
- 为优化破坏 snapshot immutable 或 frame boundary；
- 大量 shared ownership 只是为了掩盖 queue 生命周期。

回退优先级：

1. 保守 invalidate；
2. 保留立即 notify；
3. 保留全树 traversal；
4. 小范围 volatile；
5. 最后才考虑复杂调度/缓存。

## 7. 收尾

1. 任务 `[-]` 改 `[x]` 或 `[-x]`。
2. 更新 `progress.md`：
   - 本轮完成；
   - 代码/行为结论；
   - 验证命令与结果；
   - 下一接力点。
3. 更新 `feature_matrix.json` 状态和 notes。
4. 若任务改变总设计，更新 `plan.md`；若只补分析，更新 `audit.md`。
5. `git diff --check`，确认没有半套接口和无主 TODO。
6. 提交格式遵循 `[module] message`。
