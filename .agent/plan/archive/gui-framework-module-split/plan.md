# 引擎模块化 / GUI 框架抽取计划

> **状态：2026-08-09 全部条目闭合。** 本计划的目标已由
> `module-boundary-cleanup` 计划吸收并完成（Phase 5.5 GUI 闭包拆分、
> Phase 6 转发头体系、Phase 7 app-services、Phase 8 闭包测试与 CI matrix）；
> 所有 [x] 条目的完成证据见该计划与 `Script/ci.sh all` 全量矩阵。
> 剩余仅按需事项：职责聚合头（决策：不补）、统一 feature manifest
> （决策：保持 profile 分支 + shader manifest 现状）。
> 状态：2026-08-08 建立。
> 2026-08-08 第二轮 review 迭代：修正 DeferredDeletionQueue 归属、补充
> Physics/IWindowProvider/RenderRuntime 三处耦合、细化 Phase 1/2、固化争议点
> 默认决策（见 §9）。
> 用户决策：渲染抽象层命名为 **RHI**（`render(-backend)` → `rhi(-backend)`）；
> RDG 不引入 2D/UI 层；GUI 框架与 3D/gameplay 模块彻底解耦。

## 1. 目标

把 YA Engine 从"一个大静态库（ya-engine）"拆成按职责分层的模块集，使得：

1. **GUI 框架可独立消费**：GUI 框架**自有模块**为 `libya-ui` +
   `libya-ui-scene`（2D 批渲染、字体/纹理、Node2D 场景树、UI 组件、事件
   路由与合成 pass）。它依赖的 `core` / `rhi` / `rhi-backend` 是**共享
   基础设施**，不属于 GUI 框架本身——UI 与 game/3D 同等依赖 RHI。
   宿主消费时的链接集合为 `core + rhi + rhi-backend + ui + ui-scene`。
2. **3D / gameplay 仍是完整引擎的一部分**，但与 GUI 闭包无依赖交叉。
3. **构建增量与链接体积受益**：改 gameplay 不重编 ui；GUI 宿主不链入
   physics / ECS / render-3d / editor 的任何对象。

## 2. 已完成的前置重构（2026-08-08，4 个 commit）

本计划依赖的 viewport 编排收口已提交：

- `929c4489 [render/2d]` Render2D 收敛 pass pipeline 准备并实例化录制会话
  （`FRender2dSession` / `FRender2dDebugState`，begin/draw 会话断言）
- `3870b568 [render/ui]` 抽出共享 2D compose pass 统一三场景合成入口
  （`ERender2DComposePassKind` / `recordRender2DComposePass`）
- `003142ce [render/runtime]` 收口 viewport 编排：统一 display target、
  module viewport compose 钩子（`onViewportCompose`）、world 渲染开关
- `10c9b109 [editor]` editor compose 迁入 `onViewportCompose`，2D 模式自建
  canvas target 并裁剪 world 渲染

这些改动使 GUI 侧的 pass 语义、编排顺序、录制生命周期都集中在
`RenderViewportOverlayRecorder` + `RenderRuntime::renderFrame`，是拆库的
前置条件。

## 3. 模块地图（决策）

### 3.1 分层与依赖方向

**基础设施链**（共享，不属于任何上层模块）：

```text
libya-core            日志/反射/类型/FName/事件/ResourceRegistry/TaskQueue
  ▲
libya-rhi             硬件抽象：IRender/CommandBuffer/Buffer/Texture/Descriptor/
                      Pipeline/RenderImage/Swapchain/ResourceFactory/
                      IWindowProvider（自根 WindowProvider.h 下沉）
  ▲
libya-rhi-backend     Vulkan / OpenGL 平台实现（宿主选择其一）
```

**上层消费者**（平级，都依赖 core + rhi；RHI 不归属其中任何一个）：

