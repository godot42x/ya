---
name: resource-system
description: YA Engine 资源加载、运行时 resolve、环境光照 cubemap/irradiance 生成链路梳理。适用于排查 AssetManager、AssetRef、Material dirty/version、ResourceResolveSystem，以及后续将环境光照从 tick 扫描改为 dirty/event 驱动的重构。
---

## 适用场景

- 用户要求梳理 texture / model / material 的资源加载数据流
- 排查资源热重载、stale pointer、placeholder、descriptor 不更新问题
- 排查环境贴图、辐照图、skybox 与 scene lighting 不同步问题
- 准备重构 `ResourceResolveSystem`，尤其是把 environment lighting 从 tick 扫描改为 dirty / event 驱动

## 当前架构结论

1. `AssetManager` 是资源加载与缓存入口：负责 async decode、cache、pending 去重、resource version。
2. `TAssetRef<T>` 是组件侧轻量引用：只持有 path / cachedPtr / resolvedVersion，不直接做 GPU 上传。
3. `ResourceResolveSystem` 负责**已有组件**的运行时 resolve，不负责 scene topology 创建。
4. `ModelInstantiationSystem` 负责 `ModelComponent` → 子节点 / 子实体展开。
5. `TextureSlot` 持有 authoring 语义（path、enable、uv），runtime `Material` 只持有 render-ready binding/cache。
6. 材质上传现在基本是 **dirty/version 驱动**；`Skybox` / `EnvironmentLighting` 的 resolve 仍由 `ResourceResolveSystem` 在 update 中推进，但组件内的 orchestration 已经收口到 system。
7. `StateTransition<E>` 是 resolve 状态机的统一基础设施：通过 `StateTraits<E>` 描述失败态 / 终态，并用 `makeTransition(state, label).to(...)` / `fail(...)` 统一状态跃迁与 trace。
8. 组件增删现在有统一 mutation 路径：`Scene` 的 typed 入口和 `ECSRegistry` 的 dynamic 入口最终都应共享同一套 add/remove 生命周期逻辑，避免 `prepareForRemove()`、`SceneBus` 广播等行为分叉。

## 一、资源加载主链路

### 1.1 Texture：authoring path → runtime texture binding → descriptor

```text
TextureSlot
  → TAssetRef<Texture>::resolve()
  → AssetManager::loadTexture()
  → async decode / cache / version tracking
  → TextureSlot::toTextureBinding()
  → runtime Material::setTextureBinding()
  → MaterialDescPool::flushDirty()
  → render pass updateDescriptorSets()
```

关键锚点：

- `Engine/Source/Render/Material/Material.h:137` `TextureSlot::resolve()`
- `Engine/Source/Core/Common/AssetRef.h:266` `TAssetRef<Texture>::resolve()`
- `Engine/Source/Resource/AssetManager.cpp:529` `AssetManager::loadTexture()`
- `Engine/Source/Render/Material/PhongMaterial.h:159` `setTextureBinding()`
- `Engine/Source/Render/Material/PBRMaterial.h:110` `setTextureBinding()`
- `Engine/Source/Render/Material/MaterialDescPool.h:85` `flushDirty()`

行为要点：

1. `TAssetRef<Texture>::resolve()` 每次先看 `AssetManager::getResourceVersion()`；版本未变则直接复用缓存，版本变了就失效重解。
2. `AssetManager::loadTexture()` 对相同 cache key 会复用缓存，并对 in-flight load 去重，避免重复提交。
3. 纹理未 ready 时，组件层可能先写 placeholder，避免 descriptor 指向空 imageView。
4. 一旦 `setTextureBinding()` 被调用，就会 `setResourceDirty()`，后续由渲染 consumer 在 `flushDirty()` 中完成 descriptor 更新。

### 1.2 Material：component 是真相源，runtime material 是 render cache

```text
Phong/PBR/UnlitMaterialComponent
  → syncParamsToMaterial()
  → syncTextureSlot()
  → runtime Material param/resource version++
  → consumer-specific MaterialDescPool::flushDirty()
  → GPU UBO / descriptor set 更新
```

关键锚点：

