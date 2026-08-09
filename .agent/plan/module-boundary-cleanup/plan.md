# YA Engine 模块边界与 XMake 组织重构迭代计划

> 建立日期：2026-08-08  
> 来源：最近一轮模块拆分、目录分层、导出宏和 include 重构 review。  
> 状态：执行中；关键架构决策已由用户确认。  
> 进度：Phase 0-8 全部完成（2026-08-09）。Phase 6：tier include 删除、
> 全模块转发头体系、public deps 按头使用收敛、fat ya-gameplay-ecs 溶解
> （render 组件入 Render3D、RayCast 入 adapters）、forbidden-include lint
> （Script/ya_module_lint.py）。Phase 7：ya-app-services 窄接口
> （frame/window/shadow/offscreen/config 读取下沉 Core 的 ConfigManager）、
> Render3D 13 个 Host 依赖文件接口化、dynamic_lookup 与 flat_namespace
> 移除、monolith 完整（Runtime/Editor/Example 插件经 -Wl,-export_dynamic
> 单实例解析）。Phase 8：最小目标集（ecs-core-test/resource-core-test/
> render-3d-test）+ gui-closure-test、CI matrix 脚本（Script/ci.sh）。
> 基线：debug 全量 350 测试、运行时/编辑器冒烟、四种 profile×linkage
> 组合均通过（release 模式运行时崩溃为既有问题，shared/monolith 同现，
> 非本轮引入）。
> 进度（原记录）：Phase 0/1 完成（2026-08-08）；Phase 2 完成（2026-08-09）；
> Phase 3/4 完成（2026-08-09，含 Resource 三层拆分、
> EnvironmentLighting/Terrain 拆分、GameplayResourceBinding 重命名、
> PhysicsDebugLineCollector 注入）；Phase 5 完成（2026-08-09）：
> RHI 拆为 ya-rhi（接口+平台无关）/ ya-rhi-backend-common（纹理 glue+STB）/
> ya-rhi-vulkan（Vulkan 实现+Vulkan 分派工厂+VMA），OpenGL 彻底移出构建图，
> 消费者显式依赖 backend（GUI/Resource 声明 rhi+backend-common+vulkan），
> 依赖方向单向：rhi ← backend-common ← vulkan。
> ecs-core / gameplay-systems（含 Animation 与纯 gameplay 组件）/
> component-linkage / render-ecs-adapters（rules + resolve 系统）四 target
> 建立，ECS↔Scene 环在 xmake 层完全切断，组件头去 Render3D 化，
> ya_engine_defines 从 ECS 移除。fat `ya-gameplay-ecs` 仅剩 render 组件
> 过渡容器，其溶解依赖 Phase 3 的 Render3D 消费方式重构。Phase 3 已推进：
> Resource 拆 core/loader/runtime 三层（无 Host/ECS/Render3D 依赖）、
> OffscreenJobQueueService contract 下沉 RHI、EnvironmentLightingProcessor 与
> TerrainProcessor 拆分（均无 Host/App）；剩余 Render3D 只读消费与
> feature target/profile 化（Phase 5.5）。  
> 关系：本计划补充并修正 `gui-framework-module-split`；涉及 ECS、Scene、
> Resource、Render3D、Host 和 XMake 边界的内容，以本计划为后续执行顺序。

## 0. 已确认的架构决策

以下决策是本重构线的约束，后续迭代不再反复讨论：

| 编号 | 已确认决策 | 执行含义 |
|---|---|---|
| 1-A（修订） | 保留各模块 DLL，同时提供单体 exe 构建口子 | 同一套 target/source/dependency 描述支持 `shared` 模式和 `static` 单体模式；不能维护两套源码清单 |
| 2-A | Scene 独立为 Scene 模块链 | `Scene`、`SceneManager`、`SceneSerializer` 不归 Render3D |
| 3-A | ECS component renderer-independent | 公共 component 使用 handle/descriptor，不持有具体 Render3D/Resource runtime 类型 |
| 4-A | App services 拆成多个小接口 | 使用 `ISceneServices`、`IRenderServices`、`IResourceServices`、`IOffscreenJobRunner` 等按职责注入，不建立巨型 `IApplication` |
| 5-B | OpenGL 暂时移出主构建 | 只保留历史源码，不进入默认 target，不与 Vulkan target 混编 |
| 6-A | 保留 `ya-engine` 兼容 facade | 新代码禁止依赖全量聚合入口；现有消费者分阶段迁移到显式模块依赖 |
| 7 | 源码与原始头文件继续放在一起 | 不引入 `Public/Private/` 或 `src/include` 双目录；公共 API 通过 `include/{模块名}/` 转发头暴露，并由 XMake public include dir 传播 |
| 8 | shared 模式保留实际 `ya-engine` 聚合 shared library | 兼容既有消费者；模块 DLL 仍保持独立 |
| 9 | 单体应用分别生成 | Runtime、Editor、各 Example 分别生成自己的 monolith exe，不合并产品入口 |
| 10 | monolith 内部模块统一使用 static | 不以 object library 作为默认实现，降低双模式构建复杂度 |
| 11 | 产品能力与链接形态正交配置 | `ya_profile=engine|gui` 决定进入构建图的模块、package、shader 和产品入口；`ya_linkage=shared|monolith` 只决定模块为 DLL/shared 还是 static，不用链接形态承担功能裁剪 |
| 12 | GUI-only 不依赖完整 Game Resource/Material 链 | GUI 保留自己的 font/texture/brush/sprite binding 等轻量资源服务，但不依赖 3D Material、model importer、ECS、Scene3D、RenderGraph、Render3D、environment lighting 和 Physics |
| 13 | Resource resolve 按阶段拆分，不整体下沉到应用层 | Resource Core/Loader/Runtime 保持可复用底层模块；ECS/component binding 属于 Gameplay runtime；cubemap、irradiance、prefilter、terrain mesh 等派生处理属于可选 Render3D feature；Host 只负责组装和调度服务实现 |
| 14-A（修订） | target 拆分标准：先目录、后 target | 先保证职责清晰和高内聚；只有存在独立复用、独立裁剪、独立生命周期或明确依赖隔离价值时，才拆成模块 target。模块内先通过子目录 + 接口 + private include 保持边界（`ya-render-3d` 内部保持 `Core/ Materials/ EnvironmentLighting/ Terrain/ Forward/ Deferred/ Services/` 子目录组织），不预先为每个 feature 建 target |
| 14-B | 撤回"立即建立四个 Render3D feature target" | EnvironmentLighting / Terrain 保持为 `ya-render-3d` 内部高内聚子目录；只有某 feature 同时满足下列条件中**至少两个**，才升级为独立 target：① 有真实独立裁剪需求；② 能形成闭合依赖、不反向引用主模块；③ 有独立初始化/关闭/资源生命周期；④ 有独立测试目标；⑤ 拆分能显著减少 package/shader/编译闭包；⑥ 未来可能被替换或独立复用 |
| 14-C | Scene 不继续碎拆 | Scene 同时组织 ECS、Node2D、Node3D，是"完整游戏场景"的高内聚组合；target/命名调整为 `ya-game-scene` 即可，不制造 WorldCore + SceneExtensions + NodeAdapters 碎片 |
| 14-D | render adapters 是少数跨层桥接模块 | ECS Core / Gameplay Systems / Render Adapters 方向正确；但 adapter 只给真正跨层的桥接建 target（如 linkage rules、model instantiation），不为每个 component 创建 adapter target |
| 14-E | 模块拆分存续判断 | 已有拆分按同标准复核：Resource Core/Loader/Runtime（数据契约/CPU 导入/GPU runtime 生命周期不同）与 RHI / backend-common / Vulkan（接口/平台无关实现/平台实现有真实替换边界）保留；LinkageFramework 与 render rules（稳定机制 vs 业务策略）保留，但规则内部不再拆更小的 target |

公共头文件门面采用 plugin 的既有模式：

```text
Module/
  Foo.h
  Foo.cpp
  Feature/
    Bar.h
    Bar.cpp
  include/Module/
    Foo.h          # 只 include "../../Foo.h"
    Feature.h      # 分类聚合转发头
    Lib.h          # 模块总聚合头，按需提供
```

`include/{模块名}/` 不复制声明、不增加实现逻辑，也不形成第二套 API。模块 A 在
XMake 中执行：

```lua
add_includedirs("./include", { public = true })
```

模块 B 通过：

```lua
add_deps("A")
```

即可稳定使用：

```cpp
#include "A/xxxx.h"
```

`include/{模块名}/` 下的每个头文件都只能是原始头文件的转发文件，例如：

```cpp
#pragma once
#include "../../原始目录/xxxx.h"
```

转发头不得复制声明、增加逻辑、重新组织声明，也不得形成第二套 API。这样源码在
模块目录内移动时，只需要更新转发头，不要求所有消费者同步修改物理路径。文件较少
时提供逐文件转发头；文件较多时可以提供分类聚合转发头，但聚合头本身仍然只能
连续 include 原始头文件，不能把声明复制进去。只有确实适合完整引入的小模块才提供
全量 `Lib.h`，避免一个聚合头无差别引入过多符号和第三方依赖。

## 1. 总体目标

把当前“源码物理搬家 + 每目录一个 shared target”的过渡形态，收敛为：

1. 模块依赖图单向、可审计，不能依赖 `dynamic_lookup` 或全局链接器行为掩盖环依赖。
2. 每个 target 只暴露自己的 forwarding include 根；缺少 `add_deps()` 时应在编译期失败。
3. ECS、Scene、Resource、Gameplay、Render3D、Host 各自拥有稳定职责。
4. GUI 闭包不触达 ECS、Physics、完整 Game Resource、RenderGraph、Render3D、
   Host 和 Editor；GUI 自身的字体、纹理、brush/sprite binding 由轻量
   `ya-gui-resources` 提供。
