---
name: resource-system
description: YA Engine 资源加载、资产槽与派生资源准备。适用于排查 AssetManager、TextureRef/ModelRef/TilesetRef、GameplayResourceBinding、地形与 environment lighting 的事件驱动 resolve。
---

## 适用场景

- 用户要求梳理 texture / model / material 的资源加载数据流
- 排查资源热重载、槽状态、placeholder、descriptor 不更新问题
- 排查 skybox、environment cubemap、irradiance、prefilter 与 scene lighting 不同步问题
- 修改 `AssetManager`、资产槽、`GameplayResourceBinding`、地形或 environment lighting 的准备链路

## 先判断是不是这里

留在本 skill：

- 资源槽是否到达 Ready / Failed
- 派生准备有没有被编辑、槽更新或离屏完成重新入队
- descriptor / GPU 资源是否刷新
- environment lighting 的运行时结果是否真的生成并被消费

转去 `material-flow`：

- 谁是材质 authoring 真相源
- editor 修改路径该落到 component 还是 runtime material
- pipeline 是否越界直接读 component

## 当前稳定边界

1. 贴图和模型槽在 `AssetManager`。文档型资产（Tileset、SpriteAnimationSet，以及注册进来的其它文档）的槽在 `AssetTypeRegistry` 的 store 上，`AssetManager` 只做 clear / collectUnused / unload / invalidate 聚合。Ref 只持有路径和 `AssetHandle`；加载状态、资源和 `generation` 在槽上，一份。`SpriteAnimationSet.atlas` 非空时是该动画集拥有的贴图路径：显示帧时写到同实体 `Sprite2DComponent.image` 的路径上，采样配置留在精灵上。空 `atlas` 不改贴图。
2. 处理器订阅 `SceneBus`（创建 / 编辑 / 删除），并持有槽订阅。回调只入队。稳态 `prepare` 不扫描组件 view；每个场景第一次 prepare 做一次 seed。
3. `GameplayResourceBinding` 负责已有 mesh / material / billboard 的运行时 resolve，不负责 scene topology。
4. `ModelInstantiationSystem` 负责 `ModelComponent` -> 子节点 / 子实体展开，再交给普通 resolve 链。它同样由 SceneBus 和模型槽驱动。
5. `TextureSlot` 的 authoring 语义归 component；这里只关心它如何变成 runtime binding。
6. 材质上传依赖 runtime `Material` 自己的 `paramVersion` / `resourceVersion` 与 consumer 的 uploaded version。这不是 `AssetManager` 的版本表；按路径的 `getResourceVersion` 已删除。
7. GUI 贴图目录用 `AssetManager::getResourceVersionEpoch()`。贴图槽每次 `dispatchSlotUpdate` 推进这个 epoch。
8. `EnvironmentLighting` 是 source / irradiance / prefilter 三段分支。运行时纹理、pending job、`resultVersion` 属于 runtime state，不回写 authoring 数据。完成靠批次 `onReady` 和离屏 `onFinished`，不靠 active 重泵。
9. 地形高度图同样由批次 `onReady` 唤醒。派生缓存键里的高度图版本是槽 `generation`。
10. GPU 资源创建与 offscreen job 提交必须由 owner 显式提供 `IRender` / `OffscreenJobQueueService`。准备阶段不回查全局 App。
11. 槽状态只有 `Loading` / `Ready` / `Failed`。完成、失败、热重载替换都 `generation++` 并通知订阅者。没有一份给 debug 审计用的 Loading 计数。
12. 编辑只走 `Scene::notifyComponentEdited`（检查器、undo、脚本字段写入、companion 带外写入）。创建 / 删除由组件漏斗广播。派生工作只由三类事件入队：编辑、槽更新（完成 / 失败 / 热重载）、离屏任务 `onFinished`。
13. `GameplayResourceBinding` 的 debug 一致性审计只按 120 tick 间隔跑。它不是派生工作，不调用 `noteResourceResolveView`。发现问题只断言，不把实体重新入队。

