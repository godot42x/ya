---
name: resource-system
description: YA Engine 资源加载、运行时 resolve 与 environment lighting 预处理链路。适用于排查 AssetManager、TAssetRef、Material dirty/version、ResourceResolveSystem、Skybox / EnvironmentLighting 结果同步。
---

## 适用场景

- 用户要求梳理 texture / model / material 的资源加载数据流
- 排查资源热重载、stale pointer、placeholder、descriptor 不更新问题
- 排查 skybox、environment cubemap、irradiance、prefilter 与 scene lighting 不同步问题
- 修改 `ResourceResolveSystem`、`AssetManager`、`TAssetRef`、offscreen preprocess 相关代码

## 当前架构结论

1. `AssetManager` 是资源加载与缓存入口：负责 decode、cache、pending 去重、resource version。
2. `TAssetRef<T>` 是组件侧轻量引用：只持有 path / cachedPtr / resolvedVersion，不直接做 GPU 上传。
3. `ResourceResolveSystem` 只负责**已有组件**的运行时 resolve，不负责 scene topology 创建。
4. `ModelInstantiationSystem` 负责 `ModelComponent` -> 子节点 / 子实体展开。
5. `TextureSlot` 持有 authoring 语义，runtime `Material` 只持有 render-ready binding / cache。
6. 材质上传以 `paramVersion/resourceVersion` + consumer uploaded version 为主，不依赖一次性全局 dirty。
7. `StateTransition<E>` 是 resolve 状态机的统一基础设施。
8. `EnvironmentLighting` 不再是单一 `resolveState`；当前拆成 `sourceState`、`irradianceState`、`prefilterState` 三条分支，并由 `EnvironmentLightingRuntimeState` 持有运行时纹理、pending offscreen job、`resultVersion`。

## 相关 skills

- `build`：资源改动涉及目标、shader 生成、测试时一起看
- `render-arch`：涉及 RenderRuntime、offscreen pipeline、layout 时一起看
- `material-flow`：涉及 TextureSlot、runtime material、descriptor 上传时一起看
- `cpp-style`：做状态机与 runtime state 收口时一起看
- `debug-review`：排查资源失效、descriptor 不更新、崩溃时一起看

## 一、资源加载主链路

### 1.1 Texture：authoring path -> runtime texture binding -> descriptor

```text
TextureSlot
  -> TAssetRef<Texture>::resolve()
  -> AssetManager::loadTexture()
  -> cache / version tracking
  -> TextureSlot::toTextureBinding()
  -> runtime Material::setTextureBinding()
  -> MaterialDescPool::flushDirty()
  -> render consumer 更新 descriptor
```

行为要点：

1. `TAssetRef<Texture>::resolve()` 会先比较 `AssetManager::getResourceVersion()`。
2. `AssetManager::loadTexture()` 会复用缓存并对 in-flight load 去重。
3. 纹理未 ready 时，组件层可以先写 placeholder，避免 descriptor 直接指向空 view。
4. `setTextureBinding()` 后由 runtime material 的版本变化驱动后续上传。

### 1.2 Material：component 是真相源，runtime material 是 render cache

```text
Phong / PBR / Unlit MaterialComponent
  -> syncParamsToMaterial()
  -> syncTextureSlot()
  -> runtime Material param/resource version++
  -> consumer-specific MaterialDescPool::flushDirty()
  -> GPU UBO / descriptor 更新
```

判断原则：

1. Component 保存 authoring 数据与 editor 可编辑语义。
2. runtime `Material` 只作为 GPU 上传前的 cache / binding 容器。
3. 多个 render consumer 各自维护上传版本，不要互相清掉状态。

### 1.3 Model：模型资源加载与子实体展开分离

```text
ModelComponent._modelRef
  -> ModelInstantiationSystem
  -> 生成 child entities
  -> child MeshComponent / MaterialComponent
  -> ResourceResolveSystem resolve 这些已有组件
```

关键点：模型不是“直接进渲染”，而是先决定 topology，再走普通 resolve 流。

## 二、Environment Lighting 当前链路

### 2.1 三段状态，而不是单一 resolveState

当前 component 侧状态：

