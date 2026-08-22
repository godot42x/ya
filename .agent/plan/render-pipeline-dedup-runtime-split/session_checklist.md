# 渲染管线去重 + RenderRuntime 服务化拆分 — Session Checklist

> 更新时间：2026-08-22

## 每轮开工前

1. 先读：
   - `plan.md`
   - `progress.md`
   - `feature_matrix.json`
2. 看工作区状态：`git status --short`
3. 确认只推进一个最小切片（一个 phase 或 phase 内一小步），不混入无关重构。
4. 确认 `IRenderPipeline` / `IRenderRuntimeServices` 对外签名当前状态，开工前记下基线。

## 每轮进行中

1. 只做结构迁移，不改行为——禁止改 pass 顺序、禁止改资源创建/释放时机。
2. 挪动任何 owner 都要复查 `retainedResources` / `retireResource` lifetime。
3. 抽取纯函数时，先确认没有隐藏的成员状态依赖（否则不叫「纯搬移」）。
4. **只抽资源/内存机制，不抽渲染策略骨架**——`shouldSkipTick` / `buildShadowState` / `captureShadowSettings` / viewport RT spec 一律不抽。
5. **禁止引入 `IBasePipeline` 或任何 Forward/Deferred 统一编排骨架**。
6. 不平行造新 Service 基类约定，复用现有 `_offscreen` / `_environmentLightingProcessor` 模式。

## 每轮收尾前

1. 至少跑一条最小构建：`xmake b ya-render-3d`
2. 更新 `progress.md` 与 `feature_matrix.json`
3. 复查是否把「结构迁移」做成了「行为改动」
4. 若形成稳定规则，后续考虑上收到 `resource-system` / `render-arch` skill。