```text
libya-ui              FontManager/TextureLibrary + Render2D/FQuadRender/
                      FLineRender（GUI 框架自有；ui-resource 合并进本库）
  ▲
libya-ui-scene        Node2D/UIPanel/UIButton/UIText/UISceneRenderer/
                      compose pass（GUI 框架自有）

libya-scene-core      Node 基类/场景图基础（无 3D 内容）
  ▲
libya-scene-3d        Node3D/Transform/世界矩阵

libya-ecs             EnTT 封装 + IComponent/ISystem 基类
libya-resource        AssetManager/Mesh/Material/Texture 加载
  ▲
libya-gameplay        Transform/Animation/Scripting/ModelInstantiation/
                      ResourceResolve + Physics 集成
libya-render-graph    RDG（构图/executor/import-export，通用工具）
  ▲
libya-render-3d       RenderRuntime/Forward/Deferred pipeline/stage/
                      Shadow/SSAO/PostProcess/EntityId/ViewportOverlay
  ▲
libya-host            窗口/输入/App/AppFrameLoop/presentation/automation
  ▲
libya-editor          ImGui 编辑器（宿主应用）
```

依赖规则：每条边只向下；任何向上引用都是模块边界违规。
补充显式边：`libya-render-3d` 与 `libya-host` 依赖 `libya-ui`（RenderRuntime
调用 compose/prepare，AppFrameLoop 调用 Render2D::onUpdate）——向下，允许。

### 3.2 RHI 边界（关键决策）

**定位**：RHI 是引擎的共享基础设施层（类似平台层），**不是 GUI 框架的组成
部分**——UI、resource、render-3d、gameplay 都依赖它。GUI 框架自有模块只有
`libya-ui` 与 `libya-ui-scene`。

**进 RHI**：`IRender`、`ICommandBuffer`、`IBuffer`、`IImage/IImageView/ITexture`、
Descriptor 三件套、`IPipelineLayout/IGraphicsPipeline`、`IRenderTarget/IRenderPass`
（仍是有效抽象）、`RenderImage`、`Swapchain`、`ResourceFactory`、
`IWindowProvider`（当前定义在引擎源码根 `WindowProvider.h`，拆库时下沉到 rhi；
VulkanRender 持有 `nativeWindow + IWindowProvider`，rhi-backend 不能反向依赖 host）。

**DeferredDeletionQueue 归 Core 而非 RHI**：它在 `Resource/DeferredDeletionQueue.h`，
是通用模板工具（`retireResource(shared_ptr<T>)`），不依赖任何 RHI 类型。

**不进 RHI**：

- `Render/Core/Graph/*`（RDG）→ 归 `libya-render-graph`（3D 构图工具；GUI 不用）
- `Render/2D/*` → 归 `libya-ui`（目录随库迁移，`Render/` 收敛为纯 RHI）
- shader 生成链（Slang/GLSL）→ 构建期工具 `render-shaders`，不属运行时模块

### 3.3 3D 渲染模块内部划分（libya-render-3d）

- pipeline 编排：`RenderRuntime`、Forward/Deferred pipeline、`IRenderPipeline`、
  viewport snapshot 服务
- stage：GBuffer / Light / Shadow / SSAO / Overlay / PostProcess / EntityId，
  每 stage 自持资源与帧输入
- graph 层独立为 `libya-render-graph`
- shader 产物按消费方分目录：Sprite2D 归 ui，其余归 render-3d
- 决策：`RenderRuntime` 归 render-3d，不归 host；GUI compose pass 只对接
  `ICommandBuffer + RenderImage`，不依赖 RenderRuntime
- 内部解耦项：`RenderRuntime.h` 目前 include
  `Deferred/DeferredPipelineDebugViews.h`（编排层直接依赖 Deferred 内部）。
  拆 render-3d 时把 debug views 提为共享头，或让 RenderRuntime 通过接口取
  debug 视图，消除编排层对具体 pipeline 内部的依赖。

### 3.4 gameplay 模块划分

