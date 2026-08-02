# RenderGraph 清晰化任务清单

> 本清单是 `.agent/plan/frame-graph-orchestrator-migration/` 的 graph core 子计划。
> 若任务与 FG-101~FG-111 重叠，以主计划任务为 owner；不要双重修改同一公共契约。

## Gate 0：运行边界冻结

- [x] RG-0001 记录 Deferred / Forward / Presentation 的 executor 调用路径
- [x] RG-0002 记录 command buffer acquire/record/submit/present 生命周期
- [x] RG-0003 记录 imported resource owner、retained resource 和安全退休边界
- [x] RG-0004 明确 Scene、AssetManager、ResourceResolveSystem、render thread 不作为本轮前置重构
- [x] RG-0005 建立本计划与 FG-001、FG-101~FG-111 的依赖映射，避免 `RenderGraph.h/.cpp` 双 owner

证据：[runtime-boundary-inventory.md](./runtime-boundary-inventory.md)

## Phase 0：当前契约 inventory

- [x] RG-0101 列出 setup/execute 双重真相的所有 raster pass
- [x] RG-0102 列出 imported final state 的声明、执行和绕过路径
- [x] RG-0103 列出 registry 对 imported/persistent/transient 的 replacement/reuse 规则
- [x] RG-0104 为现有行为补最小 RenderGraph executor 回归测试

证据：[contract-inventory.md](./contract-inventory.md)

## Gate 1 / Phase 1：执行闭环

- [x] RG-0201 将 imported texture finalization 编译进 compiled plan
- [x] RG-0202 将 imported buffer finalization 编译进 compiled plan
- [x] RG-0203 让 `executeCompiled()` 成为完整执行路径
- [x] RG-0204 让 `execute()` 退化为 convenience wrapper
- [x] RG-0205 验证 Deferred / Forward / Presentation 语义一致
  - 说明：这里的 Forward 仅指现有 graph executor 调用点，不包含 Forward 主 surface 迁移

## Gate 2 / Phase 2：per-pass compiled plan

- [x] RG-0301 增加 `RGCompiledPassPlan`
- [x] RG-0302 将 texture barrier 按 pass 分桶
- [x] RG-0303 将 buffer barrier 按 pass 分桶
- [x] RG-0304 将 imported finalize plan 按 graph 输出保存
- [x] RG-0305 executor 只顺序消费 per-pass plan
- [x] RG-0306 debugDump 输出 pass-local plan

## Phase 3：declaration 单一事实源

- [x] RG-0401 将 raster attachment desc 放入 declaration
- [x] RG-0402 将 load/store/finalLayout/render area/layer count 放入 compiled rendering plan
- [x] RG-0403 将 execute callback 改为消费 typed pass parameters
- [x] RG-0404 按 GBuffer、Light、Skybox、Overlay、Postprocess 顺序迁移

## Phase 4：显式 pass kind

- [x] RG-0501 增加 `ERGPassKind`
- [x] RG-0502 增加 kind-specific validation
- [x] RG-0503 更新 debugDump 和错误信息
- [x] RG-0504 逐步提供 Raster / Compute / Copy builder 入口
  - 前置：必须存在对应真实 consumer；首轮只实现 Raster declaration 和现有 transfer API 所需校验

## Gate 3 / Phase 5：资源模型扩展入口

- [x] RG-0601 对齐 FG-101/102 的 persistent stable key API
- [x] RG-0602 对齐 FG-106 的 transient lifetime metadata
- [x] RG-0603 为 FG-107~FG-109 提供 compiled-plan seam，不重复实现 slot allocator
- [x] RG-0604 对齐 FG-110 的 reuse diagnostics，不把“handle 复用”冒充物理复用
  - 当前阶段只输出 logical transient count/bytes、used/unused lifetime 统计，并明确标记 physical reuse 尚未 materialize
  - physical slot count/bytes、assignment、pool hit/miss、reuse ratio 仍由主计划 FG-107~FG-110 完成
- [ ] RG-0605 评估 async compute / multi-queue 是否需要扩展 state model

## 每阶段验证

- [ ] `xmake b ya-engine`
- [ ] `xmake b ya-runtime`
- [ ] 受影响目标的 `xmake b`
- [ ] `python3 Script/ya.py test --target ya --filter 'RenderGraphCoreTest.*:ResourceStateTrackerTest.*'`
- [ ] Deferred / Forward / Presentation smoke
- [ ] Bloom、SSAO、Shadow 开关
- [ ] resize、pipeline switch、scene switch、shutdown
- [ ] Vulkan validation / RenderDoc 检查 layout、barrier、lifetime

## 当前建议提交分组

1. `[render/graph] close imported finalize contract in compiled execution`
   - `Engine/Source/Render/Core/RenderGraph.h`
   - `Engine/Source/Render/Core/RenderGraph.cpp`
   - `Engine/Source/Render/Core/RenderGraphExecutor.h`
   - `Engine/Source/Render/Core/RenderGraphExecutor.cpp`
   - `Engine/Test/Source/RenderGraphCoreTest.cpp`

2. `[plan/render] record render-graph clarity progress`
   - `.agent/plan/render-graph-clarity-refactor/*`
