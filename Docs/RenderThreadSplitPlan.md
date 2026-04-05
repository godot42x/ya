# Game Thread / Render Thread 拆分预研与实施计划

## 1. 目标

当前引擎的主循环仍然是单线程顺序执行：Game Update、Editor、Scene 访问、Render 提交都发生在同一线程上。

未来要做的不是“直接上多线程录制 command buffer”，而是先完成更基础的一层：

- `GT (Game Thread)` 负责 Scene / ECS / 脚本 / 输入 / Editor 交互 / 资源解析状态推进
- `RT (Render Thread)` 负责消费渲染快照、更新 GPU 资源、录制并提交图形命令

这是后续继续做 `RenderGraph`、并行 command recording、异步上传、任务化 culling 的前置条件。

## 2. 当前现状

### 2.1 主循环模型

目前 `App` 仍然是单线程串行推进：

- [`App.cpp`](/C:/Users/dexzhou/1/craft/Neon/Engine/Source/Runtime/App/App.cpp) `tickRender`
- [`App.cpp`](/C:/Users/dexzhou/1/craft/Neon/Engine/Source/Runtime/App/App.cpp) `prepareRenderFrameState`

问题在于：

- GT 和 RT 没有边界
- render 时直接读取 live scene / component
- editor 改动、resource callback、GPU upload 都可能和 scene 访问混在同一帧流程中

### 2.2 RenderRuntime 仍直接依赖 SceneManager / EditorLayer

- [`RenderRuntime.h`](/C:/Users/dexzhou/1/craft/Neon/Engine/Source/Runtime/App/RenderRuntime.h)
- [`RenderRuntime.cpp`](/C:/Users/dexzhou/1/craft/Neon/Engine/Source/Runtime/App/RenderRuntime.cpp)

`RenderRuntime::FrameInput` 里仍直接传：

- `SceneManager*`
- `EditorLayer*`
- camera matrices
- clicked points

这意味着 RT 还不是“消费不可变 frame snapshot”，而是“继续碰业务对象”。如果单独起 render thread，这会立刻引出跨线程读写 Scene 的问题。

### 2.3 Render pipeline 直接遍历场景与系统

- [`ForwardRenderPipeline.cpp`](/C:/Users/dexzhou/1/craft/Neon/Engine/Source/Runtime/App/ForwardRender/ForwardRenderPipeline.cpp)
- [`DeferredRenderPipeline.cpp`](/C:/Users/dexzhou/1/craft/Neon/Engine/Source/Runtime/App/DeferredRender/DeferredRenderPipeline.cpp)
- [`IRenderSystem.h`](/C:/Users/dexzhou/1/craft/Neon/Engine/Source/ECS/System/Render/IRenderSystem.h)

现在的模型是：

- pipeline 在 render 当帧直接 `preparePBR / preparePhong / prepareUnlit`
- render system 在 `tick(ICommandBuffer*, ...)` 内直接访问 live component / material / texture

这是一种“逻辑和渲染没有分层”的结构，单线程可工作，但 GT/RT 拆开后会成为主要障碍。

### 2.4 GPU 同步仍偏保守

- [`RenderRuntime.cpp`](/C:/Users/dexzhou/1/craft/Neon/Engine/Source/Runtime/App/RenderRuntime.cpp) `_render->waitIdle()`
- [`VulkanRender.h`](/C:/Users/dexzhou/1/craft/Neon/Engine/Source/Platform/Render/Vulkan/VulkanRender.h) `flightFrameSize = 1`

目前存在几个直接限制吞吐的点：

- 每帧 render 前后有 `waitIdle()`
- offscreen task 也通过 `waitIdle()` 粗暴串行化
- Vulkan 飞行帧数固定为 1

如果还没把 GT/RT 解耦，就算单独开一个 render thread，RT 也会频繁被 GPU 全局同步卡死。

### 2.5 异步资源系统只有“CPU 异步”，GPU 创建仍回主线程

- [`TaskQueue.h`](/C:/Users/dexzhou/1/craft/Neon/Engine/Source/Core/Async/TaskQueue.h)
- [`AssetManager.cpp`](/C:/Users/dexzhou/1/craft/Neon/Engine/Source/Resource/AssetManager.cpp)
- [`App.h`](/C:/Users/dexzhou/1/craft/Neon/Engine/Source/Runtime/App/App.h)