- `libya-ecs`：GUI 框架不用 ECS（UISceneRenderer 直接遍历 Node2D 树）
- `scene-core` 与 `scene-3d` 分离：**Node2D 只依赖 scene-core 的 Node 基类**，
  不能因继承链把 Node3D 拖进 GUI 宿主（Node3D 当前与 Node 同文件，需移出，
  见 Phase 1）
- gameplay systems：Transform/Animation/Scripting/ModelInstantiation/
  ResourceResolve（cubemap/irradiance 预处理归 render-3d 的离屏服务）
- `libya-physics`：独立库，只被 gameplay 引用
- 数据桥：`RenderFrameExtractor`（registry → RenderFrameData）建议独立为
  `render-adapters` 或放 host，不塞进 gameplay / render-3d
- **Physics → Render2D 反向依赖（拆库前必须解耦）**：`PhysicsDebugDraw.cpp`
  直接 include `Render/2D/Render2D.h` 画 world wireframe
  （`makeWireSphere/makeWireBox`）。GUI 闭包不含 Physics，Physics 不能反向
  依赖 Render2D。方案：world line 能力留在引擎侧，PhysicsDebugDraw 通过
  注入的 line 收集器（回调/接口）绘制，不再 include Render2D.h。

## 4. 剔除清单（GUI 闭包不包含）

- 3D 渲染：Deferred/Forward/Shadow/SSAO/PostProcess/EntityIdViewportPass
- ECS 渲染链路：RenderFrameExtractor、3D 组件（Mesh/Model/Terrain/Skybox/Light/
  Animation）、ResourceResolveSystem 的 3D 部分
- 物理：`Physics/` 全部
- 编辑器：`Editor/` 全部；`Runtime/GUI` 的 ImGui 封装归工具（框架自身 UI
  不依赖 ImGui，维持 game-ui-rendering 计划的边界）
- Render2D 的 world 能力：`makeWorldSprite` / `makeWorldLine` / `makeWireBox` +
  world vertex 流 + `_worldPipeline`（依赖相机，属 3D debug；如保留 line 只留
  screen-space 部分）
- `recordRenderViewportOverlayPass`（world overlay）留在引擎；`recordRender2DComposePass`
  进框架
- `ECS/System/2D/UIComponentSytstem.cpp`（全注释残留）随 ECS 剔除

判定标准：凡 include 链触达 `Render/Shader/`、`Runtime/Rendering/Deferred|Forward`、
`Physics/`、`ECS/Component/(Mesh|Terrain|Light|Skybox)` 的文件不进 GUI 库。

## 5. 实施步骤

### Phase 0 —— 依赖收敛（先做，低风险）

- [x] 删除 `Render2D.h` 死 include：`RenderOverlay.h`、`Render/Stage/IRenderStage.h`
      （均已确认内容未使用）
- [x] `Render2D.h` 瘦身：`Resource/Font/FontManager.h`、`Resource/Texture/TextureLibrary.h`
      只被 cpp 使用（`TextureBinding` 在 `Texture.h`；`Font` 可前置声明），
      从头移除；`Render/Render.h` 改前置声明 `IRender*`
- [x] **Phase 2 默认不迁目录**（决策见 §9-C）：`Render/2D/` 与
      `Render/Core/Graph/` 目录暂留，仅拆 xmake target，include 路径零改动；
      目录物理收敛列为可选收尾阶段
- [x] 头文件卫生检查：GUI 库 public 头禁止 include 3D/physics/ECS 头
      （复用 xmake 现有 `check_runtime_source_isolation()` 模式）

### Phase 1 —— scene-core 剥离

- [x] **把 `Node3D` 移出 `Node.h` 到独立 `Node3D.h`**（当前两者同文件，
      Node3D 从 103 行起；Node 基类本身干净，仅前置声明 `Entity*`，但
      ui-scene include Node.h 会连带编译 Node3D 声明）