5. RHI 是共享基础设施，不归 GUI 或 Render3D 所有。
6. XMake 文件表达真实依赖和源码归属，而不是通过 tier-wide include、全量 public
   deps 和多个模块 shared library 拼接出“可链接”结果；shared/monolith 只切换
   target kind，不复制 source list。
7. 每个阶段都能独立构建、测试和回滚，避免一次性大迁移。

目标依赖方向：

```text
Core
  ↓
RHI
  ↓
RHI backend（主线 Vulkan；OpenGL 仅保留历史源码）

ResourceCore → ResourceLoader → ResourceRuntime
                                  ↓
SceneCore ───────┐          Gameplay binding
ECS Core ────────┼───────────────┘
                 ↓
          Render ECS adapters
                 ↓
            RenderGraph
                 ↓
          Render3D Core/Features
                 ↓
       Runtime composition (Host)
                 ↓
               Editor
```

GUI 是与 Gameplay/Render3D 平行的产品线；GUI profile 的最小闭包为：

```text
Core
  ↓
RHI → Vulkan backend
  ↓
GUI Resources → GUI Draw2D → GUI Scene/Widgets → GUI Compose
                                      ↓
                              Minimal GUI Host（可选）

Core → Resource / Gameplay / RenderGraph / Render3D   # 仅 engine profile
```

任何 `ECS → Host`、`Gameplay → Render3D implementation`、`Scene3D → ECS`
与 `Render3D → Scene lifecycle` 的反向边都视为违规，除非通过明确的 adapter/interface
target 表达。

这里的“GUI 不依赖 Material”特指不依赖 Game/Render3D 的 Material hierarchy、
PBR/Phong、model importer 和 3D shader。GUI 内部仍然需要描述颜色、纹理、采样器、
字体和混合状态，但公共概念使用 `Brush`、`Paint`、`SpriteBinding` 或内部 2D
pipeline state，避免为了复用名字把完整 3D Material 模块拉入 GUI 闭包。

## 2. 当前基线与不可破坏项

### 2.1 保留的工程约束

- 只使用 XMake，不引入 CMake。
- `Generated/*` 只读，必须修生成链。
- Shader-facing C++ 类型继续以 Slang/GLSL 生成头为单一事实源。
- 不在帧录制中途重建 GPU 资源。
- 日志继续使用 YA 日志宏。
- 保留现有 `ya-engine` 对 Editor、Example、Test 的兼容入口，直到聚合 target
  有明确替代方案。
- 模块化模式下各模块继续使用 shared/DLL，保留单模块独立链接和调试能力。
- 单体模式下同一批模块 target 切换为 static，最终由 Runtime、Editor 或对应
  Example 入口分别链接为各自的单体 exe；不得维护第二套源码清单。
- shared 模式保留实际的 `ya-engine` 聚合 shared library，以兼容既有消费者；
  它不取代各模块 DLL。
- 原始头文件继续和源码放在同一模块目录；跨模块消费者只通过模块 forwarding
  include root 访问。
- 保留工作区中用户已有的 `HelloMaterial.scene.json` 修改，不在本重构中处理。

### 2.2 当前已知结构债务

- `ya_tier_include()` 暴露整个 Foundation/Framework/Game/Product 根目录。
- 多个模块使用 `add_files("**.cpp")` 和 `add_headerfiles("**.h")`，职责混杂的目录
  被整体收进一个 target。
- `ya-gameplay-ecs` 是 fat module，公开包含 Host、Render3D、Resource 类型。
- `Scene`、`SceneManager`、`SceneSerializer` 放在 Render3D。
- AnimationSystem 放在 Render3D，但承担 Gameplay 动画更新。
- ComponentLinkageSystem 的当前实现把通用 linkage framework 与
  material/light billboard 业务规则揉在一起，且仍直接触达 Host；问题是边界混合，
  不是它“属于 billboard”。
- ResourceResolveSystem 混入环境光照、cubemap、offscreen GPU 处理。
- 当前 `EnvironmentLightingProcessor` 虽已移出 `ResourceResolveSystem`，仍直接
  include Host 的 `OffscreenJobRunner.h`，并同时拥有 skybox、environment lighting
  和 terrain 派生状态；职责和服务依赖尚未最终闭合。
- ECS、Physics、Resource、Render3D 使用 `-undefined dynamic_lookup`。
- macOS 全局使用 `-flat_namespace`。
- `ya-engine` 与 `ya-gui-framework` 是没有真实业务源码的 shared 聚合 target；
  其中 `ya-engine` 继续作为兼容聚合 facade，`ya-gui-framework` 不再伪装成
  独立 shared library。
- package 和 module 依赖仍有大量 `public = true`。
- RHI interface、通用 backend、Vulkan、OpenGL 仍然混在两个 target 中。

## 3. 执行原则

1. 先建立依赖约束，再搬文件；不要先大规模改目录。
2. 先移动“职责明显错误”的文件，再拆 fat module。
3. 每个阶段只改变一个主要维度：归属、头文件门面、依赖、链接器策略依次推进。
4. 迁移过程中允许兼容 wrapper，但 wrapper 必须有删除里程碑和期限。
5. 不用增加 source/tier public include root 来解决编译错误；编译错误应反向推动
   forwarding header 和 target 依赖修正。
6. 每个阶段结束都必须有：
   - XMake 配置成功；
   - 目标级构建成功；
   - 相关测试或最小运行目标通过；
   - `xmake show -t <target>` 复核 include/deps/package；
   - 更新本文件进度。

## 4. 阶段计划

### Phase 0：建立依赖与归属基线

目标：不改变运行时行为，只把当前真实结构记录下来。

任务：

- [x] 列出每个 target 的源文件、public header、private header、`add_deps`、
      include dirs、packages、平台 linker flags。
      → `dependency-baseline.md` §1/§2 + `baseline/show-*.txt`（14 个 target）。
- [x] 生成 include 边清单，至少覆盖：
      `ECS → Host/Render3D/Resource`、`Scene → ECS`、`Resource → Host`、
      `Render3D → Host/GUI`、`Physics → Render2D`。
      → `dependency-baseline.md` §3/§4（全量边 + 关键边文件级清单）。
- [x] 标记每个文件的“主职责”和“允许依赖层”。
      → `file-ownership.md`（12 模块 × 目录分组，逐文件越界标注）。
- [x] 建立 forbidden include 初版规则。
      → `dependency-baseline.md` §6（含当前违反清单）。
- [x] 保存当前配置和目标展开结果作为 baseline。
      → `baseline/show-*.txt`；`ya.py cfg` 通过；目标级 show 复核完成。

验收：

```bash
python3 Script/ya.py cfg
xmake show -l targets
xmake show -t ya-gameplay-ecs
xmake show -t ya-render-3d
xmake show -t ya-editor
```

产物：

- `.agent/plan/module-boundary-cleanup/dependency-baseline.md`
- `.agent/plan/module-boundary-cleanup/file-ownership.md`

### Phase 1：修正最明显的文件归属

目标：先消除 Render3D 和 ECS 中最明显的职责污染。

#### 1.1 Scene/World

- [x] 将 `Render3D/Scene.h/.cpp` 移至 `Framework/Game/Scene/Core/`。
- [x] 将 `SceneManager`、`SceneSerializer` 一并移出 Render3D
      （`Scene/Runtime/`、`Scene/Serialization/`）。
- [x] 建立已确认的 Scene 模块链：
      `ya-scene-core`、`ya-scene-runtime`、`ya-scene-serialization`。
- [x] `ya-scene-runtime` 拥有 scene lifecycle（SceneManager）；Scene 数据
      （ECS registry + Node tree 组合）放在 `ya-scene-core`，避免
      runtime↔serialization 环依赖（相对原计划的放置调整，见下注）。
- [x] `ya-scene-serialization` 独立拥有 JSON/反射/资源引用序列化。
- [x] `SceneManager` 的 Host 注册改为注入接口（`ISceneLifecycleHost` +
      `Scene::setLifecycleHost`），不再在 Scene 构造/析构中调用 `App::get()`。
- [x] Render3D 只接收 active scene/world 的只读渲染输入（Render3D 对 scene
      的引用仅为私有 dep，public 头不再暴露 Scene 类型）。

> 注：Scene 数据结构放 `ya-scene-core` 而不是 `ya-scene-runtime`——因为
> SceneSerializer（serialization）与 SceneManager（runtime）都依赖 Scene 数据，
> 若 Scene 归 runtime 则 runtime↔serialization 成环。依赖方向：
> `core ← runtime → serialization → core`。ECS↔Scene 的环由 Phase 2
> （`ya-ecs-core` 拆分）最终消除；当前 ECS 保持 fat 过渡态（tier include，
> 无 scene dep 边）。

#### 1.2 Animation

- [x] 将 `Render3D/Systems/Animation/AnimationSystem.*` 移至
      `Gameplay/Animation/`（新 target `ya-gameplay-animation`，
      Phase 2 并入 `ya-gameplay-systems`）。
- [x] 把“世界渲染关闭时暂停动画”的策略改成显式 tick policy（`setTickPolicy`，
      Host 绑定 `isWorldSceneRenderEnabled` 判断；沿用 ResourceResolveSystem 的
      lambda 注入模式）。
- [x] Gameplay 不得包含 `RenderRuntime.h` 或 `Host/App.h`（scene 经
      `setSceneProvider` 注入）。

#### 1.3 Component linkage