当前资源加载模型：

- worker thread 做 decode / assimp load
- 完成后通过 main-thread callback 执行 GPU upload / cache publish

这对现在是合理的，但如果将来 RT 独立，GPU upload 的归属应该从“主线程”切到“render thread”。

## 3. 拆分 RT / GT 后，目标线程模型应是什么

建议采用最小可落地模型，而不是一步到位做复杂 job graph。

### 3.1 线程职责

`GT`:

- 输入
- editor UI 状态更新
- scene / ECS / transform / script / physics
- 资源 authoring 状态推进
- 生成 render snapshot
- 提交 snapshot 给 RT

`RT`:

- 消费 snapshot
- 处理 shader/pipeline 延迟重建
- 处理 GPU resource create / upload / destroy
- 录制 command buffer
- submit / present

### 3.2 关键原则

必须坚持三条原则：

1. RT 不直接读写 live scene。
2. GT 不直接操作 Vulkan 对象。
3. GT/RT 之间传递的是 frame data / render proxy / command，不是 ECS 指针。

## 4. 推荐演进路线

不要一上来改 Vulkan backend。第一阶段先做“数据边界”，第二阶段再做“线程边界”。

### 阶段 A: 先把 RenderRuntime 改成 snapshot consumer

这是第一优先级，也是第一步。

目标：

- 让 `RenderRuntime` 不再依赖 `SceneManager*` / `EditorLayer*`
- 每帧只吃一个 `RenderFrameSnapshot`

建议新增：

- `RenderFrameSnapshot`
- `RenderViewData`
- `RenderSceneSnapshot`
- `RenderDebugSnapshot`
- `RenderResourceUpdateQueue`

`RenderFrameSnapshot` 至少应包含：

- camera/view/projection
- viewport info
- skybox / lights / mesh draw list
- material instance resolve 结果
- debug draw / editor overlay 数据
- 本帧需要执行的 GPU 资源更新命令

这一步完成后，即使仍然单线程，渲染层也已经从 ECS live data 脱钩。

### 阶段 B: 建立 RenderProxy / Snapshot 层

要避免 RT 访问原始组件，必须显式建立代理层。

建议拆成两类数据：

- 长寿命 `RenderProxy`：Mesh / Material / Texture / Skybox 等对象的渲染镜像
- 短寿命 `FrameSnapshot`：每帧相机、可见实例、light list、debug overlay

例如：

- `MeshComponent` 不直接给 RT，用 `MeshRenderProxyHandle`
- `PhongMaterialComponent` 不直接给 RT，用 `MaterialProxyHandle`
- `SkyboxComponent` 不直接给 RT，用 `SkyboxProxy`

GT 更新 proxy 的 authoring state，RT 负责把 proxy 对应的 GPU state 真正落地。

### 阶段 C: 引入双缓冲 / 三缓冲 Frame Snapshot

在 GT/RT 正式分线程前，要先定义 frame handoff。

推荐：

- GT 写 `Frame N+1 snapshot`
- RT 读 `Frame N snapshot`
- 使用双缓冲起步，必要时扩成三缓冲

需要明确：

- snapshot 生命周期
- RT 消费完成前 GT 不覆盖该槽位
- scene 切换 / play mode 切换 / viewport resize 时的 flush 策略

### 阶段 D: 独立 Render Thread

当 snapshot 模式跑通后，再真正拆线程。

建议增加：

- `RenderThread` 对象
- `RenderThreadCommandQueue`
- `RenderFrameMailbox`

`App` 主循环变为：

1. GT 更新 scene
2. GT 构建 snapshot
3. GT 推送 snapshot 给 RT
4. RT 唤醒并渲染
5. GT 继续下一帧 update，不等待 RT 立即完成

这一阶段先允许 GT/RT 在 frame boundary 进行保守同步，不急着追求最低延迟。

### 阶段 E: 把 GPU 创建/销毁都收口到 RT

这是第二个硬边界。

当前很多 GPU 创建路径是“主线程 callback + 立即创建”，未来要改成：

