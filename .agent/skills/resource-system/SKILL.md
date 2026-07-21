---
name: resource-system
description: YA Engine 资源加载、运行时 resolve 与 environment lighting 链路。适用于排查 AssetManager、TAssetRef、ResourceResolveSystem、材质资源上传与 scene-level 环境贴图同步。
---

## 适用场景

- 用户要求梳理 texture / model / material 的资源加载数据流
- 排查资源热重载、stale pointer、placeholder、descriptor 不更新问题
- 排查 skybox、environment cubemap、irradiance、prefilter 与 scene lighting 不同步问题
- 修改 `AssetManager`、`TAssetRef`、`ResourceResolveSystem`、offscreen preprocess 相关代码

## 先判断是不是这里

留在本 skill：

- 资源是否 resolve 成功
- 版本是否推进
- descriptor / GPU 资源是否刷新
- environment lighting 的运行时结果是否真的生成并被消费

转去 `material-flow`：

- 谁是材质 authoring 真相源
- editor 修改路径该落到 component 还是 runtime material
- pipeline 是否越界直接读 component

## 当前稳定边界

1. `AssetManager` 是资源加载、缓存、pending 去重与 `resourceVersion` 的入口。
2. `TAssetRef<T>` 是组件侧轻量引用，只持有 path / cachedPtr / resolvedVersion。
3. `ResourceResolveSystem` 只负责**已有组件**的运行时 resolve，不负责 scene topology 创建。
4. `ModelInstantiationSystem` 负责 `ModelComponent` -> 子节点 / 子实体展开，再交给普通 resolve 链。
5. `TextureSlot` 的 authoring 语义归 component；这里只关心它如何变成 runtime binding。
6. 材质上传依赖 `paramVersion/resourceVersion` 与 consumer 自己的 uploaded version，不依赖“一次性全局 dirty”。
7. `EnvironmentLighting` 是 source / irradiance / prefilter 三段分支；运行时纹理、pending job、`resultVersion` 属于 runtime state，不回写 authoring 数据。

## 主链路

### Texture

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

要点：

1. `resolve()` 先比较 `AssetManager::getResourceVersion()`。
2. `loadTexture()` 应复用缓存并对 in-flight load 去重。
3. 未 ready 时可先落 placeholder，避免 descriptor 指向空 view。
4. 纹理更新最终靠 runtime material 的 version 推动上传。

### Material

```text
MaterialComponent
  -> syncParamsToMaterial() / syncTextureSlot()
  -> runtime Material param/resource version++
  -> consumer-specific MaterialDescPool::flushDirty()
  -> GPU UBO / descriptor 更新
```

要点：

1. 这里的重点是 runtime `Material` 作为上传前 cache / binding 容器。
2. 多个 render consumer 各自维护上传版本，不能互相清状态。
3. authoring 语义与 editor 修改链路交给 `material-flow`。

### Model

```text
ModelComponent._modelRef
  -> ModelInstantiationSystem
  -> child MeshComponent / MaterialComponent
  -> ResourceResolveSystem resolve 这些已有组件
```

关键点：模型先决定 topology，再走普通 resolve；不要把两件事揉进一个系统。

## Environment Lighting

只保留稳定判断，不在这里堆当前实现细节。

### 结构规则

1. source 负责拿到最终 environment cubemap。
2. irradiance / prefilter 是基于 source 的派生结果，可独立启停。
3. 三段状态与运行时结果分离：component 保存 authoring 选择，runtime state 保存纹理、pending job、`resultVersion`。

### Source 分支

```text
SceneSkybox / CubeFaces / Cylindrical
  -> sourceState Dirty
  -> resolve source
  -> 必要时排队 offscreen job 构建 cubemap
  -> sourceState Ready
  -> resultVersion++
```

### 派生分支

```text
source Ready
  -> irradianceState / prefilterState Dirty
  -> 创建 offscreen job
  -> 生成派生 cubemap
  -> Ready
```

规则：

1. `Disabled` 必须真的停用并回收对应运行时结果。
2. 重新生成前要退休旧纹理，避免悬挂引用。
3. 不能因为状态分支存在，就假设 job 已经接线且结果已经生成。

## 渲染侧消费

```text
RenderRuntime
  -> 查 runtime skybox / environment lighting 输出
  -> 绑定 cubemap / irradiance / prefilter 相关资源
  -> 绑定对象变化时更新 descriptor
```

说明：

1. scene-level environment binding 和材质纹理上传不是同一条链。
2. 前者更接近 runtime state 变化驱动，后者依赖 material version。
3. 环境贴图问题通常要同时看 `ResourceResolveSystem` 与 `RenderRuntime`。

## 高风险模式

1. 把 topology 创建和普通 resolve 混在一起。
2. component authoring 数据与 runtime state 双写。
3. 某个 consumer 更新了 descriptor，另一条管线仍持有旧绑定。
4. 只改 component 状态，不同步 runtime 纹理 / pending job / `resultVersion`。
5. mutation 入口分叉，绕过统一 add/remove 或 resolve 生命周期。

## 快速排查顺序

1. 资源不更新：先看 `AssetManager::getResourceVersion()` 是否变化。
2. 材质纹理没刷新：看 component 是否重新 `syncTextureSlot()`，runtime material 的 `resourceVersion` 是否递增。
3. descriptor 没刷新：看对应 consumer 的 `MaterialDescPool::flushDirty()` 是否执行。
4. skybox / environment cubemap 没刷新：看 sourceState 是否进入 `Ready`，以及 runtime `resultVersion` 是否推进。
5. irradiance / prefilter 没生效：看分支状态、pending offscreen job 和 `RenderRuntime` 绑定是否同步。

## 相关 skills

- `material-flow`：authoring 真相源、editor 修改链路、runtime material 边界
- `render-arch`：RenderRuntime、offscreen pipeline、layout、后端消费
- `debug-review`：崩溃、自检、回归排查
- `ya-build`：资源改动涉及构建、shader 生成、测试时一起看

## 退出条件

- 已明确问题属于 resolve、runtime cache、descriptor 上传，还是 environment lighting 运行时结果
- 已定位主要责任层：`AssetManager`、`TAssetRef`、`ResourceResolveSystem`、`ModelInstantiationSystem` 或 `RenderRuntime`
- 已知道下一步是继续改资源链路，还是转去 `material-flow` / `render-arch` / `debug-review`