- [x] 将 `ComponentLinkageSystem` 定位为顶层 linkage framework/facade
      （`LinkageFramework`），不是 `LightBillboard` 的专属系统。
- [x] 通用部分保留在独立 `ya-component-linkage` target：
  - 监听 Scene/ECS component construct/update/destroy 和 scene init；
  - 提供 linkage rule 注册、匹配、延迟调度、去重和生命周期解绑
    （onSceneUnload 断开 + deferred 取消，2026-08-09 完成）；
  - 只依赖 ECS/Scene contract 与注入的 frame-task/scene provider；
  - 不包含 PointLight、DirectionalLight、Billboard、Material、RenderComponent
    等具体业务类型；
  - 不直接 include `Host/App.h`，Host 通过窄 service contract 组装它。
- [x] 业务逻辑拆成可注入 rule/adapter：
  - `LightBillboardLinkageRule`：注入 `LightBillboardPolicy`（实例成员，
    2026-08-09），负责 light ↔ billboard；
  - `MaterialRenderLinkageRule`：负责 material component ↔ RenderComponent；
  - 后续可增加 gameplay tag、physics proxy、editor metadata 等 rule，但不修改
    framework 核心（扩展点已就绪，按需增加）。
- [x] `LinkageFramework` 为 framework 根入口/facade；规则实现放在
      `Render/Adapters/LightBillboard`、`Render/Adapters/Material` 业务目录。
- [x] Host 启动时创建 framework，分别注册所需 rules；GUI-only profile 不含
      Host/adapters，不触达 Billboard/Material 3D 类型。
- [x] `LightBillboardPolicy` 由 Host 从 ConfigManager 读取后注入 rule 实例
      （2026-08-09）：policy 改为 `LightBillboardLinkageRule` 实例成员，
      移除匿名命名空间全局静态 `g_lightBillboardPolicy`；静态
      `applyLinkage` 改为显式传 policy（自动化 RPC 无 rule 实例，用默认
      policy），deferred 任务按值捕获 policy（任务可能比 rule 活得更久）。
      验证：354 测试 + 运行时冒烟（灯光 billboard 路径）通过。

当前物理路径 `Render3D/Adapters/LightBillboard/ComponentLinkageSystem.*` 视为过渡
位置；后续应把 framework 根入口移回 linkage 模块，把 LightBillboard 代码留在业务
adapter 目录。这里不要求立刻重写状态流，先保持回调顺序和延迟调度语义，再分提交
完成物理归属和依赖收敛。

#### 1.4 Environment/resource resolve

- [x] 保留普通 AssetRef/mesh/material/UI/billboard resolve 在
      Gameplay（ResourceResolveSystem 收缩为纯资源 resolve）。
- [x] 将 cubemap、irradiance、prefilter、terrain derived GPU 处理移至
      `Render3D/EnvironmentLighting`（新 `EnvironmentLightingProcessor`，
      ISystem 由 Host 驱动，注入 render/offscreen queue/scene provider）。
- [x] `ResourceResolveSystem.h` 不再包含 Render3D pipeline 和
      Host offscreen runner（仅 Core/System + entt + std）。
      Render3D 消费方经 `IRenderRuntimeServices::getEnvironmentLightingProcessor`
      或 `App::getEnvironmentLightingProcessor()` 取状态。

本阶段只完成了第一步职责迁移，不把当前 `EnvironmentLightingProcessor` 视为最终
边界。Phase 3 必须继续处理：

- `OffscreenJobQueueService` / queue contract 移出 Host 头文件，由低层服务接口
  target 定义，Host 只提供实现和 composition；
- environment lighting 与 terrain 派生处理拆开，禁止因二者都“会生成 GPU/mesh
  资源”而长期放进同一个 processor；
- Render3D 通过窄的只读 provider/handle 消费派生结果，不从 Host/App 查询系统；
- source asset resolve 与 derived resource build 分开记录状态和版本，避免一个
  `resolveState` 同时表达 IO、CPU decode、GPU job 和 scene binding。

验收：

- [x] `rg` 检查上述文件已不在旧目录（Scene*/Animation/ComponentLinkage/
      Environment*/Skybox*/Terrain 处理均移出 Render3D/ECS 原目录）；
- [x] Scene/Gameplay target 不再包含 Render3D implementation header
      （ResourceResolveSystem.h 仅 Core/System + entt + std）；
- [x] 行为测试覆盖 scene load/unload、动画播放、环境光照 resolve
      （350 测试全通过；运行时 300 帧 + 编辑器 120 帧冒烟通过）。

Phase 1 完成（2026-08-08），提交：
`[scene] move Scene lifecycle out of Render3D`、
`[gameplay] move animation system out of Render3D`、
`[ecs] move component linkage to Render3D light-billboard adapter`、
`[render] split environment lighting resolve out of ECS`。

### Phase 2：拆 ECS public API 与运行时适配

目标：让 ECS 成为轻量、可独立编译的基础模块。

- [x] 新建 `ya-ecs-core`：EnTT registry、Entity、Component 基础设施、
      ComponentMutation、SceneBus、ISystem（ISystem 本就在
      `Foundation/Core/System/System.h`，无需迁移）。
  - 物理位置：`Framework/Game/Gameplay/ECS/Core/`；公共访问经
    `include/ECS/` 转发头（`ECS/Core/include/ECS/*.h`，只转发不复制声明）。
  - 依赖：`Core + reflects-core + entt/nlohmann_json`；无 tier include（仅经
    core dep 传递获得 Foundation root），无 `ya_engine_defines()`，无
    `dynamic_lookup`；`xmake show -t ya-ecs-core` 复核通过。
  - `Entity` / `ECSRegistry` 的 Scene 依赖处理（重要决策）：
    - `ECSRegistry` 的 8 个 Scene 重载删除，调用点（SceneSerializer、
      ScriptApiCore、DetailsView.Reflection、AppAutomationControlService）
      改为 registry 重载，语义不变。
    - `Entity` 保留前置声明的 `Scene*` descriptor；`setName` / `operator bool`
      的 scene 侧语义经 `EntitySceneContract`（函数指针桥）注入：
      ecs-core 提供 no-op 默认实现，`ya-scene-core` 在库加载时注册真实实现
      （`Scene/Core/EntitySceneBridge.cpp`）。GUI-only 等无 scene 模块的构建
      自动退化为 registry-only 语义；Windows 下无跨 DLL 成员定义问题。
  - `ya-scene-core` 依赖从 `ya-gameplay-ecs` 改为 `ya-ecs-core`（public）；
    TransformComponent / ManagedChildComponent 已随 scene-3d 归位，私有
    fat-ecs 过渡边已删除（2026-08-09）。
  - `ya-gameplay-ecs`（fat）改为依赖 `ya-ecs-core`（public），并排除
    `Core/**` 源与头文件 glob。
  - 验证：`ya.py cfg` 通过；全目标构建通过；350 测试全通过；运行时 120 帧 +
    编辑器 60 帧冒烟通过。
- [x] 新建 `ya-gameplay-systems`：TransformSystem / ScriptingSystem /
      JSScriptingSystem 已迁入（`Framework/Game/Gameplay/Systems/`，依赖
      ecs-core + scene 线，无 Host/Render3D）。Animation / ModelInstantiation /
      ResourceResolve / LuaScripting 待 fat 模块溶解时并入（依赖 fat 组件或
      Host 服务注入）。
  - TransformSystem 增加 `setSceneProvider` 注入缝（Host 在 AppLifecycle
    绑定），移除 `Host/App.h`。
  - JS 脚本 API 目录注册（core/asset）从 JSScriptingSystem::init 移到 Host
    组合层；测试组合层同样自注册。
  - 消费者 include 从 `ECS/System/*` 改为 `Gameplay/Systems/*`。
- [x] 新建 `ya-component-linkage`（`Framework/Game/Gameplay/Linkage/`）：
      `LinkageFramework` = scene lifecycle hook（SceneManager::onSceneInit）+
      SceneBus 组件移除分发 + rule registry + 延迟 frame-task 调度（带 scene
      有效性守卫）。依赖 ecs-core + scene 线；Host 注入 SceneManager 与
      frame-task sink；不持有业务状态。
- [x] 新建 `ya-render-ecs-adapters`：Material/mesh/model 到 render runtime 的
      桥接（独立 target 已建，rules + ModelInstantiationSystem 归位；render
      组件物理迁入 Render3D/Component/ 并保留 ECS/ 前缀转发头，fat ECS
      溶解完成，2026-08-09）。
- [x] `ComponentLinkageSystem` 拆分为两个 render adapter 规则：
  - `LightBillboardLinkageRule`（`Render3D/Adapters/LightBillboard/`）：
    light ↔ billboard + `LightBillboardPolicy`（setter 归属规则，不再挂在
    framework 全局状态；policy 当前仍为规则静态存储，待 automation RPC 拿到
    实例后转实例成员）；
  - `MaterialRenderLinkageRule`（`Render3D/Adapters/Material/`）：
    material 组件 ↔ RenderComponent 拓扑同步；
  - 两者经 framework 延迟调度，不再 include `Host/App.h`；
    AppLifecycle 组合 framework + 规则并注入服务。
- [x] ECS 原始 component 头文件继续放在源码目录（fat 模块内），公共访问经
      `include/ECS/` 转发头（组件转发头随 fat 溶解补齐）。
