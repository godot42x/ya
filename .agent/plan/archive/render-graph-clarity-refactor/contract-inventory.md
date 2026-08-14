# RenderGraph Contract Inventory

时间：2026-08-02

## 1. Setup / execute 双重真相

当前所有 raster rendering desc 都在 execute lambda 中创建：

| Consumer | graph declaration | execute lambda 额外决定的内容 |
|---|---|---|
| Deferred GBuffer | color/depth attachment handle、frame/light/skinning read | render area、clear value、load/store、final layout、viewport Y、Stage execute |
| Deferred Light | GBuffer/SSAO/shadow/environment read、color attachment | clear/load/store/final layout、LightStage execute |
| Deferred Skybox | color/depth attachment | load/store/final layout、Skybox execute |
| Deferred Scene/Viewport Overlay | color/depth attachment | load/store/final layout、overlay callback |
| SSAO | GBuffer/noise read、output color attachment | clear/final layout、descriptor binding、draw |
| Bloom/Postprocess | input read、output attachment | clear/final layout、postprocess state |
| Forward Viewport | color/resolve/depth attachment、shadow dependency | resolve mode、load/store/final layout、Stage execute、overlay callback |
| Presentation | swapchain output attachment | PresentSrc final layout、postprocess、extension callback |
| Directional/Point Shadow | depth attachment、buffer read/write | cascade/face render area、load/store/final layout、shadow draw |

结论：

- 当前 declaration 足以建立资源依赖，但不能表达完整 raster pass intent。
- 首个样板应选择 Deferred GBuffer 或 SSAO，不要同时迁移所有 pass。
- `Stage::execute()`、descriptor binding 和 graph declaration 仍是三套资源真相，不能在本阶段假设已经统一。

## 2. Imported final state 路径

### 当前完整路径

`RenderGraphExecutor::execute()`：

```text
prepare
  -> graph.compile
  -> registry.sync
executeCompiled
  -> replay pass state transitions
  -> execute callbacks
finalizeImportedBufferStates
finalizeImportedTextureStates
```

### 当前绕过路径

Deferred 主图：

```text
prepare(graph, compiled)
  -> stage prepare / registry resolve / resource 回灌
executeCompiled(graph, compiled, cmdBuf)
  -> 没有 imported finalization
```

因此 `executeCompiled()` 不是完整执行闭环。

### Texture / buffer 差异

- Texture final state 当前只保存 `finalLayout`，由 `transitionImageLayoutAuto()` 处理。
- Buffer final state 保存 stages/access/offset/size，但只在 `execute()` 路径收尾。
- imported buffer 的 retained resources 在 pass state replay 时 retain，在 registry replacement/prune 时通过
  `DeferredDeletionQueue` 退休。

## 3. Registry replacement / reuse

当前 registry 的真实行为：

- 每帧按 `RGHandle(index,generation)` 作为 map key。
- `pruneUnusedResources()` 会移除本图未声明的 handle。
- Persistent resource 目前没有 stable key；跨帧复用依赖创建顺序和 handle 稳定。
- owned texture/buffer 的 desc 变化会立即进入 replacement 分支，旧 shared resource 通过
  `DeferredDeletionQueue` 退休。
- imported texture replacement 同时比较 native/image/view/layout contract。
- imported buffer replacement 比较 desc 和 raw buffer pointer。
- retained resources 的变化会被单独刷新并延迟退休。

结论：

- “persistent” 当前是 lifetime 标签，不是跨 graph 的稳定身份。
- 本子计划不自行实现 stable key；由主计划 FG-101/102 持有。
- 本子计划后续只能为 stable key / lifetime metadata 提供 graph compiler seam。
- 过期声明（2026-08-03）：主计划 FG-101/FG-102 已落地 stable key 契约与
  registry 按 stable key 管理物理资源；`RG-0601/RG-0602` 的 seam 已被消费，
  见 `progress.md` 状态对齐。

## 4. 首个代码切片边界

首个实现提交只允许包含：

- `RenderGraph` / `RenderGraphExecutor` 的 imported finalization contract；
- 对应 core tests；
- 必要的 debug dump / diagnostics；
- 不迁移 Stage owner，不引入 stable key，不实现 alias allocator。

首个提交禁止包含：

- Deferred/Forward surface pass 大规模迁移；
- ResourceResolveSystem / Scene / AssetManager 修改；
- OffscreenTaskService 合并；
- Compute dispatch 或 async compute abstraction；
- physical buffer reuse。