- worker thread: decode / import
- GT: 发布逻辑资源状态
- RT: 真正执行 GPU upload / descriptor rebuild / deferred deletion

这样可以避免：

- GT 持有 Vulkan 上下文职责
- 跨线程直接触碰 descriptor set / image view / pipeline
- scene unload 与 GPU in-flight 资源冲突

### 阶段 F: 再考虑 RT 内部并行录制

这是更后面的事，不是第一阶段目标。

当 RT 已经独立后，再做：

- shadow pass / gbuffer pass / postprocess 的任务切分
- secondary command buffer
- culling / draw packet sorting 并行化
- render graph pass compilation

## 5. 第一阶段具体要改什么

### 5.1 第一阶段的核心成果

第一阶段不是“开线程”，而是把下面三件事做出来：

1. `RenderRuntime` 不再依赖 live scene。
2. `RenderPipeline` 不再自己遍历 ECS 组装 draw data。
3. `App` 能在单线程下构建并提交一份 `RenderFrameSnapshot`。

### 5.2 第一阶段建议落点

建议从下面这些点开始改：

- 新建 `Engine/Source/Render/RHI/RenderFrameSnapshot.h`
- 新建 `Engine/Source/Render/RHI/RenderSceneExtractor.*`
- 修改 [`RenderRuntime.h`](/C:/Users/dexzhou/1/craft/Neon/Engine/Source/Runtime/App/RenderRuntime.h)
- 修改 [`RenderRuntime.cpp`](/C:/Users/dexzhou/1/craft/Neon/Engine/Source/Runtime/App/RenderRuntime.cpp)
- 修改 [`App.cpp`](/C:/Users/dexzhou/1/craft/Neon/Engine/Source/Runtime/App/App.cpp)

先做一个最小版本 snapshot：

- camera
- viewport
- directional light / point lights
- visible mesh instances
- skybox texture handle
- editor viewport debug info

先不追求全量覆盖所有系统，先让 forward/deferred 至少有一条路径能切到 snapshot。

## 6. 风险点

### 6.1 最大风险不是线程，而是对象所有权

如果 RT 仍拿着这些东西，就会持续出问题：

- `Scene*`
- `Entity*`
- `Component*`
- `Texture*` 但其内部状态由 GT 改写
- `MaterialComponent*`

要避免的是“看起来只读，实际上共享可变对象”。

### 6.2 Editor 与 Runtime Scene 切换是特殊场景

当前已经有 editor scene / play scene clone 模式，这是对的。

但 GT/RT 拆分后需要额外处理：

- scene 切换时 RT 仍可能持有上一帧 snapshot
- play mode enter/exit 时 proxy 重建与旧 GPU 资源回收
- inspector 改材质后，RT 应只看到代理层变更而非 live component 突变

### 6.3 资源上传路径要避免“谁都能直接创 GPU 对象”

如果不收口，后期会变成：

- worker thread 有时碰 GPU
- GT 有时碰 GPU
- RT 也碰 GPU

这样最后基本不可维护。

## 7. 建议的实施顺序

### Step 1

定义 `RenderFrameSnapshot`，让 `RenderRuntime::renderFrame()` 改为只接收 snapshot。

这是必须先做的第一步。

### Step 2

增加 `RenderSceneExtractor`，在 GT 上把 active scene 提取成 draw list / light list / skybox data。

### Step 3

修改 forward/deferred pipeline，让它们消费 snapshot，而不是自己访问 scene。

### Step 4

把资源异步回调里的 GPU upload 入口从“主线程 callback”改造成“RT resource command queue”。

### Step 5

实现 `RenderThread` 与 frame mailbox，正式把 `tickRender()` 从 GT 挪出去。

### Step 6

去掉保守的 `waitIdle()` 和 `flightFrameSize = 1`，改成真正的 per-frame fence / resource lifetime 管理。

### Step 7

稳定之后，再推进 RT 内部多线程录制和 render graph。

## 8. 第一阶段验收标准

满足以下条件，才算可以进入“真正分线程”：

- `RenderRuntime` 不再接受 `SceneManager*`
- `RenderRuntime` 不再依赖 `EditorLayer*`
- pipeline 不再直接遍历 ECS 组装渲染对象
- GT 可以在单线程下产出完整 snapshot
- RT 仅靠 snapshot 能完成一帧绘制
- scene 切换 / play mode 切换不会因 snapshot 悬挂指针崩溃