- `sourceState`：`Empty / Dirty / ResolvingSource / BuildingEnvironmentCubemap / Ready / Failed`
- `irradianceState`：`Empty / Disabled / Dirty / Building / Ready / Failed`
- `prefilterState`：`Empty / Disabled / Dirty / Building / Ready / Failed`

含义：

1. source 负责拿到最终 environment cubemap。
2. irradiance / prefilter 是基于 source 的派生分支，可独立启停。
3. 运行时结果与 pending job 放在 `EnvironmentLightingRuntimeState`，不要重新塞回 component authoring 数据。

### 2.2 Source 分支

```text
SceneSkybox / CubeFaces / Cylindrical
  -> sourceState Dirty
  -> resolve source
  -> 若 cylindrical，则排队 offscreen job 构建 cubemap
  -> sourceState Ready
  -> resultVersion++
```

当前实现要点：

1. `SceneSkybox` 同步是基于 scene skybox 的 `resultVersion`，不是裸指针盲比。
2. `CubeFaces` 直接消费 6 张面贴图。
3. `Cylindrical` 通过 offscreen job 跑 `EquidistantCylindrical2CubeMap`。

### 2.3 Irradiance 分支

```text
source Ready
  -> irradianceState Dirty
  -> 创建 offscreen job
  -> CubeMap2PBRIrradianceMap.execute()
  -> irradianceState Ready
```

规则：

1. `bEnableIrradiance=false` 时应进入 `Disabled`，并回收中间运行时资源。
2. 重新生成前要退休旧 irradiance 纹理，避免悬挂引用。

### 2.4 Prefilter 分支

```text
source Ready
  -> prefilterState Dirty
  -> 创建 offscreen job
  -> 生成 prefilter cubemap
  -> prefilterState Ready
```

当前注意点：

1. 分支结构已经预留，但 `createEnvironmentPrefilterJob(...)` 目前还是未接线占位。
2. 因此如果启用 prefilter，当前重点是先确认是否已经补齐 job，而不是误以为链路天然可用。

## 三、渲染侧消费

```text
RenderRuntime
  -> 查 scene skybox / environment lighting runtime 输出
  -> 绑定 cubemap / irradiance descriptor set
  -> 绑定对象变化时更新 descriptor
```

说明：

1. 材质资源上传和 scene-level environment binding 不是同一条链。
2. 前者依赖 material version；后者更接近 scene runtime state 变化驱动。
3. 修改 scene-level 环境资源时，要同时看 `ResourceResolveSystem` 与 `RenderRuntime` 两边。

## 四、当前高风险点

1. 把 topology 创建和普通 resolve 混在一起。
2. component authoring 数据与 runtime state 双写。
3. 某个 consumer 更新了 descriptor，另一条管线仍持有旧绑定。
4. 只改 `EnvironmentLightingComponent` 状态，不同步运行时纹理 / pending job / resultVersion。
5. 开了 prefilter 分支，却忘了当前 job 仍可能未接线。
6. `Scene`、`ECSRegistry`、editor 等 mutation 入口若分叉，容易绕过统一 add/remove 生命周期。

## 五、快速排查顺序

1. 资源不更新：先看 `AssetManager::getResourceVersion()` 是否变化。
2. 材质纹理没刷新：看 component 是否重新 `syncTextureSlot()`，runtime material 的 resource version 是否递增。
3. descriptor 没刷新：看对应 consumer 的 `MaterialDescPool::flushDirty()` 是否执行。
4. skybox / environment cubemap 没刷新：看 sourceState 是否进入 `Ready`，以及 runtime `resultVersion` 是否推进。
5. irradiance 没生效：看 `irradianceState`、pending offscreen job、`RenderRuntime` 绑定是否同步。
6. prefilter 异常：先确认 job 是否真的已接线，再查 shader / pipeline。

## 六、一句话判断

- **Texture / Material 主链路**：当前是比较稳定的 `authoring source -> runtime cache -> versioned upload` 模式。
- **Environment Lighting 链路**：已拆成 source / irradiance / prefilter 三段状态与运行时结果，但 prefilter job 仍需特别确认是否已接线。