- `Engine/Source/ECS/System/ResourceResolveSystem.cpp:316` `resolvePendingMaterials()`
- `Engine/Source/ECS/Component/Material/PhongMaterialComponent.cpp:125` `syncTextureSlot()`
- `Engine/Source/ECS/Component/Material/PBRMaterialComponent.cpp:102` `syncTextureSlot()`
- `Engine/Source/Render/Material/Material.h:212` `setParamDirty()`
- `Engine/Source/Render/Material/Material.h:222` `setResourceDirty()`
- `Engine/Source/ECS/System/Render/PhongMaterialSystem.cpp:397` `flushDirty()`
- `Engine/Source/Runtime/App/DeferredRender/DeferredRenderPipeline_PBR.cpp:105` `flushDirty()`

判断原则：

1. Component 保存 authoring 数据与可编辑语义。
2. runtime `Material` 只作为 GPU 上传前的 cache / binding 容器。
3. descriptor 更新不该依赖某个全局 dirty 被单一路 consumer 清空，而应依赖 version 对比；当前 `MaterialDescPool` 基本符合这个方向。

### 1.3 Model：模型资源加载与子实体展开分离

```text
ModelComponent._modelRef
  → ModelInstantiationSystem
  → 生成 child entities
  → child MeshComponent / MaterialComponent
  → ResourceResolveSystem resolve 这些已有组件
```

关键锚点：

- `Engine/Source/ECS/Component/ModelComponent.h:11` Data Flow 注释
- `Engine/Source/ECS/System/ModelInstantiationSystem.h:22` 职责边界
- `Engine/Source/ECS/Component/MeshComponent.cpp:12` `MeshComponent::resolve()`
- `Engine/Source/Core/Common/AssetRef.h:309` `TAssetRef<Model>::resolve()`
- `Engine/Source/Resource/AssetManager.cpp:1276` `AssetManager::loadModel()`

这条链路的关键不是“模型直接进渲染”，而是：

1. `ModelComponent` 是源引用。
2. `ModelInstantiationSystem` 决定 topology。
3. `MeshComponent` / `MaterialComponent` 进入普通 resolve 流程。

## 二、Environment Lighting / Irradiance 当前流程

### 2.1 高层状态机

当前 pattern：

- `EnvironmentLightingComponent` / `SkyboxComponent` 只保留稳定数据、结果纹理、状态查询与 `invalidate()`
- pending async / offscreen runtime state 由 `ResourceResolveSystem` 持有
- 主流程保持显式 `switch(resolveState)`
- 状态变更统一通过 `StateTransition<E>` 记录

`EnvironmentLightingComponent` 的状态：

- `Dirty`
- `ResolvingSource`
- `PreprocessingEnvironment`
- `PreprocessingIrradiance`
- `Ready`
- `Failed`

关键锚点：

- `Engine/Source/ECS/Component/3D/EnvironmentLightingComponent.h`
- `Engine/Source/ECS/Component/3D/EnvironmentLightingComponent.cpp`
- `Engine/Source/ECS/System/ResourceResolveSystem.cpp`
- `Engine/Source/Core/Common/StateTransition.h`

### 2.2 Cubemap 源路径

```text
Dirty
  → ResourceResolveSystem::stepEnvironmentLightingSourceLoad()
  → loadTextureBatchIntoMemory(6 faces)
  → consumeTextureBatchMemory()
  → Texture::createCubeMapFromMemory()
  → beginIrradianceFromResolvedCubemap(cubemapTexture)
  → StateTransition.to(PreprocessingIrradiance)
```

关键锚点：

- `Engine/Source/ECS/Component/3D/EnvironmentLightingComponent.cpp:83`
- `Engine/Source/Resource/AssetManager.cpp:631` `loadTextureBatchIntoMemory()`
- `Engine/Source/Resource/AssetManager.cpp:681` `consumeTextureBatchMemory()`

### 2.3 Cylindrical 源路径

```text
Dirty
  → ResourceResolveSystem::stepEnvironmentLightingSourceLoad()
  → AssetManager::loadTexture(linear)
  → StateTransition.to(PreprocessingEnvironment)
  → enqueueOffscreenTask()
  → EquidistantCylindrical2CubeMap.execute()
  → cubemapTexture
  → beginIrradianceFromResolvedCubemap(cubemapTexture)
```

关键锚点：

- `Engine/Source/ECS/Component/3D/EnvironmentLightingComponent.cpp:96`
- `Engine/Source/ECS/System/ResourceResolveSystem.cpp:186`
- `Engine/Source/Render/Pipelines/EquidistantCylindrical2CubeMap.cpp:170`

### 2.4 Irradiance 生成路径

```text
PreprocessingIrradiance
  → enqueueOffscreenTask()
  → createRenderableSkyboxCubemap()
  → CubeMap2IrradianceMap.execute()
  → irradianceTexture
  → StateTransition.to(Ready)
```