- [x] Node 基类 + 场景图基础归 scene-core；确认 Node2D 依赖闭包 =
      Node 基类 + UIBase（已验证：`Node2D.h` 只 include
      Core/AssetRef + Core/Event + Core/Reflection + Core/UI/UIBase +
      Scene/Node.h，不触达 ECS/资源系统）
- [x] `UISceneRenderer` / `Node2D` 头搬入 ui-scene 分组

### Phase 2 —— GUI 闭包拆库（xmake）

- [x] `Engine/YA.xmake.lua` 新增 target：基础设施库 `ya-core` / `ya-rhi` /
      `ya-rhi-backend`（共享，非 GUI 框架组成）+ GUI 框架自有库 `ya-ui`
      （含 FontManager/TextureLibrary）、`ya-ui-scene`（static，
      `add_deps` 表达层级）——ui-resource 不单拆（决策见 §9-A）
- [x] **对外导出不变**：`ya-engine` 保持 `set_kind("shared")` 聚合导出
      （`add_deps` 上述库），editor/example 链接方式与 `ENGINE_API` 边界不变；
      内部按模块拆库只为增量编译与按需链接（决策见 §9-B）
- [x] unity_group 按 target 分组，改 ui 只重编 ui 闭包
- [x] 导出宏按库拆分（`YA_CORE_API`/`YA_RHI_API`/`YA_UI_API`/`YA_RENDER3D_API`），
      拆分期间 `ENGINE_API` 保留为聚合导出别名
- [x] 单例归属固定：`Render2D::quadData`、`FontManager::get()`、
      `TextureLibrary::get()` → ya-ui；`AssetManager` → ya-resource
      （遵守 windows DLL boundary；对外 shared 边界只有 ya-engine 一个，
      单例自然唯一）
- [x] 包依赖按库收敛（见 §6 清单）：GUI 闭包只挂
      sdl3/glm/freetype/vulkansdk/vma/glad，不挂 entt/lua/sol2/jolt 等
- [x] 新增 GUI 闭包测试 target（`Node2DFactoryTest`/`Node2DLayoutTest`/
      `UISceneRendererTest` 只链 GUI 闭包）——验证"无 3D 依赖"的最强手段

### Phase 3 —— 3D / gameplay 拆库

- [x] `ya-resource` / `ya-ecs` / `ya-scene-core` / `ya-scene-3d`
- [x] `ya-render-graph`（RDG 从 `Render/Core/Graph` 迁出）
- [x] `ya-gameplay` / `ya-physics`
- [x] `ya-render-3d`（RenderRuntime + pipeline + stage；先解耦
      RenderRuntime.h 对 DeferredPipelineDebugViews 的 include）
- [x] `RenderFrameExtractor` 数据桥归位（render-adapters 或 host）
- [x] PhysicsDebugDraw 改注入 line 收集器，移除对 Render2D.h 的 include

### Phase 4 —— 宿主与验证

- [x] `ya-host`（窗口/输入/App/presentation/automation）+ `ya-editor`
- [x] 最小 GUI 宿主示例 target：只 `add_deps` GUI 闭包 + rhi-backend，
      （链接集合 = core + rhi + rhi-backend + ui + ui-scene），
      验证不链接 physics/ECS/3D 也能运行
- [x] shader 生成按消费方分组，GUI 宿主构建跳过 3D shader 组

## 6. 构建 / 链接优化

- 静态库按需拉对象：GUI 宿主不会把 physics/3D 代码链进产物
- 平台后端独立库：宿主可只链 Vulkan 后端，不链 OpenGL
- 头文件收敛是编译性能瓶颈：胖 include 前置声明化后，改 UI 头只重编 ui 闭包
- unity build 按 target 分组；**已知坑**：unity 缓存不感知源文件内容变化，
  出现"没重新编译"假象时用 `python3 Script/ya.py build --editor --build-arg=-r`
  强制 rebuild