## 9. 结论

对这个仓库来说，拆 GT/RT 的第一步不是 Vulkan 线程化，也不是 secondary command buffer，而是先把“渲染输入数据”从 Scene/ECS live object 中剥离出来。

一句话总结实施策略：

先做 `snapshot/proxy`，再做 `render thread`，最后再做 `RT 内并行`。




### Snapshot 包含什么

  原则只有一条：只放“这一帧渲染必需的数据”，不要放 authoring 数据，不要放 ECS 指针。

  第一版建议只包含这些：

  - View/Camera
      - view
      - projection
      - cameraPos
      - viewportRect
      - viewport scale
  - Light list
      - directional light 的方向、颜色、强度
      - point light 的位置、颜色、强度
  - Draw list
      - 每个可见 draw item 的 world matrix
      - mesh handle / submesh index
      - material proxy handle
      - shading model / pipeline key
      - sort key
  - Skybox
      - 当前要用的 skybox texture/cubemap handle
  - Frame constants
      - 时间、debug flags、pass 开关、阴影开关
  - Editor overlay/debug
      - gizmo、debug line、billboard、viewport debug slot 这种纯渲染输入

  不要放这些：

  - Scene*
  - Entity*
  - Component*
  - entt::entity
  - 直接可变的 MaterialComponent*
  - 带业务行为的对象

  一句话说，snapshot 更像“渲染命令输入表”，不是“scene 副本”。

### Proxy 要不要每个 component 都写

  不需要。只有“会被 RT 跨帧持有或频繁读取的渲染相关对象”才值得做 proxy。

  第一批建议只做这几类：

  - Mesh / Model
  - Material
  - Texture
  - Skybox
  - 必要时 Skeleton/Animation render data

  而且 proxy 通常不是按“component 类型”一比一做，而是按“渲染资源类型”做。

  例如：

  - MeshComponent 不一定有自己独立 proxy
      - 它更像引用 MeshProxyHandle + MaterialProxyHandle + Transform
  - TransformComponent 也不需要长期 proxy
      - world matrix 直接进每帧 snapshot 就够了
  - DirectionalLightComponent、PointLightComponent
      - 第一阶段甚至不需要长期 proxy，直接提取进 frame snapshot 即可
  - CameraComponent
      - 通常也不做长期 proxy，直接进 snapshot

  真正适合长期 proxy 的，是这些“重资源”：

  - GPU buffer / image 对应物
  - material shader permutation / descriptor layout 对应物
  - texture/sampler/descriptors 对应物
  - skybox cubemap / BRDF LUT / reflection capture 这类跨帧资源

  一个更实用的划分

  可以按三层理解：

  1. Authoring Component
      - ECS 里的原始组件
      - 给 GT、Editor、序列化使用
  2. Render Proxy
      - RT 可长期持有的渲染镜像
      - 例如 MaterialProxy、TextureProxy、MeshProxy
  3. Frame Snapshot
      - 每帧临时组装
      - 例如 DrawItem { worldMatrix, meshProxy, materialProxy }

  这套分层里，大多数 component 不需要单独 proxy。

  怎么判断一个 component 要不要 proxy

  用这三个判断标准就够了：

  - RT 是否需要跨帧持有它
  - 它是否对应 GPU 资源或 descriptor/pipeline 状态
  - 它是否更新频率低、但构建成本高

  满足就做 proxy，不满足就直接提取成 snapshot 字段。

  所以：

  - TransformComponent：不要 proxy，进 snapshot
  - CameraComponent：不要 proxy，进 snapshot
  - LightComponent：前期不要 proxy，进 snapshot
  - Mesh/Material/Texture/Skybox：做 proxy

  对你这个仓库，第一步最小落地建议

  先不要铺太大。第一版只做：

  - RenderFrameSnapshot
  - RenderDrawItem
  - MaterialProxyHandle
  - MeshProxyHandle
  - SkyboxProxyHandle

  然后 GT 从 scene 提取：

  - camera
  - light list
  - visible draw items
  - skybox

  这样 forward/deferred 就已经能脱离 Scene* 直接渲染了。