关键锚点：

- `Engine/Source/ECS/System/ResourceResolveSystem.cpp:245`
- `Engine/Source/Render/Pipelines/CubeMap2IrradianceMap.cpp:177`
- `Engine/Source/ECS/Component/3D/EnvironmentLightingComponent.cpp:196`
- `Engine/Source/Runtime/App/RenderRuntime.cpp:165` `findSceneEnvironmentIrradianceTexture()`
- `Engine/Source/Runtime/App/RenderRuntime.cpp:181` `getSceneEnvironmentLightingDescriptorSet()`

### 2.5 渲染侧消费

```text
RenderRuntime
  → 找 scene cubemap / irradiance
  → 若为空则 fallback
  → 若绑定对象变化则 updateEnvironmentLightingDescriptorSet()
```

含义：

1. 环境光照 descriptor set 的更新，本质上已经是“绑定对象变化驱动”。
2. 真正还停留在 tick 轮询上的，是 **EnvironmentLighting resolve 状态推进** 和 **scene skybox 变更检测**。

## 三、现在合理的地方

### 3.1 AssetManager + AssetRef 的职责切分是对的

- 组件层不直接管 decode / GPU 上传。
- `AssetRef` 只做 path + cachedPtr + version 失效判断。
- `AssetManager` 负责 cache、in-flight 去重、回调派发。

### 3.2 Material 上传采用 version 比对是对的

- `Material::setParamDirty()` / `setResourceDirty()` 会递增 version。
- `MaterialDescPool::flushDirty()` 以 uploadedVersion 对比，而不是只看一次性 dirty flag。
- 多个 render consumer 可以各自维护自己的 last uploaded version。

### 3.3 EnvironmentLighting 的 GPU 生命周期控制基本合理

- 中间 texture / imageView 使用 `DeferredDeletionQueue` 回收。
- offscreen task 有 `bCancelled / bTaskFinished / bTaskSucceeded`，至少考虑了异步取消与 GPU 安全释放。

## 四、当前不合理点

### 4.1 最大问题：EnvironmentLighting 仍是 tick 扫描推进，不是 dirty / event 驱动

关键锚点：

- `Engine/Source/ECS/System/ResourceResolveSystem.cpp:128` `onUpdate()`
- `Engine/Source/ECS/System/ResourceResolveSystem.cpp:148` `resolvePendingEnvironmentLighting()`
- `Engine/Source/ECS/System/ResourceResolveSystem.cpp:179` 每帧检查并 `elc.resolve()`

问题本质：

1. `ResourceResolveSystem::onUpdate()` 每帧都会扫所有 `EnvironmentLightingComponent`。
2. 状态机推进依赖“每帧来一遍看看有没有 ready / finished”。
3. 这和材质那套 version/dirty consumer 模式不一致。

这会导致：

- 逻辑入口分散：组件自身一部分，system 一部分，task callback 一部分。
- 行为被 frame tick 驱动，而不是 source change 驱动。
- 后续要支持更多 environment preprocess（prefilter/specular LUT 等）时，状态机会继续膨胀在 `onUpdate()` 里。

### 4.2 scene skybox 变化检测方式不理想

关键锚点：

- `Engine/Source/ECS/System/ResourceResolveSystem.cpp:166`

当前做法：

```cpp
if (elc._lastSceneSkyboxSource != currentSkyboxSource) {
    elc.invalidate();
    elc._lastSceneSkyboxSource = currentSkyboxSource;
}
```

问题：

1. 通过裸指针比较检测变化，语义弱。
2. 每帧轮询而不是由 skybox resolve 完成时主动通知。
3. 如果未来 skybox 支持更多重建路径，这里很容易出现“逻辑上已变化，但指针没变/变化时机不对”的边界问题。

### 4.3 已完成的一步收口：component / system 边界更清晰，但还没完全事件化

目前已经完成：

- component 侧不再持有 pending async/offscreen 细节
- `Skybox` / `EnvironmentLighting` 的 orchestration 已集中到 `ResourceResolveSystem`
- 主流程可直接沿 `switch(resolveState)` 阅读

但当前仍然存在的限制是：

- 状态推进依旧由 `ResourceResolveSystem::onUpdate()` 驱动
- scene-level 依赖变化（例如 scene skybox）仍主要靠 update 中同步检查推进

### 4.4 cubemap 与 irradiance 的产物没有统一进通用资源脏流