- shader 生成：全局生成器保留，产物按消费方分目录，GUI 宿主可跳过 3D 组
- **包依赖拆分清单**（当前 `ya-engine` 一个 target 挂全量包）：
  - GUI 闭包只需：`libsdl3`（窗口）、`glm`、`freetype`（字体）、
    `vulkansdk`、`vulkan-memory-allocator`、`glad`
  - 引擎其余保留：`entt`、`lua`、`sol2`、`joltphysics`、`assimp`、`ktx`、
    `tinygltf`、`quickjs-ng`、`asio`、`nlohmann_json`、`cxxopts` 等
  - 拆分后 GUI 宿主构建不再下载/解析 3D 相关包，依赖解析与编译时间同时受益
- **测试拆分**：GUI 闭包测试 target（Node2D/UISceneRenderer/Render2D）只链
  GUI 闭包，既是验证也是防止回归的护栏

## 7. 风险与坑

1. **单例归属**：`Render2D` 静态全局状态、`FontManager::get()`、
   `TextureLibrary::get()` 拆库后必须单一 owner，避免多 DLL 各持一份
   （windows DLL boundary memory）。
2. **Node2D 继承链**：Node2D → Node 的依赖若混入 3D 内容，GUI 闭包会被污染；
   Phase 1 必须先验证。
3. **聚合 target 兼容**：`ya-engine` 拆库后保持对外接口不变，Editor/示例
   不需要改构建入口；每 Phase 结束跑一次 `build --editor` 验证。
4. **RenderRuntime 边界**：compose pass 不能反向依赖 RenderRuntime；
   新增 UI pass 时只允许对接 `ICommandBuffer + RenderImage`。
5. **world overlay 保留在引擎**：`recordRenderViewportOverlayPass` 与
   world sprite/line 能力留在 render-3d 侧，不要顺手带进 GUI 闭包。
6. **Physics 反向依赖**：`PhysicsDebugDraw` 依赖 `Render2D`。若不先解耦
   （注入 line 收集器），拆库时 GUI 闭包会被 Physics 污染。
7. **窗口抽象归属**：`IWindowProvider` 目前在源码根 `WindowProvider.h`，
   拆 rhi-backend 前必须下沉到 rhi，否则 rhi-backend 反向依赖 host。
8. **shared 导出边界**：当前 `ya-engine` 是 shared；拆库期间保持单一 shared
   聚合导出，避免多个 DLL 边界同时出现（放大 windows DLL boundary 风险）。

## 9. 决策记录（2026-08-08 review 迭代）

- **A 拆库粒度**：`FontManager/TextureLibrary` 并入 `libya-ui`，不单拆
  `ya-ui-resource`；`scene-core` 独立保留（Node3D 移出 Node.h 后）。
- **B shared vs static**：对外保持 `ya-engine` shared 聚合导出不变；
  内部模块按 static 库拆分，只用于增量编译与按需链接。Windows DLL 边界
  始终只有 ya-engine 一个。
- **C 目录迁移**：Phase 2 只拆 target，不迁目录、不改 include 路径；
  目录物理收敛（`Render/2D/` 迁出、RDG 迁出）列为可选收尾阶段，单独评估。
- **D Render2D 实例化**：拆库期间**不**把静态入口实例化成 service 对象
  （保持分阶段收敛的静态 facade）；实例化作为独立后续计划，不阻塞拆库。
- **E Physics 解耦方式**：world line 能力留在引擎侧，`PhysicsDebugDraw`
  改为注入 line 收集器（回调/接口），移除对 `Render2D.h` 的 include。
- **F RHI 定位**：RHI 是共享基础设施，不属于 GUI 框架；GUI 框架自有模块
  仅为 `libya-ui` + `libya-ui-scene`。"GUI 闭包"仅指宿主消费时的链接集合
  （core + rhi + rhi-backend + ui + ui-scene），不代表归属。

## 8. 验证基线