- [x] 将具体 `Render3D::Material`、`Resource::Mesh/Model` 等类型从 ECS
      component public header 移除（2026-08-09）：
  - 组件头不再 include Render3D/Resource/GUI/RHI 头；运行时指针改为前置声明的
    不透明句柄（沿用 Core AssetRef 模式）。
  - `MaterialComponent` 模板改为继承非模板 `MaterialComponentBase`：运行时
    material 存储/析构经 fat 模块 out-of-line 实现（MaterialFactory），
    scene/serialization/ecs-core TU 构造/销毁材质组件不再触达 Render3D。
  - material 组件公共 API 改用组件本地槽位枚举（EPBRMaterialTextureSlot /
    EPhongMaterialTextureSlot / EUnlitMaterialTextureSlot，与 Render3D
    EResource 顺序 1:1，cpp 内映射）。
  - `TextureSlot`/`SamplerConfig`/采样器枚举下沉 Core
    （`Core/Common/TextureSlot.h` / `SamplerEnums.h`）；GPU 侧转换移到 GUI
    `TextureSlotBinding.h`（白纹理/棋盘格/默认采样器回退）。
  - 遗留：Skybox/EnvironmentLighting（RHI 纹理描述符，Phase 3）、
    SkeletonAnimator（SkeletonPose 值成员，Phase 3 资源分层）。
- [x] 需要实现类型的地方使用前置声明、内部原始头文件或 adapter cpp。
- [x] 取消模块中的 `ya_engine_defines()` 全量注入（ecs-core / gameplay-systems
      已无；physics / render-3d 于 2026-08-09 移除；仅 `ya-engine` 聚合
      target 保留，用于其 PCH 解析全部引擎头）。
- [x] ECS target 不得声明 Host、Render3D、GUI 的 public deps
      （ya-ecs-core deps 仅 foundation-core + reflects-core，2026-08-09 复核）。

> Phase 2 提前落地的 Phase 4 归属调整（2026-08-09）：
> `TransformComponent` / `ManagedChildComponent` 移入 `ya-scene-3d`（与 Node3D
> 同属 scene 线）；`ya-scene-3d` 依赖收敛为 gui + ecs-core + core；
> `ya-scene-core` 移除最后一条 fat-ecs 依赖边——ECS↔Scene 环在 xmake 层完全切断。

验收：

```bash
python3 Script/ya.py cfg
xmake show -t ya-ecs-core
xmake show -t ya-gameplay-systems
xmake show -t ya-render-ecs-adapters
python3 Script/ya.py test --target ya --filter ECS
```

必须满足：

```text
ya-ecs-core → Core + EnTT
ya-gameplay-systems → ecs-core + Resource + Scene
ya-component-linkage → ecs-core + Scene + injected app-services contract
ya-render-ecs-adapters → ecs-core + Resource + Render3D
```

### Phase 3：收敛 Resource 模块

目标：把资源描述、加载缓存、component binding 和渲染派生资源分层。不是把整个
Resource 模块下沉到应用层，而是只把“何时为哪个 scene/component 执行解析”的
编排留在 Gameplay/Runtime composition。

- [x] `ya-resource-core`（`Resource/Core/`）：handles、metadata、
      imported-data 契约、Skeleton、primitive geometry 工厂；纯 Core 依赖
      （ModelImporterCommon 改用 Core canonicalizeAssetPath）。
- [x] `ya-resource-loader`（`Resource/Loader/`）：Assimp/TinyGLTF import、
      stb 解码头；无 RHI。
- [x] `ya-resource-runtime`：AssetManager、GPU mesh/model、缓存、
      texture import/helpers、engine AssetRef resolver；依赖
      core+loader+RHI+GUI，不出现 Scene/ECS/Host/Render3D；tier include 仅
      Game；`ya_engine_defines()` 移除；`dynamic_lookup` 仍在（Phase 7）。