## 主链路

### Texture

```text
setPath / deserialize
  -> TextureRef 绑定 AssetHandle
  -> 槽 Loading / Ready / Failed
  -> 槽 observer 或编辑入口把实体入队
  -> GameplayResourceBinding 泵一次
  -> TextureSlot::toTextureBinding()
  -> runtime Material::setTextureBinding()
  -> MaterialDescPool::flushDirty()
  -> render consumer 更新 descriptor
```

要点：

1. 槽按请求身份（规范化路径 + colorSpace）去重。完成、失败、热重载替换都 `generation++` 并通知订阅者。
2. 未 ready 时可先落 placeholder，避免 descriptor 指向空 view。
3. 纹理更新最终靠 runtime material 的 version 推动上传。
4. 热重载走 `onAssetFileChanged` -> 槽原地重填。GUI 看到的是 epoch，不是按路径的版本。

### Material

```text
MaterialComponent
  -> syncParamsToMaterial() / syncTextureSlot()
  -> runtime Material param/resource version++
  -> consumer-specific MaterialDescPool::flushDirty()
  -> GPU UBO / descriptor 更新
```

要点：

1. runtime `Material` 是上传前的 cache / binding 容器。
2. 多个 render consumer 各自维护上传版本，不能互相清状态。
3. authoring 语义与 editor 修改链路交给 `material-flow`。

### Model

```text
ModelComponent._modelRef
  -> 模型槽
  -> ModelInstantiationSystem（SceneBus + 槽订阅 + 一次 seed）
  -> child MeshComponent / MaterialComponent
  -> GameplayResourceBinding resolve 这些已有组件
```

模型先决定 topology，再走普通 resolve。

### Model 实例语义（prefab-like）

`ModelComponent` 根 + 它的 `ManagedChildComponent` 子树就是一个**实例子场景**，行为对齐 Unity prefab instance：

- 根实体只序列化 `_modelRef`；子 mesh 不进存档（`SceneSerializer` 跳过 `ManagedChildComponent`），加载时按 ModelRef 重建。
- `_cachedMaterials` 按 material index 共享 runtime material，等价 `sharedMaterial`：改一个 mesh 的参数会影响共享同一 material 的同级 mesh。
- **实例边界来自结构，不来自缓存**：判断/清理实例子节点一律扫「实例根 node 下的 `ManagedChildComponent` 子树」。曾经存在的 `ModelComponent::_childNodes` 裸指针清册在作者删掉某个子 mesh 后必然失效，触发 cleanup 时就是 use-after-free，已删除。
- `Scene::destroyEntity` **级联销毁整棵子树**（逆前序，后代先于祖先）。不要写「删父留子」的新代码：孤儿实体会留在 registry 里继续被绘制提取渲染，同时不在任何 node 子树里，于是既看不见也选不中。
- 编辑器侧：视口点选默认解析到实例根（`editorResolveInstanceRoot`，`Alt`+点击保留叶子 mesh）；托管子节点上的删除 / 复制 / 重排 / 拖入被拒绝，因为它们的改动会被下一次重建覆盖，或让被拖入的对象随实例一起销毁。
- 实例子节点的材质 / 参数 / 变换编辑**仅本次会话有效**，实例重建即重置。

## Environment Lighting

### 结构规则

1. source 负责拿到最终 environment cubemap。
2. irradiance / prefilter 是基于 source 的派生结果，可独立启停。
3. 三段状态与运行时结果分离：component 保存 authoring 选择，runtime state 保存纹理、pending job、`resultVersion`。
4. 发现路径是 SceneBus 加每个场景一次 seed。CPU 批次完成和离屏 `onFinished` 把该实体重新入队。`prepare` 只泵脏队列。
5. 派生缓存键是路径和翻转等 authoring 字段。槽内容变化靠重新入队，不靠全局 resource version。

### Source 分支