- `python3 Script/ya.py build --editor`（每 Phase 后）
- editor 运行验证：3D viewport、2D canvas 模式（world 渲染应归零）、
  PIE 进出、UI 面板显示
- 最小 GUI 宿主示例：窗口 + UI scene + compose 输出，确认链接闭包正确
- GUI 闭包测试 target 全绿（Node2D 布局/渲染/事件测试只链闭包，证明无
  3D/physics/ECS 链接依赖）
- `xmake f -y` 后 `build --editor`：验证文件列表变更后 unity 重新生成

## 10. 执行迭代记录（2026-08-08，reset 后重做）

### 10.1 已落地（3 个 commit）

- `8d90b9c8` 目录按模块物理拆分 + 每模块独立 xmake.lua（`add_files("**.cpp")`
  glob），`ya_module()` helper 单点化导出宏/include 根/unity 分组；
  `ya-engine` 收敛为纯聚合导出。跨层文件归位见 §10.3。
- `9bd32c87` 导出宏单点化：`Core/Api.h` 一张宏表（`YA_CORE_API`…`YA_EDITOR_API`），
  xmake 按 target 注入 `YA_SHARED=1` + `YA_MODULE_BUILD=1`，消费方只拿
  `YA_SHARED=1`（import 侧）；`ENGINE_API` 保留聚合别名。模块零手写导出逻辑。
- `ce1f045c` 模块边界修正：ResourceStateTracker → RHI/Core、
  DeferredDeletionQueue → Core/Common、profiling 运行时状态/查询 → Core。

### 10.2 决策更新（覆盖 §9 旧决策）

- **§9-C 目录迁移**：改为"目录随模块走"——每个模块一个物理目录 + 一个
  `xmake.lua`，禁止跨目录 `add_files("../X/**.cpp")`；include 根统一为
  `Engine/Source`，路径全量重映射。
- **源码收集**：模块一律 glob（`add_files("**.cpp")`），不维护文件清单；
  嵌套模块（RHI/Backend、UI/Scene、Scene/3D）由父 xmake.lua exclude +
  includes 组合。
- **导出宏机制**：`Core/Api.h` 是唯一导出事实源（平台逻辑 + 每模块一行宏表），
  xmake 承担"每个模块不手写宏"的职责——`ya_module()` 统一注入两个 define；
  未来若单模块独立成 DLL，只需把该模块的 `YA_<M>_API` 独立解析。
- **Shader 运行时**（Shader.h/.cpp + Shader/ 编译/反射机制）归 RHI：Vulkan/
  OpenGL pipeline 与 RenderRuntime 共同消费，属渲染抽象层而非 3D 管线。
- **单头第三方实现归位**：VMA/STB → RHI/Backend（unity_ignored），
  TinyGLTF → Resource/Model；`Implementaion/` 目录溶解。
- **§9-E PhysicsDebugDraw**：以"归位"替代注入重构——PhysicsDebugDraw 移入
  Render3D/Debug（render-3d → ui 为允许边），Physics 模块不再触碰 Render2D。
- **§9-B 聚合导出**：维持；`ya-engine` 自身源码仅剩 ImGui demo。
- **聚合导出机制（关键）**：聚合库自身不编译引擎源码时，静态归档按需
  拉取（pull-on-demand）会让 dylib 不导出任何引擎符号，消费方被迫内嵌
  静态副本（VFS/AssetManager 等多份单例，启动即崩）。方案：每模块新增
  `ModuleAnchor.cpp`（extern "C" 锚点），聚合库 `ModuleAnchors.cpp` 引用
  全部锚点；unity 构建下每模块为单对象，一次引用即拉入整个模块。
  已排除：`-force_load`（ld64 对已通过 `-l` 见过的归档忽略该标志）、
  `{links = false}`（模块不再产出归档）。消费方 deps 非 public，只链
  `libya-engine.dylib` 一个共享库。