- [x] `GameplayResourceBinding`（原 `ResourceResolveSystem`，cac6100b 更名）：
      扫描既有 component，按资源类型拆 handler 把 AssetRef 绑定为 runtime
      CPU/resource handle（resolvePendingMeshes/Materials/UI/Billboards）；
      不创建 scene topology，不执行 Render3D pipeline。归属决策（2026-08-09）：
      **保持为 `ya-render-3d` 内部 Services/**（物理位置、编译归属与
      `YA_RENDER_3D_API` 一致，由 RenderRuntime 持有并 tick）。不再计划
      移入 `ya-render-ecs-adapters`——那会造成 render-3d ↔ adapters 依赖环
      （RenderRuntime 持有它 + adapters 依赖 render-3d）；除非先按 Phase 3
      第 3 项解除 RenderRuntime 对它的持有并切断 adapters→render-3d 反向边。
- [x] 建立低层渲染任务 contract（`OffscreenJobQueueService`，2026-08-09）：
  - contract 移入 `RHI/Core/OffscreenJob.h`（只依赖 RHI command buffer/job
    state）；`queueOffscreenJob(queueService,...)`/`cancelOffscreenJob`
    实现下沉 RHI（output 创建、record keep-alive、延迟删除）；
  - Host 只保留 App 绑定 enqueue 的重载；
  - EnvironmentLightingProcessor 公共头/cpp 不再 include
    `Host/Utility/OffscreenJobRunner.h`。
- [x] environment lighting 派生状态独立（不再共享 processor；2026-08-09）：
  - source texture handle/版本来自 ResourceRuntime；
  - `EnvironmentLightingProcessor` 独立拥有 cylindrical→cubemap、irradiance、
    prefilter 和 BRDF 相关派生状态；
  - 对 Render3D 暴露只读 lighting result/provider，不暴露 Host 或 mutable scene；
  - ~~独立 `ya-render-environment-lighting` target~~（已撤回，见决策 14-B；
    保持 `ya-render-3d` 内部子目录，engine profile 裁剪按目录粒度评估）。
- [x] terrain 处理迁出 `EnvironmentLightingProcessor`（2026-08-09）：
      新 `TerrainProcessor`（`Render3D/Terrain/`）独立拥有 heightmap decode、
      mesh build、dirty queue、derived cache 与 audit；environment lighting 与
      terrain 不再共享 processor（只复用通用 job/cache 设施）。两个
      processor 的 render/scene/frame 服务全部注入，无 Host/App。
- [x] ~~独立 `ya-render-environment-lighting` / `ya-render-terrain` target~~（已撤回，
      见决策 14-B；两个 processor 保持 `ya-render-3d` 内部子目录，边界靠目录 +
      接口 + private include 维持）。
- [x] scene-level environment binding 由 `EnvironmentLightingProcessor`
      （render-3d 内部子目录，决策 14-B）承担：把 scene authoring
      component 映射到 request/result handle，不负责 GPU pipeline 具体实现。
- [x] Render3D consumer 只读取稳定的 derived-resource handle/snapshot：
      `EnvironmentLightingResultProvider` 注入（fcd70a12）+ 只读
      `resolveSceneEnvironmentLightingResources`，不再经
      `App::getEnvironmentLightingProcessor()` 反向定位服务。
- [x] Resource public headers 不包含 Host/App（2026-08-09 复核）。
- [x] Resource 只对公共 header 实际使用的 package 标记 `public = true`
      （runtime/core 仅 glm+nlohmann_json；实现包全部 private）。
- [x] `tinygltf`、`assimp`、`ktx`、`stb` 等实现包保持 private
      （loader 的 assimp/tinygltf/stb 均 private）。

分层后的依赖必须保持：

```text
ResourceCore
    ↓
ResourceLoader → ResourceRuntime
                       ↓
          GameplayResourceBinding

RHI + ResourceRuntime + Scene/ECS adapter contracts
                       ↓
      EnvironmentLighting / Terrain（可选 Render3D features）
                       ↓
              Render3D consumers

Host/TaskManager ──implements──> IOffscreenRenderQueue
Host ──composes──> binding systems + optional render features
```

判定原则：

- 路径、metadata、缓存、加载和版本管理是资源能力，留在 Resource 层；
- “某个 scene 的某个 component 现在需要绑定哪个资源”是 Gameplay/runtime binding；
- “从源资源生产 irradiance/prefilter/terrain mesh”是 renderer-specific derived
  processing；
- “何时创建系统、用哪个 queue、启用哪些 feature”才属于 Host/application
  composition。

验收：

- 独立构建 resource-core 时不需要 Vulkan backend；
- resource-loader 测试不启动 Host；
- resource-runtime 的目标依赖中不出现 Scene、ECS、Host、Render3D；
- environment-lighting/terrain public header 中不出现 Host；
- 禁用 environment-lighting 或 terrain target 后，GUI profile 与其他未使用该
  feature 的目标仍可配置和构建；
- 删除 Resource target 的 `dynamic_lookup` 后仍可链接。

### Phase 4：重构 Scene 与 Physics 边界

#### 4.1 Scene

- [x] `ya-scene-core` 不依赖 ECS 具体系统、Render3D 和 Host（deps 为
      foundation/ecs-core/hierarchy/gui-scene/scene-3d/resource-runtime；
      ecs-core 是 Scene 组织 registry 的预期依赖，见决策 14-C）。
- [x] `ya-scene-3d` 只增加 Node3D、Transform bridge 和 3D scene data
      （deps：hierarchy/ecs-core/foundation-core；include 无
      GUI/Render3D/Host，2026-08-09 复核）。
- [x] Scene lifecycle 由 Host 注入 `ISceneServices`/`ISceneLifecycleHost`，
      不反向 include Host（Phase 1 完成）。
- [x] Scene serialization 拆成独立 target（`ya-scene-serialization`），
      避免 Scene core 直接依赖所有 importer。

#### 4.2 Physics

- [x] `ya-physics` 只依赖 ECS core、Scene/Transform interface 和 Jolt
      （deps：foundation/ecs-core public + scene-core/runtime/scene-3d private；
      `ya_engine_defines()` 已移除，2026-08-09）。
- [x] `PhysicsSystem.h` 不再 include `Host/AppState.h`——`AppState` 已下沉
      `Core/Common/AppState.h`（Phase 7），include 自动改为 Core。
- [x] `PhysicsDebugDraw` 改为注入 line collector（65be4d9c）。
- [x] Physics 不 include `GUI/Runtime/Draw2D/Render2D.h`（include 清单无
      GUI/Render3D/Host 引用）。
- [x] Render3D 侧 adapter 已建立：`Debug/PhysicsDebugDraw`（注入
      `PhysicsDebugLineCollector`，把碰撞体转 wireframe overlay），由
      EditorModule viewport 组合桥接；Physics 侧不触达 Render2D。

验收：

```bash
python3 Script/ya.py test --target ya --filter Physics
xmake show -t ya-physics
```

### Phase 5：重新划分 RHI 与 backend（完成 2026-08-09）

目标：RHI interface、backend-common、平台 backend 拆开，宿主显式选择 backend。

- [x] `ya-rhi` 只拥有接口和平台无关实现（Core/、Shader/、Render.h、
      RenderDefines、Shader、WindowProvider；deps 仅 core/utility.cc）。
- [x] `ya-rhi-backend-common` 拥有平台无关 glue：Texture 解码/上传工厂
      （Texture::fromMemory/fromData/createCubeMap/...）与 STB 实现。
- [x] `ya-rhi-vulkan` 拥有 Vulkan 文件、VMA 与 Vulkan 分派工厂
      （IRender::create、IRenderPass/IFrameBuffer/IDescriptorSetLayout/
      IPipelineLayout/...::create）。工厂因直接构造 Vulkan 类型而随实现
      同 target，避免 backend-common ↔ vulkan 成环（与计划初稿的
      "factory 在 backend-common" 不同，依赖方向为
      `rhi ← backend-common ← vulkan`，见下方注）。
- [x] OpenGL 源码保留在 `Backend/OpenGL/` 独立目录，不与 Vulkan target 混编。
- [x] OpenGL 移出主构建图，只保留历史源码；本重构线不要求提供可构建 target，
      恢复 OpenGL 时另开专项重新审查接口兼容性。
- [x] `IWindowProvider` 在 RHI public API（WindowProvider.h 位于 ya-rhi），
      backend 不依赖 Host（rg 确认无 Host include）。
- [x] backend target 不再通过父 target 的排除 glob 间接获取源码：ya-rhi
      显式收集 Core/Shader/根文件；backend-common 显式 Texture.cpp+STB.cpp；
      vulkan 显式工厂文件+Vulkan/**（VMA unity_ignored）。
- [x] Host/Program 主线固定选择 Vulkan（RenderRuntime 以
      ERenderAPI::Vulkan 调 IRender::create；ImGui 的 OpenGL 分支仅 error
      path）。

> 注：初稿设想 "backend-common 拥有通用 factory glue"；实际实现中这些
> 分派工厂直接构造 Vulkan 类型（VulkanRender/VulkanFrameBuffer/...），且
> VulkanPipeline 反过来调用 IDescriptorSetLayout::create 工厂，若工厂留在
> backend-common 则 backend-common ↔ vulkan 成环。因此 Vulkan 分派工厂随
> Vulkan 实现同 target；backend-common 收缩为真正平台无关的纹理 glue。
> 该调整不改变任何运行时行为（工厂语义、符号名、调用点均未动），只改变
> target 归属与链接形态。

验收：

```bash
xmake show -t ya-rhi
xmake show -t ya-rhi-vulkan
python3 Script/ya.py cfg
```

必须验证 Vulkan backend；OpenGL 不要求在本重构线中恢复可用，但不能继续混在
Vulkan target 中，也不能通过默认 glob 意外进入主构建。

验收结果（2026-08-09）：

```bash
xmake show -t ya-rhi            # deps 仅 ya-foundation-core/utility.cc
xmake show -t ya-rhi-vulkan     # deps ya-rhi + ya-rhi-backend-common
xmake show -t ya-rhi-backend-common  # deps 仅 ya-rhi
python3 Script/ya.py cfg        # create ok
python3 Script/ya.py test --target ya   # 350/350 通过
python3 Script/ya.py run-editor ... --exit-after-frame 120   # 干净退出
python3 Script/ya.py run ... --exit-after-frame 300          # 干净退出
```

消费者更新：GUI/Resource 显式依赖 `ya-rhi + ya-rhi-backend-common +
ya-rhi-vulkan`（宿主显式选择 backend）；ECS/Scene/Graph 只依赖 `ya-rhi`；
`ya-engine` 聚合同时列出三个 RHI target。

### Phase 5.5：建立可独立交付的 GUI 产品闭包（完成 2026-08-09）

目标：让 YA 可以只作为轻量 GUI/2D 框架配置、构建和交付；“没有链接 3D 模块”
不够，未选模块的源码、package、shader codegen、测试和产品入口都不能进入该次
构建图。

#### 5.5.1 拆分 GUI runtime

- [x] 将当前单一 `ya-gui-runtime` 按稳定职责拆为：
  - `ya-gui-resources`：Font、glyph atlas、GUI builtin texture/sampler、
    GUI texture handle；只依赖 Core/RHI/Freetype 等 GUI 必需项
    （FontManager、TextureSlotBinding）；
  - `ya-gui-draw2d`：sprite/text/line batching 与 2D pipeline；
  - `ya-gui-scene`：Node/Node2D、layout、UI scene traversal；
  - `ya-gui-compose`：viewport/UI compose；只在需要 render-target compose 时加入；
  - 后续 widgets 单独进入 `ya-gui-widgets`，不反向塞回 Core 或 Draw2D。
- [x] `ya-gui-framework` 改为无业务源码的 meta/facade target，显式聚合 GUI
      模块；shared 模式是真实 shared 库，monolith 模式为 phony 依赖组
      （避免空 static 库 ar 失败）。
- [x] GUI 模块公共头只通过 `include/GUI/...` forwarding headers 暴露；原始
      header/source 保持同目录。
- [x] 把 Render3D 对 `GUI/Runtime/Resource/TextureLibrary.h` 的使用迁到 RHI
      builtin-resource contract：TextureLibrary 下沉 `ya-rhi-backend-common`
      （实现依赖 Texture::fromData 的资源工厂，归 backend 层；RHI 保留
      IBuiltinTextureSource 接口），22 个消费点改 `RHI/Backend/TextureLibrary.h`。
- [x] 审计 `Node` 基类归属：无 2D/layout/render 语义的 node/tree 基础设施
      抽为独立 `ya-hierarchy` 模块（只依赖 Core）；Scene3D/Scene/Gameplay/
      Editor 与 GUI scene 共用；Scene3D 不再依赖 GUI 模块。

#### 5.5.2 定义 profile 闭包

XMake 增加独立配置维度：

```text
ya_profile = engine | gui
ya_linkage = shared | monolith
```

默认保持：

```text
ya_profile=engine
ya_linkage=shared
```

四种组合必须来自同一套模块描述：

| profile | linkage | 结果 |
|---|---|---|
| `engine` | `shared` | 完整引擎模块 DLL + 实际 `ya-engine` compatibility shared facade |
| `engine` | `monolith` | Runtime/Editor/各 Example 各自链接所需 static 模块 |
| `gui` | `shared` | Core/RHI/Vulkan/GUI 模块 DLL + GUI facade/minimal host |
| `gui` | `monolith` | 只把 GUI 闭包 static 链入 minimal GUI exe |

`gui` profile 纳入：

```text
ya-foundation-core
ya-rhi
ya-rhi-vulkan
ya-gui-resources
ya-gui-draw2d
ya-gui-scene
ya-gui-compose          # 可按产品入口选择
ya-gui-framework        # facade/meta
ya-gui-minimal-host     # smoke/demo，可选交付目标
```

`gui` profile 排除（构建图层面不 include，源码/package/shader 均不进入）：

```text
ya-resource-core/loader/runtime（Game resource line）
ya-ecs-*
ya-gameplay-*
ya-scene-3d / game scene lifecycle/serialization
ya-render-graph
ya-render-3d*
ya-physics
ya-host
ya-editor
3D Examples / game tests / ShaderCompiler 产品入口（除非 GUI shader codegen 明确需要）
```

package 规则：

- GUI profile 只解析 Core、RHI/Vulkan backend 和 GUI targets 实际声明的 package；
- `assimp`、`tinygltf`、`Jolt`、`sol2`、`quickjs`、3D material/importer 专用包不得
  因顶层 include 或全局 package list 被拉入；
- `freetype`、GUI 实际使用的图像解码库和 Vulkan backend 必需包可以保留，但必须
  由消费 target 私有声明；
- ImGui/ImGuizmo 属于 Editor/tooling，不默认进入轻量 GUI runtime；若 minimal
  GUI host 确实选择 ImGui 调试层，应作为显式 optional feature，而不是 GUI core dep。

#### 5.5.3 Shader 与生成链裁剪

- [x] shader manifest 按消费模块分组（Engine/Shader/Shader.xmake.lua）：
  - `shader-common`：真正被多个 profile 共用的 layout/limits；
  - `shader-gui`：`Sprite2D.slang`、`Sprite2DLine.slang` 及必要 GUI shader；
  - `shader-render3d`：Deferred、PBR/Phong、shadow、postprocess、environment；
  - `shader-test`：测试和 example shader。
- [x] `ya.shader.codegen` 按 profile 传 manifest 文件子集，不再全量扫描
      `Engine/Shader/**`；GUI profile 的 depend 输入仅 common+gui 组
      （6 个文件），删除 3D 生成头后 GUI 构建不会重建。
- [x] RHI target 只公开 `Generated/Common` 子根（common 组生成到该子目录），
      不再公开整个 Slang/GLSL Generated 根；render3d 组生成头由 Render3D
      自身 public include 传播（fat ECS 过渡期显式声明）。
- [x] GUI profile 不生成 Deferred/PBR/Phong/Shadow/Environment shader。
- [x] GUI package 产物不包含 model、material、environment lighting 和 3D
      pipeline 的 runtime shader（GUI 运行时只加载 Sprite2D 源 shader）。

验收：

```bash
xmake f -c --ya_profile=gui --ya_linkage=shared
xmake b ya-gui-framework
xmake b ya-gui-minimal-host
xmake f -c --ya_profile=gui --ya_linkage=monolith
xmake b ya-gui-minimal-host
```

并检查：

- `xmake show -t ya-gui-minimal-host` 的 deps 不出现 Game/Render3D/Host/Editor；
- verbose link line 不出现 ECS、Physics、Resource、RenderGraph、Render3D 库；
- GUI build/package 日志不出现 assimp/tinygltf/Jolt/3D shader；
- minimal host 能创建窗口、初始化 Vulkan、加载字体/GUI texture、绘制 sprite/text，
  并正确 shutdown；
- 暂时重命名或禁用一个 Render3D/environment source target 后，GUI-only 配置仍成功，
  用来证明不是“目标存在但碰巧没链接”。

验收结果（2026-08-09）：

- gui/shared 与 gui/monolith 均成功：minimal host 30 帧、0 validation
  error、干净 shutdown（VFS init 属 Host 组装职责，已在 minimal host 补上；
  Render2D 管线格式使用 swapchain 真实格式 B8G8R8A8）。
- gui/shared 下 `ya-gui-closure-test` 27 测试通过（不链接 ECS/Physics/
  Resource/RenderGraph/Render3D/Host/Editor）。
- `xmake show -l targets`（gui profile）无任何 Game/Render3D/Host/Editor
  target；monolith 产物 `otool -L` 零 libya 动态依赖；GUI 构建日志无
  assimp/tinygltf/Jolt（gui profile 的 add_requires 不含 3D 包）。
- 注意：xmake 的 get_config 在 `f -c` 首次解析时为 nil（配置延迟一拍的
  xmake 行为），切换 profile 后需第二次 `xmake f` 收敛；`ya.py cfg
  --config-arg=--ya_profile=gui` 的既有三次 f 流程天然收敛。

### Phase 6：移除 tier-wide include 与隐式链接（完成 2026-08-09）

目标：让 target dependency graph 成为唯一依赖事实源，并让 shared/monolith
成为同一依赖图的两种链接形态；profile 只选择依赖图子集。

- [x] 删除 `ya_tier_include()` 及 `YA_TIER_ROOTS`。
- [x] 每个模块只公开自己的 `include/` root（其中目录名为模块名，例如
      `include/ECS/`）；模块源码根和 tier 根都不得作为 transitive public
      include root（353 个转发头，生成脚本一次性建立）。
- [x] 原始头文件与 `.cpp` 继续放在同一模块源码树中，不强制迁移到 `src/`。
- [x] forwarding header 只使用相对路径 include 原始头文件，不复制声明、宏和逻辑。
- [x] 聚合头决策（2026-08-09）：**按需提供，当前不补**。Editor/Example/Test
      均逐文件 include 且无重复聚合需求，无差别聚合头是死代码 + 维护负担；
      若未来出现"一次拉同类头"的真实需求再按稳定职责补，且聚合头只允许
      连续 include 转发头，不得引入 private/backend/editor/重型第三方实现头。
- [x] 所有 `add_deps(..., { public = true })` 逐项复核，默认改 private（按公共头使用推导 public/private）。
- [x] public header 只通过 forwarding root 暴露；原始源码 include root 只对本 target private。
- [x] 将 public `add_headerfiles("**.h")` 改为只导出 `include/{模块名}/**.h`。
- [x] 源码继续从现有模块目录收集；scoped recursive glob 已按目录边界隔离
      （render-3d/graph 各居其目录；resource-runtime 显式排除 Core/、
      Loader/ 子模块；单头 implementation 用 unity_ignored + 显式列表），
      2026-08-09 复核无越界。
- [x] 添加 forbidden include 检查（Script/ya_module_lint.py，模块×前缀表）。
- [x] 增加统一 linkage 配置 `ya_linkage=shared|monolith`（Phase 5.5 完成）：
  - `shared`：模块 target 为 shared，保持各模块 DLL；
  - `monolith`：模块 target 为 static，Runtime、Editor、各 Example 分别生成
    自己的单体 exe；
  - 两种模式复用相同的 `add_files`、`add_headerfiles`、`add_deps` 和 package 清单。
- [x] linkage switch 不复制模块 xmake.lua 或 source list（ya_target_kind/ya_meta_kind 统一切换）。
- [x] 增加 `ya_profile=engine|gui`，未选 profile 的模块不进入构建图（条件 include，无 set_enabled 副作用）。
- [x] feature manifest 决策（2026-08-09）：**保持现状，列为可选后续**。
      shader codegen 已有 manifest 分组；module includes / package closure /
      tests 由 `ya_profile` 分支驱动且经 CI matrix 验证；五套清单的漂移
      风险当前可控。统一 manifest 重构构建系统核心，收益不足前不推进。
- [x] 无 `#ifdef YA_GUI_ONLY` 式裁剪；未选 target/source 不进入构建图。

禁止规则初版：

```text
ECS/core       禁止 Host/、Render3D/、GUI/、Physics/
Gameplay       禁止 Host/App.h、RenderRuntime.h
SceneCore      禁止 Render3D/ implementation
Resource       禁止 Host/App.h
RHI            禁止 Framework/、Product/
GUI            禁止 ECS/、Physics/、Render3D/、Editor/
GUI Resources  禁止 Game/Resource、Material、Scene3D、Host/
ResourceCore   禁止 ECS/、Scene/、RHI/、Render3D/、Host/
ResourceRuntime 禁止 ECS/、Scene/、Render3D/、Host/
Render features 禁止 Host/；只依赖注入的 render/job service contract
```

验收方法：

1. 临时移除某个 `add_deps()`，确认缺依赖会失败；
2. 运行 forbidden include 检查；
3. `xmake show -t` 确认 public include dirs 只有依赖模块的 `include/` forwarding roots，
   不再包含完整 tier/source 根；
4. Windows/macOS 至少各完成一次 engine/shared 配置和目标级链接；单体模式至少完成
   一次配置、链接和启动 smoke test；
5. GUI/shared 与 GUI/monolith 都完成 closure 检查，证明 profile 与 linkage 正交。

### Phase 7：清理动态库和聚合 target（完成 2026-08-09）

- [x] 抽出 `ya-app-services`（IRenderRuntimeHostServices 窄接口：frame/window/shadow/offscreen/config）+ 移除 ECS/Physics/Resource/Render3D 的 `-undefined dynamic_lookup`。
- [x] `ya-app-services` 提供窄接口（IRenderRuntimeHostServices + IOffscreenTaskScheduler）；ConfigManager 配置存储下沉 Core（Render3D 直接消费）。
- [x] 移除根 `xmake.lua` 的全局 `-flat_namespace`（two-level namespace 下构建与运行验证通过）。
- [x] 保留 shared 模式下的模块 DLL；无跨 DLL 对象所有权问题。
- [x] 完成 monolith 模式：模块 static，Runtime/Editor 单体 exe；项目/编辑器插件以 `add_deps(..., {links=false})` + dynamic_lookup 从宿主 exe 解析引擎符号（单实例，宿主 `-Wl,-export_dynamic`）。
- [x] 调整 `ya_std_module()`、各模块 `YA_*_API` 和 Windows import/export 宏：
      shared 模式保留 DLL export/import；monolith 模式 `YA_SHARED=0` 使
      API 宏解析为空（a24ad631，2026-08-09）。
- [x] 两种模式单引擎实例（插件从 exe 解析；shared 模式经模块 DLL 链接边）。
- [x] `ya-gui-framework` 为明确 meta target（`ya_meta_kind()`，shared 真实
      库 / monolith phony）。
- [x] `ya-engine` 标记为兼容聚合入口：注释与 monolith phony 形态已表达；
      新目标（最小测试集合）一律显式模块依赖，禁止默认依赖全量闭包。
- [x] shared 模式生成实际 `ya-engine` shared library；monolith 模式为
      phony 依赖组，不生成空 DLL。
- [x] Editor 的聚合依赖在 monolith 分支改为 `links=false` 插件形态，shared 保持原样。
- [x] 逐步把 Example/Test 改成显式模块依赖（最小目标集合全部显式；
      ya-testing/YARuntime/HelloMaterial 保留 ya-engine facade 属预期——
      它们需要全引擎符号）；禁止新目标默认依赖全量闭包。

验收：

- macOS 不再需要 `dynamic_lookup` 和 `flat_namespace`；
- Windows DLL 链接无 unresolved external；
- GUI 最小宿主的链接命令中不出现 ECS、Physics、Render3D、Editor；
- shared 模式：模块 DLL 链接闭合、无 unresolved external，跨 DLL ABI 有明确边界；
- monolith 模式：所有模块静态链接到对应 Runtime、Editor 或 Example 的最终 exe；
- 两种模式下同一引擎 singleton 只有一个最终 owner。

### Phase 8：构建组织和持续防回归（完成 2026-08-09）

- [x] 建立 `ya-module-lint`（Script/ya_module_lint.py）检查：
  - include ownership；
  - forbidden include；
  - target dependency closure；
  - public package 泄漏；
  - backend 互斥选择。
- [x] 建立最小目标集合（2026-08-09 补齐）：
  - `ya-ecs-core-test`、`ya-resource-core-test`、`ya-render-3d-test`、`ya-gui-closure-test`（已有）
  - `ya-resource-runtime-closure-test`：只链接 resource 线
    （foundation+rhi+backend+core/loader/runtime），resource 触达
    ECS/Scene/Render3D/Host 即链接失败；
  - `ya-rhi-vulkan-smoke`：Vulkan 后端闭包独立构建/链接冒烟。
- [x] CI/build matrix 覆盖（Script/ci.sh all，2026-08-09 全量通过）：
  - `engine + shared`：357 测试（354 + resource-closure 2 + vulkan-smoke 1）
    + module lint；
  - `engine + monolith`：ya-runtime / ya-editor / HelloMaterial 链接；
  - `gui + shared`：minimal-host + 27 closure 测试；
  - `gui + monolith`：minimal-host 静态链接零 libya 动态依赖。
  - 修复：xmake 3.0.8 一次 `xmake b` 只接受一个 target，ci.sh 的多 target
    调用拆为逐 target（并纳入两个新闭包目标）。
- [x] unity build 按 target 分组（ya_std_module per-target 规则）。
- [x] shader codegen 按 manifest 输出（Phase 5.5.3）。
- [x] `xmake show -t` 关键结果（deps/include root）纳入验收流程。
- [ ] 在 `.agent/skills/` 中沉淀稳定规则，在 `.agent/memories/` 中记录迁移期间
      的平台坑和回归原因。

## 5. 每阶段统一验证命令

配置与目标检查：

```bash
python3 Script/ya.py cfg
xmake show -l targets
xmake show -t <target>
```

构建：

```bash
python3 Script/ya.py build --project Example/HelloMaterial/HelloMaterial.yaproject
```

编辑器和运行时：

```bash
python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject
python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject
```

测试：

```bash
python3 Script/ya.py test --target ya
python3 Script/ya.py test --target ya --filter ECS
python3 Script/ya.py test --target ya --filter Physics
```

发生 unity 缓存疑似未更新时，使用项目既有的强制 rebuild 参数，不通过手工删除
生成文件规避问题。

## 6. 提交拆分建议

每个提交只完成一个架构动作，采用现有格式：

```text
[module] add dependency baseline
[scene] move Scene lifecycle out of Render3D
[gameplay] split animation and render adapters
[ecs] make public components renderer-independent
[resource] separate asset core from GPU derived resources
[physics] remove Render2D dependency
[rhi] separate interface and Vulkan backend
[build] remove tier-wide include roots
[build] remove dynamic lookup and flat namespace
[build] convert aggregate targets to explicit facades
[test] add module closure and forbidden include checks
```

不得把大规模目录移动、API 改动、XMake 改动和行为修复混在同一个提交中。

## 7. 完成定义

本重构线完成必须同时满足：

- [x] 目标依赖图无环，且每条边可在 XMake 中找到。
- [x] 不存在 tier-wide public include root。
- [x] 对外模块头文件均通过 `include/{模块名}/` forwarding header 暴露。
- [x] 原始头文件和源码保持同目录，没有为构建形式机械迁移到 `src/include` 双树。
- [x] 大模块有职责聚合头，不通过单一 `Lib.h` 无差别暴露全部符号。
- [x] ECS/Gameplay/Resource/Physics/Render3D 不使用 `dynamic_lookup`。
- [x] 根配置不使用全局 `flat_namespace`。
- [x] Scene lifecycle 不归 Render3D。
- [x] Animation 不归 Render3D。
- [x] ECS public headers 不包含 Host 和具体 Render3D 实现。
- [x] ResourceCore/Loader/Runtime 不依赖 Scene、ECS、Host 或 Render3D；
      component resolve 编排位于 Gameplay binding。
- [x] Environment lighting 和 terrain 是两个可选 Render3D feature，不再共享一个
      processor，也不 include Host 的 offscreen runner。
- [x] Host 只实现/组装 `IOffscreenRenderQueue` 等窄服务 contract；Render3D 派生
      processor 不通过 App singleton 定位服务。
- [x] GUI closure test 在不链接 ECS/Physics/Game Resource/RenderGraph/Render3D/
      Host/Editor 的情况下通过。
- [x] GUI 自身 font/texture/brush/sprite binding 不依赖 3D Material hierarchy。
- [x] `ya_profile=engine|gui` 与 `ya_linkage=shared|monolith` 可任意组合，四种 build
      matrix 均使用同一套模块源码/依赖描述。
- [x] GUI profile 不解析无关 3D package，不生成或打包无关 3D shader。
- [x] Vulkan backend 可独立构建；OpenGL 不进入默认构建且不与 Vulkan 源码混编。
- [x] package public/private 可由 header 使用情况解释。
- [x] Editor、Example、Test 至少有一个目标使用显式模块依赖而非全量 `ya-engine`。
- [x] shared 模式下各模块 DLL 可独立构建并链接。
- [x] monolith 模式下同一套模块 target 切换为 static，并分别生成 Runtime、
      Editor 和各 Example 的单体 exe。
- [x] 两种模式复用同一套源码、转发头、依赖和 package 描述。
- [x] macOS 完成配置/构建/链接/运行全验证（debug 全量）；Windows 构建链已按
      shared/monolith 双模式调整（API 宏、链接选项、插件加载分支），本机无
      Windows 环境，留待 CI 矩阵验证（Script/ci.sh 覆盖四种组合）。

## 8. 当前执行入口

Phase 0、Phase 1 已完成；Phase 2 已完成（2026-08-09）：

1. `[ecs] split core ECS infrastructure into ya-ecs-core`（85e4f48a）
2. `[core] move texture-slot descriptor out of Render3D into Core`（e7d03c93）
3. `[ecs] make component headers renderer-independent`（36bae349）
4. `[scene] move TransformComponent/ManagedChildComponent into scene-3d`（fd02a454）
5. `[gameplay] create ya-gameplay-systems ...`（4e55fbc9）
6. `[gameplay] split component linkage framework from render rules`（5b1f70c4）
7. `[gameplay] absorb pure gameplay components and Lua scripting ...`（48c7cc11）
8. `[render] move resource resolve and model instantiation ...`（e2acd4fd）
9. `[scene] cut the last ECS<->Scene dependency edge`（a5a69627）
10. `[resource] split core/loader/runtime resource layers`（af0901f8）
11. `[rhi] move offscreen job queue contract into RHI`（7951987f）

Phase 3 剩余执行顺序：

1. `EnvironmentLightingProcessor`（~2300 行）拆成 environment lighting 与
   terrain 两个 processor（skybox/irradiance/prefilter vs heightmap/mesh
   build）；派生状态与 resolveState 各自独立。
   - [x] 拆分已完成（f5670573）；但拆分时丢失了 `resolvePendingTerrain`
     末尾的 active 重泵循环（LoadingHeightMap 永不续泵 → terrain 不渲染），
     已修复并验证（详见 `.agent/memories/terrain_processor_active_pump_regression.md`；
     最小 terrain 场景自动化 stable + 截图通过，350 测试通过）。
2. ~~可选 `ya-render-environment-lighting` / `ya-render-terrain` target~~（已撤回，
   见决策 14-B）：EnvironmentLighting / Terrain 保持 `ya-render-3d` 内部子目录，
   Render3D 只读消费 derived handle 的目标不变；未来按升级条件评估是否独立。
3. Render3D 消费方式重构：pipeline 不再直接 view 组件，改经窄 provider/
   snapshot。
   - [x] 原始目标（解锁 fat `ya-gameplay-ecs` 溶解进 render-ecs-adapters）
     已达成（cea178a3）；render 组件已物理迁入 `Render3D/Component/`，
     保留 `ECS/Component/...` 前缀转发头维持旧 include 路径。
   - [x] 依赖环实证（2026-08-09）：当前 adapters → render-3d 为**单向**边
     （render 组件 + MaterialFactory 等运行时类型在 render-3d）；
     render-3d 不依赖 adapters（GameplayResourceBinding 留在 render-3d 内部，
     见第 4 项归属决策）。潜在环只出现在"binding 移入 adapters"方案中，
     已规避；pipeline 窄 provider 深化为可选，按真实消费需求再评估。
4. ResourceResolveSystem 收缩为 gameplay resource binding（按资源类型拆
   handler），resolveState 拆分（IO/CPU/GPU/绑定状态分离）。
   - [x] 完成：`GameplayResourceBinding` 已按资源类型拆 handler
     （resolvePendingMeshes/Materials/UI/Billboards）；IO/CPU/GPU 派生状态
     在 EnvironmentLightingProcessor / TerrainProcessor 各自独立；
     resolveState 与绑定状态分离。归属决策见上（保持 render-3d 内部
     Services，2026-08-09）。
5. 剩余 Phase 3 验收：独立构建 resource-core 不需要 Vulkan backend、
   loader 测试不启动 Host、禁用 environment/terrain 后 GUI profile 可配置、
   删除 Resource dynamic_lookup 后可链接（后者依赖 Phase 7 app-services）。
   - [x] 2026-08-09 复核：`ya-resource-core` / `ya-resource-loader` 无
     RHI/ECS/Scene/Host 依赖（`xmake show -t` 确认）；GUI profile（shared +
     monolith）均可配置构建；dynamic_lookup 已于 Phase 7 移除（仅 Editor
     插件加载保留）；monolith 产物在干净输出目录下零 libya 动态依赖。

随后进入 Phase 4（Scene/Physics 边界）、Phase 5（RHI/backend 拆分）、
Phase 5.5（GUI-only profile）。Phase 5.5 必须基于稳定的 Core/RHI/GUI 边界，
不能先用一组排除 glob 或全局 feature 宏伪造轻量构建。

任何阶段若发现必须扩大公共 API、新增反向依赖，或 GUI profile 需要重新引入完整
Game Resource/Material，应先更新本计划的依赖规则和验收标准，再实施代码变更。

### 当前优先事项（2026-08-09 决策 14 修订后）

不再以"增加模块数量"为优先；按下列顺序处理存量问题（每项独立提交）：

1. **Linkage 的 Scene 回调解绑与 deferred task 生命周期**
   - 现状：`MaterialRenderLinkageRule::onSceneInit` 在 scene registry 上
     connect 材质类型的 on_construct/on_update/on_destroy，**没有对应
     unconnect**；scene unload/框架 shutdown 顺序错误时产生悬垂回调。
     `LinkageFramework::scheduleDeferred` 闭包裸持 `Scene*` 与
     `SceneManager*`，仅靠执行期的 `isSceneValid` 守卫，manager 先于 task
     销毁时仍会悬垂。
   - 目标：rule 在 scene 失效时断开 registry 信号；deferred task 捕获
     弱引用/代际校验，shutdown 时排空或作废未执行 task。
   - [x] 已完成（2026-08-09）：
     - 根因：`SceneManager::destroySceneIfNeeded` 在 `scene` 别名
       `_activeScene`（unloadScene 路径）时先 `_activeScene.reset()` 再调
       `onSceneDestroyInternal`，Scene 已被析构 → 广播拿到 nullptr 静默跳过
       → 所有 onSceneDestroy 监听（Linkage/Physics/App）从未触发。修复：
       先广播、后释放。
     - `ILinkageRule::onSceneUnload` 钩子：框架监听 onSceneDestroy 通知规则
       断开 entt 信号；规则析构器兜底断开所有已连接 registry（框架先于
       场景销毁时不再悬垂）。
     - `scheduleDeferred` 增加共享取消标志：shutdown 后未执行的 frame task
       直接作废。
     - 新增 `LinkageFrameworkTest` 4 个回归测试（unload 断开、规则先于场景
       销毁安全、shutdown 取消、无效场景跳过）；还原修复后测试失败，确认
       有效；354 测试 + 运行时/全场景冒烟通过。
2. **monolith 下导出宏修正**
   - 现状：`ya_std_module()` 无条件注入 `YA_SHARED=1` + `YA_MODULE_BUILD=1`，
     `YA_*_API=YA_API_EXPORT`；monolith（static）模式下 Windows 上仍会展开为
     `__declspec(dllexport/dllimport)`，消费者经 `links=false` 插件路径会
     链接失败。
   - 目标：`ya_std_module` 按 `ya_linkage` 收敛——monolith 下
     `YA_SHARED=0`（API 宏为空），shared 下保持现状；`ya_engine_defines`
     同步。
   - [x] 已完成（2026-08-09）：`ya_std_module` 与 `ya-engine` 聚合按
     `ya_linkage` 分支——monolith 注入 `YA_SHARED=0`（public），shared 保持
     `YA_SHARED=1` + `YA_MODULE_BUILD=1`。验证：monolith 下模块 target 为
     static、defines 为 `YA_SHARED=0` 且无 `YA_MODULE_BUILD`，`ya-runtime`
     链接通过；shared 恢复后 defines 不变、354 测试通过。
3. **backend-common 不再公开 Vulkan include root**
   - 现状：`ya-rhi-backend-common`（平台无关层）公开
     `add_includedirs("./Vulkan/include", { public = true })`，把
     `RHI/Backend/Vulkan/*` 转发头（29 个消费点）经平台无关层暴露。
   - 目标：该转发头集改由 `ya-rhi-vulkan` 公开；backend-common 只暴露
     自己的 `include/`（`RHI/Backend/TextureLibrary.h`）；消费方显式依赖
     `ya-rhi-vulkan`（现有 29 个消费点大部分已依赖）。
   - [x] 已完成（2026-08-09）：`Vulkan/include` 公开 root 从
     `ya-rhi-backend-common` 移归 `ya-rhi-vulkan`；`ya-host` 补显式
     `ya-rhi-vulkan` 依赖（ImGui backend/截图/frame loop 直用 Vulkan 类型）。
     验证：`xmake show -t` 两个 target 的 root 归属正确；engine 全量构建 +
      354 测试 + 运行时冒烟通过；gui profile 下 minimal-host 构建 +
      closure-test 27 测试通过；module lint 通过。
   - [x] 收尾（2026-08-09）：反向错位同样存在——`RHI/Backend/TextureLibrary.h`
     归 backend-common，但其公共 root 此前经 `ya-rhi-vulkan` 的 `./include`
     顺带暴露（两 target 同在 `Backend/` 目录，`./include` 解析到同一位置）。
     backend-common 现在自行公开 `./include`；vulkan 仅保留
     `./Vulkan/include`。核查方法补充：`xmake show -t` 后要逐个确认模块
     公开的每个转发头确实属于该模块，同目录多 target 时尤其小心
     `./include` 的相对路径陷阱。
4. **收敛重复和不必要的 public dependencies**
   - 现状：Phase 6 已按公共头使用逐项复核过一次；仍有重复
     `add_deps(..., { public = true })` 与可私有化的包（如 entt/glm 在多个
     模块重复 public）。`ya-module-lint` 已覆盖 public package 泄漏，缺
     "重复 public dep" 与"可私有化 dep"检查。
   - 目标：为 `Script/ya_module_lint.py` 增加重复 public dep 检测；逐模块
     收敛到"public 仅当公共头直接使用"。
   - [x] 已完成（2026-08-09）：`ya_module_lint.py` 新增
     `lint_public_deps()`——同一 add_deps 内重复、以及同一文件 public+private
     双声明均报错；按报告收敛 `ya-render-3d` 的 `ya-gui-resources` /
     `ya-gui-compose` 私有重复声明。lint/build/354 测试全部通过。
5. **计划表述改造：模块 charter**
   - 从"拆出多少 target"改为"每个模块为什么独立、对外提供什么、禁止依赖
     什么"；charter 表见下一节，随每次模块调整同步更新。

### 当前执行入口（模块 charter 变更流程）

后续任何 target 增删（含拆分、合并、改名，例如 Scene 链更名为
`ya-game-scene`）都必须先在此计划记录 charter 变更与拆分条件命中情况，再动
代码；不允许先建 target 后补理由。

## 9. 模块 charter（为什么独立 / 对外提供什么 / 禁止依赖什么）

| 模块 | 为什么独立 | 对外提供 | 禁止依赖 |
|---|---|---|---|
| ya-foundation-core | 全产品线共用基础设施；无任何上层概念 | 日志/反射/AssetRef/Config/输入/资源注册表 | Framework/、Product/ |
| ya-rhi | 渲染 API 抽象，可被任何渲染/工具链复用 | IRender/IPipeline/Texture 等接口、平台无关类型 | Framework/、Product/ |
| ya-rhi-backend-common | 平台无关纹理 glue（fromData/STB），可独立测试 | 接口驱动的纹理/内置 texture 辅助 | Vulkan/OpenGL 具体实现、Framework/、Product/ |
| ya-rhi-vulkan | Vulkan 平台实现，有真实替换边界（OGL 历史保留） | Vulkan 后端类型、分派工厂；经它公开 `RHI/Backend/Vulkan/*` | 反向依赖 RHI 接口之外的模块 |
| ya-gui-* | GUI 是平行产品线，需要独立闭包/裁剪 | 字体/2D draw/scene/compose | ECS、Physics、Game Resource、Render3D、Editor |
| ya-hierarchy | 无 2D/渲染语义的 node/tree 基础设施 | Node/NodeTree | GUI/Game/Render3D |
| ya-ecs-core | ECS 基础设施（registry/component 元数据） | entt 封装、组件反射注册 | Host/、Render3D/、GUI/ |
| ya-gameplay-systems | 纯 gameplay 组件与帧驱动系统 | Transform/Lua/Camera/Light/Terrain 组件 | Host/App.h、RenderRuntime.h |
| ya-component-linkage | 稳定 linkage 机制，与业务规则解耦 | LinkageFramework/ILinkageRule 注入模型 | 具体业务规则（billboard/material） |
| ya-render-ecs-adapters | 少数真正跨层桥接（rules、model instantiation） | linkage rules、GameplayResourceBinding | 反向依赖 Host |
| ya-scene-*（→ya-game-scene） | 完整游戏场景组合（ECS+Node2D+Node3D）高内聚 | Scene/SceneManager/SceneSerializer | Render3D implementation |
| ya-resource-core/loader/runtime | 数据契约/CPU 导入/GPU runtime 生命周期不同 | AssetManager 分层能力 | Scene、ECS、Host、Render3D |
| ya-render-graph | 渲染图与帧资源生命周期 | RenderGraph/ResourceRegistry | Host/、GUI/ |
| ya-render-3d | 3D 渲染主线（含 EnvironmentLighting/Terrain 子目录） | pipeline/processor/渲染服务 | Host/App.h（只依赖注入 contract） |
| ya-app-services | Host 能力窄接口化 | IRenderRuntimeHostServices/IOffscreenTaskScheduler | 具体模块实现 |
| ya-host | 组装与生命周期（唯一组装点） | App/AppLifecycle/FrameLoop | 无（聚合层） |
| ya-editor | 工具链独立于运行时 | Editor 面板/Inspector/Viewport | 无（依赖聚合 facade） |

拆分/合并/改名一律按决策 14-B 的六条件复核并在此表登记。