```text
SceneSkybox / CubeFaces / Cylindrical
  -> sourceState Dirty（编辑或 seed）
  -> 提交批次或圆柱贴图，回调入队
  -> 必要时排队 offscreen job，onFinished 入队
  -> sourceState Ready
  -> resultVersion++
```

### 派生分支

```text
source Ready
  -> irradianceState / prefilterState Dirty
  -> 创建 offscreen job
  -> onFinished 入队后采用结果
  -> Ready
```

规则：

1. `Disabled` 必须真的停用并回收对应运行时结果。
2. 重新生成前要退休旧纹理，避免悬挂引用。
3. 不能因为状态分支存在，就假设 job 已经接线且结果已经生成。
4. 同步失败的离屏任务在同一次泵里落到 Failed 分支。不要靠下一帧的 active 重泵去发现它。
5. 重置 pending 批次时先 `consumeTextureBatchMemory`；回调若发现句柄已经对不上，也要消费并丢弃，避免 ready-map 泄漏。

## 渲染侧消费

```text
RenderRuntime
  -> 读处理器为该场景维护的选中来源
  -> 绑定 cubemap / irradiance / prefilter 相关资源
  -> 绑定对象变化时更新 descriptor
```

每个场景四条通道（天空盒 cubemap、环境 cubemap、irradiance、prefilter）各自保留**最先变为就绪**的贡献者，直到它不再贡献。失效后，按变为就绪的先后，下一个仍就绪的贡献者接上。消费侧只读队首。没有环境 cubemap 贡献者时，场景 cubemap 回退到当前选中的天空盒。

四条通道用同一条规则。旧的 view 循环里 cubemap 每次被后写覆盖、irradiance / prefilter 留下第一个，是循环少了空位判断，不是契约。

`bUsesSceneSkybox` 只在 `markEnvironmentLightingDirty` 从组件写入一次。选中来源在这些状态切换上更新：组件增删与编辑入队（`mark*Dirty` / `cleanup*`）、天空盒与环境泵结束（批次或离屏完成后的状态）、`dropWork` 清空。天空盒选中变化时，顺带重算依赖场景天空盒的环境 cubemap 通道。

scene-level environment binding 和材质纹理上传不是同一条链。环境贴图问题同时看 `EnvironmentLightingProcessor` 与 `RenderRuntime`。

## GPU 资源生命周期与保活（RetainedResource）

1. **不用 tracing GC**。渲染层资源有 GPU 同步约束，用引用计数 + 确定性延迟释放。
2. **保活句柄用强类型 `RetainedResource`**（`Core/Common/RetainedResource.h`）：`shared_ptr<void>` + `type_index` + 可选 debugTag。
3. **`retain` 与 `retire` 分开**：`retainedResources` 是外部 keep-alive；`retireResource()` 是 submit 后延迟释放。
4. `ICommandBuffer` 统一 `retireResource<T>(shared_ptr<T>, tag={})`。
5. **Imported 资源的跨帧身份是底层 `IImage` / `IImageView`，不是 `ImageResource` 包装指针。**
6. 若每帧都在 replacing / retire，先查身份比较是否误用了每帧重建的包装指针（`../../memories/rendergraph_import_reuse_wrapper_identity_regression.md`）。

相关排查记忆：

- `../../memories/vulkan_submit_lifecycle_debug.md`
- `../../memories/rendergraph_import_reuse_wrapper_identity_regression.md`
- `../../memories/terrain_processor_active_pump_regression.md`：地形与环境光照都不再用 active 重泵。

## 高风险模式

1. 把 topology 创建和普通 resolve 混在一起。
2. component authoring 数据与 runtime state 双写。
3. 某个 consumer 更新了 descriptor，另一条管线仍持有旧绑定。
4. 只改 component 状态，不走 SceneBus，也不推进 `resultVersion`。
5. 在槽回调或离屏回调里改 registry、录制命令，或对同一槽订阅/退订。
6. 恢复 active 重泵或每帧组件扫描来“补上”漏掉的完成信号。完成信号应该是回调。

## 快速排查顺序