### 10.3 跨层文件归属（执行迭代）

- 头在下层/实现归上层：`AssetRef.h` 留 Core/Common（轻量句柄），
  `AssetRef.cpp` → Resource；`InputRouter`/`ScriptApi*`/`Profiling.cpp` 实现
  → Host；`ECSRegistry`/`SceneBus` → ECS；`Scene`/`SceneManager`/
  `SceneSerializer`/`PhysicsDebugDraw` → Render3D。
- `RenderViewportOverlayRecorder` 拆分：`recordRender2DComposePass` 进框架
  （UI/Scene/Render2DComposePass），`recordRenderViewportOverlayPass` 留引擎
  （Render3D/Common/RenderOverlay）。
- 死代码删除：全注释 `UIRender.*`、`UIComponentSytstem.*`、空壳
  `Render/RHI/SceneRenderer.h`。

### 10.4 GUI 闭包纯链接验证暴露的跨层耦合（阻塞项，后续迭代）

闭包测试 target（只链 core+rhi+backend+ui+ui-scene+scene-core）无法诚实链接，
级联会拉入 resource/ecs/host/render-3d。逐项清单：

1. **Reflection.h → ECS 注册钩子**：`YA_REFLECT_BEGIN` 宏内嵌
   `ECSRegistry::get().registerComponent<T>()`（代码自带
   `// TODO: should not be in core?`），每个反射 TU 都引用 ECSRegistry。
   解耦方向：钩子从 Core 宏中移出（ECS 组件侧显式 opt-in 注册），或
   Core 提供类型擦除回调并解决静态初始化顺序。
2. **AssetRef 生命周期 → Resource**：`AssetRef.h` 内联代码 +
   `ReflectionSerializer` 引用 `DefaultAssetRefResolver` 与其派生 ref 的
   vtable（实现都在 Resource/AssetRef.cpp）。
3. **UI FontManager → AssetManager**：字体图集注册进资源系统
   （`AssetManager::registerTexture`），闭包需带 Resource。
4. **backend → Host**：`VulkanRender` 取 App 描述/帧号、
   `VulkanPipeline` 取 App 的 ShaderStorage——应改为经 IRender 注入/持有。
5. **Resource/ECS 胖模块**：`AssetManager`/`ResourceResolveSystem` 引用 App；
   拆 gameplay 时一并处理。

解耦完成后再恢复 `ya-gui-closure-test` target（测试代码已起草：
Node 树/绘制序/pickNodeAt 可见性，纯 UI 逻辑）。

### 10.5 二次重构：动态库优先 + 产品线分层（2026-08-08 执行）

**目录**：`Engine/Source` 按产品线物理分层——
`Foundation/`（Core、RHI、RHI/Backend）、`Framework/GUI/Runtime/`
（Draw2D、Resource、Scene、Compose；`Node` 场景树基类随 GUI）、
`Framework/Game/`（Scene/Scene3D、Resource、Render/Graph、Render/Render3D、
Gameplay/ECS、Physics）、`Product/`（Host、Editor）。每个模块独立
`xmake.lua`（glob 源码 + 显式 deps/packages/include），`Source/xmake.lua`
只保留薄封装 `ya_std_module()`（导出宏注入 + unity）与 `ya_engine_defines()`。

**构建形态**：模块全部 `set_kind("shared")`；`ya-engine` 降级为 public-deps
聚合兼容层；`Api.h` 只保留平台判定 `YA_API_EXPORT`，模块宏由 xmake 按模块
注入（`YA_CORE_API=YA_API_EXPORT`）；`ModuleAnchor` 机制整体删除。
macOS 多 dylib 基建：flat namespace（weak vtable/typeinfo 跨镜像合并）、
imgui-local/imguizmo-local 改单一 shared（ImGui 全局状态唯一）。