当前 environment lighting 的最终消费是 `RenderRuntime` 自己缓存“当前绑定的 cubemap/irradiance texture 指针”并比较变化后更新 descriptor。这个方案能工作，但它是另一条平行链路，不是通用 resource dirty 管理。

这不一定错，但要明确：

- 这是 scene-level environment binding 逻辑
- 不是通用 material/resource slot 逻辑
- 后续若要统一，就要引入 scene lighting runtime state + version，而不是继续加裸指针比较

### 4.4 组件增删入口如果分叉，会绕过资源生命周期

这类资源组件（例如 skybox）在 remove 前可能需要执行额外清理，例如 `prepareForRemove()`、deferred deletion、事件广播。

因此 add/remove 不能在 `Scene`、`ECSRegistry`、editor 等多处各写一套；应保证 mutation 最终共享同一条实现路径。

关键锚点：

- `Engine/Source/ECS/ComponentMutation.h`
- `Engine/Source/Scene/Scene.h`
- `Engine/Source/Core/Reflection/ECSRegistry.h`
- `Engine/Source/Core/Reflection/ECSRegistry.cpp`

## 五、重构建议：从 tick 扫描改成 dirty / event 驱动

### 5.1 目标边界

目标不是去掉所有 update，而是：

1. **去掉无条件全量扫描**
2. **把“是否需要重新生成”改成 source-change 驱动**
3. **把“GPU descriptor 是否需要更新”改成 version / binding change 驱动**

### 5.2 推荐拆分

#### A. Source dirty 层

由以下事件触发 `EnvironmentLightingComponent::invalidate()`：

- 用户修改了 `sourceType / cubemapSource / cylindricalSource / irradianceFaceSize`
- scene skybox runtime cubemap changed
- 相关 asset version changed

这层只负责“标脏”，不直接做全流程推进。

#### B. Resolve job 层

新增更明确的 runtime job / request 概念，例如：

- `EnvironmentLightingBuildRequest`
- `EnvironmentLightingBuildState`

负责保存：

- 当前输入 source identity/version
- 当前 stage
- offscreen task handle / pending result
- 目标 face size / format

让组件不直接持有太多异步细节。

#### C. Runtime state 层

最终产物抽象为 scene-level runtime state，例如：

- `environmentCubemap`
- `irradianceMap`
- `environmentVersion`

渲染侧只看这个 runtime state/version 是否变化。

### 5.3 最小可行改法

如果先做小步重构，建议顺序：

1. 保留现有状态机与 offscreen task。
2. 先把 `sceneSkybox` 变化改成显式 dirty 触发，而不是在 `resolvePendingEnvironmentLighting()` 里每帧比指针。
3. 保持当前 `switch(resolveState)` 主流程不变，继续把状态推进点收口到少量明确 helper，例如：
   - `stepEnvironmentLightingSourceLoad()`
   - `consumeEnvironmentCubemapPreprocess()`
   - `consumeEnvironmentIrradiancePreprocess()`
4. 最后再把 `RenderRuntime` 的 environment DS 更新改成基于 version，而不是裸指针缓存比较。

### 5.4 不建议的方向

1. 不要把 environment lighting 生搬硬套进 `TextureSlot` / material slot 模型。
2. 不要继续在 `ResourceResolveSystem::onUpdate()` 里叠更多 preprocess 分支。
3. 不要把“任务是否完成”的查询散落到 component / system / render runtime 三处。

## 六、快速排查顺序

1. 资源不更新：先看 `AssetManager::getResourceVersion()` 是否变化。  
   参考：`Engine/Source/Resource/AssetManager.h:358`
2. 材质纹理没刷新：看 component 是否重新 `syncTextureSlot()`，以及 runtime material 的 resource version 是否递增。
3. descriptor 没刷新：看对应 consumer 的 `MaterialDescPool::flushDirty()` 是否执行。
4. 环境光照没刷新：看 `EnvironmentLightingComponent::resolveState` 是否卡在某阶段。
5. 辐照图没生效：看 `RenderRuntime::findSceneEnvironmentIrradianceTexture()` 是否取到了最终贴图。

## 七、一句话判断

- **Texture / Material 主链路**：已经比较接近正确的 `authoring source → runtime cache → versioned upload` 模式。  
- **Environment lighting / irradiance 链路**：功能上能跑，但结构上仍偏“tick 驱动状态机”，是后续最值得收敛到 dirty/event 模式的一段。 
