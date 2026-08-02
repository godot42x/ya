# RenderGraph 执行前审查

时间：2026-08-02

## 审查结论

计划可以执行，但不能作为独立迁移计划执行。
它必须作为 `frame-graph-orchestrator-migration` 的 RenderGraph core 子计划，
否则会出现两个计划同时修改 `RenderGraph.h/.cpp`、registry 和 compiled graph contract。

## 已修正的问题

| 问题 | 风险 | 修正 |
|---|---|---|
| Forward 状态描述过于完整 | 把未完成迁移误算为现状 | 明确 Forward 主 surface 仍是 Stage 固定顺序 |
| aliasing 被写成可延后 | 与 FG-106~FG-110 冲突 | 本计划只提供 seam，主计划负责物理复用和门禁 |
| 提前设计 Compute/Copy plan | 当前没有对应真实 consumer | 首轮只落 Raster 与现有 transfer contract |
| Phase/跨计划 owner 不明确 | 中途出现公共头双重改动 | 增加 FG 依赖映射和单 owner 规则 |
| 测试命令过于泛化 | 可能调用不存在的 alias 或扩大验证范围 | 改为 `python3 Script/ya.py test` + 受影响目标 xmake |

## 执行顺序

1. `FG-001` / `RG-0001~RG-0005`：先完成 inventory 和 owner mapping。
2. `RG-0101~RG-0104`：只读审计和 core regression baseline。
3. `RG-0201~RG-0205`：先统一 execute/finalize contract。
4. `RG-0301~RG-0306`：再引入 per-pass compiled plan。
5. `RG-0401~RG-0404`：只迁移一个真实 Deferred raster consumer 作为样板。
6. `RG-0501~RG-0504`：有真实 consumer 后再提升 pass kind。
7. 与 FG-101~FG-111 的 stable key、lifetime、slot、arena 工作通过接口对齐，不重复实现。

## 停止条件

遇到以下情况应停止实现，先更新 progress：

- 需要同时修改 Scene/AssetManager/ResourceResolveSystem 才能完成 graph core 任务。
- 需要引入当前没有 consumer 的 dispatch/copy abstraction。
- 发现一个 local executor 实际拥有独立 submit/job completion boundary。
- `executeCompiled()` 无法在不破坏现有 pipeline 的情况下承担 imported finalization。
- core test 与真实 Deferred graph dump 对不上。