**GUI 闭包达成**：`ya-gui-runtime` 只依赖 core+rhi+rhi-backend；
`ya-gui-closure-test` target 只链闭包运行 27 个 Node2D 测试。切断动作：
Reflection 反射钩子移入 ECS 侧 opt-in；AssetRef 具体类型与 vtable 归 Core +
`IAssetRefResolver` 注入（Resource 静态注册）；`ResourceRegistry` 下沉 Core
（cpp 强符号）；Node 基类 ECS-free（`onNameChanged` 钩子）；FontManager 图集
注册改 sink 注入；backend 经 `IRender`/`RenderCreateInfo` 拿 frame index、
设备选择配置与 ShaderStorage；默认纹理回退改 `IBuiltinTextureSource`；
相机控制器移入 Gameplay/ECS/System。

**验证**：350 全量测试 + 27 闭包测试全绿；editor 与游戏路径 8 帧干净退出
（exit=0，无 Vulkan validation 泄漏）。运行需
`DYLD_LIBRARY_PATH=build/macosx/arm64/debug:Engine/ThirdParty/VulkanSDK/.../lib`
（既有行为）。

### 10.6 剩余解耦（下一轮）

- **Game 层 → Host 符号延迟解析**：ECS/Resource/Render3D/Physics 仍直接调用
  `App` 服务（AppLifecycle 初始化链），macOS 上以
  `-undefined dynamic_lookup` + flat namespace 过渡；计划以 app-services
  接口注入替换（App 服务的 `registerFrameTask`/`getRenderServices` 等下沉
  接口），完成后移除该过渡标志并解锁 Windows 构建。
- **ya-gameplay 拆分**：ECS 系统与 render-3d/host 深度耦合，暂为胖模块
  （注入全部引擎导出宏编译）；拆分时随文件迁出。
- **RenderRuntime.h 对 DeferredPipelineDebugViews 的 include 解耦**（同模块）。
- **最小 GUI 宿主示例**：只链 `ya-gui-framework` 聚合（core+rhi+backend+
  gui-runtime）的可运行窗口示例。

### 后续闭环（2026-08-11，guiapp 最小宿主完整循环）

目标：把 `ya-gui-minimal-host` 从"静态 sprite+文字演示"升级为完整交互宿主，
证明 GUI 闭包（core+rhi+backend+gui-runtime）可独立承载一个可交互应用——
这是"gui 框架自举 editor"路线（per-window WidgetTree + 窗口合成器）的地基。

- **帧循环**：SDL 事件（motion/down/up/wheel/key）→ `WidgetTree::dispatchEvent`
  （logical 坐标换算）→ `layout()` + `buildSnapshot` → `recordRender2DComposePass`
  到 swapchain presentation target；QUIT/Esc 退出。
- **交互 demo**：Panel + UIText×2 + UIButton 点击计数（按钮标签为 Pass 过滤的
  子文本，hover/press 仍达按钮）。
- **resize**：帧首比对 swapchain image count/extent，变化则 waitIdle 后重建
  presentation targets + command buffers + `setLogicalExtent`（swapchain 重建由
  RHI 在 begin 时自动处理）。
- **框架改动（唯一一处）**：`FRender2DComposePassDesc` 新增 `finalLayout`
  （默认 `ShaderReadOnlyOptimal` 不变，所有既有调用零改动）；直接上屏传
  `PresentSrcKHR`——swapchain 图像无 SAMPLED usage，原固定过渡触发
  VUID-VkImageMemoryBarrier-oldLayout-01211。该字段即未来窗口合成器的接口。
- **参数解析**：支持 `--exit-after-frame=N` 与 `--exit-after-frame N` 两种形式。
- **验证**：30/120 帧冒烟干净（首帧快照 5 draw items / 1024x768 logical，
  Vulkan validation 零报错）；widgets 36 / closure 42 / ya-testing 404 / lint ok /
  editor 2D 冒烟干净。
- 提交：`f8053391 [gui/compose]`（finalLayout）、`6db533ad [gui/host]`（full loop）。