1. 资源不更新：看槽 `state` / `generation`，以及处理器是否还持有该槽的订阅。
2. 材质纹理没刷新：看 component 是否重新 `syncTextureSlot()`，runtime material 的 `resourceVersion` 是否递增。
3. descriptor 没刷新：看对应 consumer 的 `MaterialDescPool::flushDirty()` 是否执行。
4. skybox / environment cubemap 没刷新：看 source 状态是否进入 `Ready`，`resultVersion` 是否推进，离屏 job 的 `onFinished` 有没有把实体入队。
5. irradiance / prefilter 没生效：看分支状态、pending offscreen job 和 `RenderRuntime` 绑定是否同步。
6. 稳态 prepare 又在扫组件：看是不是 seed 之外又进了 view。debug 审计每 120 tick 会走 view，但它不计入 resolve 探针；探针只记准备阶段为做派生工作而遍历的 view。审计只断言，不重新入队。

## 新增文档型资产

文档型资产（同步 JSON，一个路径一个槽）是开闭的。新增一种 = 资产自己的类型 + parse/serialize + **一处注册**，不改 `AssetManager`、`IAssetRefResolver`、`isAssetRefType`、`PropertyAccessor` 或编辑器选择器枚举。

要写的：

1. 资源类型 `T` 和薄 Ref `RefT : DocumentAssetRef<T>`（保留反射类型名；`_handle` / `get` / `isLoaded` / `getResolveState` / `rebind` 在基类上）。
2. `Traits`：`parse(text, error)`、`logCannotRead(path)`、`logInvalid(path, error)`。日志用字面量 `YA_CORE_ERROR`。
3. 在该资产的 cpp 里静态初始化调用
   `AssetTypeRegistry::registerDocument<T, Traits, RefT>(name, displayName, {".ext"})`。
   声明在 `AssetTypeRegistry`，定义在 `Core/Common/AssetDocumentManager.h`。

注册之后自动成立的：

- 同路径 Ref 共享一个槽；解析失败是 `Failed`。
- `AssetManager::clearCache` / `collectUnused` / `unload` / `invalidate` 以及 meta、文件变更会走到这个 store。带已注册扩展名的路径只打到对应 store；没有扩展名的 `registerAsset` 名字会问每一个 store。
- `isAssetRefType` 和 Inspector 的 Failed 判定认这个 Ref。选择器标题和扩展名来自 `AssetTypeDesc`（`makeAssetPickerRequest`）。

不需要改、也不要再加分支的地方：`AssetManager` 的逐类型方法、`IAssetRefResolver` 的 acquire、`PropertyCapabilityRegistry`（已删除）、`EEditorAssetPickerKind`（已删除）。

仍然硬编码、不走这套注册的：

- 异步 GPU 资产 Texture / Model / Mesh。它们只在 `Core/Common/AssetRef.cpp` 登记描述（选择器扩展名），槽仍由 `IAssetRefResolver` + `AssetManager` 的贴图/模型链路管。`store` 为空。
- 内容浏览器双击打开编辑器：`EditorContentBrowserTab::activateItem` 按后缀写死 `.scene.json`、`.lua`、`.yaui.json`、`.mat` / `.material`。文档资产双击不会进编辑器，留给 P3 的「扩展名 → 打开器」注册表。

## 相关 skills

- `material-flow`：authoring 真相源、editor 修改链路、runtime material 边界
- `render-arch`：RenderRuntime、offscreen pipeline、layout、后端消费
- `debug-review`：崩溃、自检、回归排查
- `ya-build`：资源改动涉及构建、shader 生成、测试时一起看

## 退出条件

- 已明确问题属于槽状态、派生入队、descriptor 上传，还是 environment lighting 运行时结果
- 已定位主要责任层：`AssetManager`、资产 ref、`GameplayResourceBinding`、`ModelInstantiationSystem`、`TerrainProcessor`、`EnvironmentLightingProcessor` 或 `RenderRuntime`
- 已知道下一步是继续改资源链路，还是转去 `material-flow` / `render-arch` / `debug-review`
