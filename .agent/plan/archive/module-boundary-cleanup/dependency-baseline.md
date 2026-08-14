# Module Dependency Baseline

> 生成日期：2026-08-08  
> 生成环境：macOS (arm64) / Xcode 26.5 / clang++ C++20 / xmake（debug 配置）  
> 状态：Phase 0 产物，描述当前真实结构，不改变任何运行时行为。  
> 方法：`xmake show -t <target>` 展开结果存于 `./baseline/show-*.txt`；
> include 边由脚本解析 `Engine/Source` 全部 `.h/.cpp` 的 `#include`，
> 按 `ya_tier_include` 根（`Engine/Source/xmake.lua` 的 `YA_TIER_ROOTS`）解析归属。

## 1. Target 清单（13 个引擎模块 + 聚合）

| target | kind | 源码根 | sources | headers | 直接 deps（public 标 *） | 直接 packages（public 标 *） |
|---|---|---|---|---|---|---|
| ya-foundation-core | shared | Foundation/Core | Foundation/Core/**.cpp | Foundation/Core/**.h | utility.cc*, log.cc*, reflects-core* | glm*, nlohmann_json*, libsdl3* |
| ya-foundation-rhi | shared | Foundation/RHI | Foundation/RHI/**.cpp 排除 Backend/** | Foundation/RHI/**.h | ya-foundation-core*, utility.cc* | glm*, entt*, vulkansdk*, libsdl3* |
| ya-foundation-rhi-backend | shared | Foundation/RHI/Backend | **.cpp 排除 OpenGL/**、VMA/STB 单头（unity_ignored） | **.h | ya-foundation-rhi* | vulkansdk*, vulkan-memory-allocator*；glad/stb/ktx/cxxopts |
| ya-gui-runtime | shared | Framework/GUI | Framework/GUI/**.cpp | Framework/GUI/**.h | ya-foundation-core*, ya-foundation-rhi*, ya-foundation-rhi-backend* | glm*；freetype |
| ya-gui-framework | shared | —（聚合，无源码） | — | — | ya-foundation-core*, ya-foundation-rhi*, ya-foundation-rhi-backend*, ya-gui-runtime* | — |
| ya-scene-3d | shared | Framework/Game/Scene/Scene3D | **.cpp | **.h | ya-gui-runtime*, ya-gameplay-ecs*, ya-foundation-core* | — |
| ya-gameplay-ecs | shared | Framework/Game/Gameplay/ECS | **.cpp | **.h | ya-foundation-core*, ya-foundation-rhi* | entt*, glm*, nlohmann_json*, sol2*；lua/quickjs-ng/cxxopts |
| ya-physics | shared | Framework/Game/Physics | **.cpp | **.h | ya-foundation-core*, ya-gameplay-ecs* | joltphysics, glm |
| ya-resource | shared | Framework/Game/Resource | **.cpp（TinyGLTF.cpp unity_ignored） | **.h | ya-foundation-rhi*, ya-foundation-rhi-backend*, ya-gui-runtime* | glm*, nlohmann_json*, tinygltf*；stb/ktx/assimp/vulkansdk/cxxopts |
| ya-render-graph | shared | Framework/Game/Render/Graph | **.cpp | **.h | ya-foundation-rhi* | — |
| ya-render-3d | shared | Framework/Game/Render/Render3D | **.cpp | **.h | ya-gui-runtime*, ya-resource*, ya-render-graph*, ya-scene-3d*, ya-gameplay-ecs*, ya-physics* | glm*, entt*, nlohmann_json*；cxxopts/vma/glad/lua/sol2/quickjs-ng/vulkansdk/stb |
| ya-host | shared | Product/Host | **.cpp | **.h | ya-render-3d*, imgui-local*, imguizmo-local* | libsdl3*, glm*, nlohmann_json*, cxxopts*；asio/vma/glad/lua/sol2/quickjs-ng/vulkansdk |
| ya-editor | shared | Product/Editor | **.cpp | —（无 headerfiles，经 ya-engine 聚合） | ya-engine, imgui-local, imguizmo-local | —（经 ya-engine 传递） |
| ya-engine | shared | —（聚合 facade，唯一 TU 为 ImGui demo） | ThirdParty/ImGui/imgui_demo.cpp | Source/**.h（PCH: Core/Common/FWD.h） | 全部 11 个模块 + utility/log/reflects + imgui/imguizmo（均 public） | 见 Engine/YA.xmake.lua（stb/tinygltf/libsdl3/glm/assimp/ktx/vulkansdk/vma/glad/cxxopts/entt/lua/freetype/nlohmann_json/sol2/joltphysics/quickjs-ng） |

## 2. 平台 linker / 编译策略（macOS baseline）

| 项目 | 现状 | 位置 |
|---|---|---|
| `-flat_namespace` | 全局（所有 target ld/sh flags） | 根 `xmake.lua` |
| `-undefined dynamic_lookup` | ya-gameplay-ecs / ya-physics / ya-resource / ya-render-3d（macOS shflags） | 各模块 xmake.lua |
| `ya_engine_defines()` | 注入全部 12 个 `YA_*_API=YA_API_EXPORT` | ECS / Physics / Render3D / Resource |
| `ya_tier_include(...)` | ECS: 6 个 tier 根；Resource: 3 个；其余模块 1-2 个，全部 public | 各模块 xmake.lua |
| unity build | 默认开启（`ya_enable_unity-build`，batchsize=2），单头实现 unity_ignored | 根 xmake.lua / ya_std_module |

## 3. 聚合 include 边（module → module : 命中次数，全量）

以下为 `Engine/Source` 内可解析的 include 聚合结果（同一文件多行 include 分别计数，
同一 include 命中多个 tier 根时按解析到的文件计数）。

```
  ya-render-3d                 -> ya-foundation-rhi            :  222
  ya-render-3d                 -> ya-render-3d                 :  185
  ya-foundation-core           -> ya-foundation-core           :  125
  ya-render-3d                 -> ya-foundation-core           :  106
  ya-gameplay-ecs              -> ya-foundation-core           :   94
  ya-editor                    -> ya-editor                    :   87
  ya-gameplay-ecs              -> ya-gameplay-ecs              :   84
  ya-host                      -> ya-foundation-core           :   82
  ya-host                      -> ya-host                      :   75
  ya-editor                    -> ya-gameplay-ecs              :   73
  ya-foundation-rhi-backend    -> ya-foundation-rhi            :   68
  ya-resource                  -> ya-resource                  :   66
  ya-editor                    -> ya-foundation-core           :   51
  ya-resource                  -> ya-foundation-core           :   48
  ya-host                      -> ya-gameplay-ecs              :   46
  ya-foundation-rhi            -> ya-foundation-rhi            :   44
  ya-foundation-rhi-backend    -> ya-foundation-core           :   40
  ya-render-3d                 -> ya-gameplay-ecs              :   38
  ya-gameplay-ecs              -> ya-render-3d                 :   36
  ya-host                      -> ya-render-3d                 :   34
  ya-editor                    -> ya-host                      :   31
  ya-render-3d                 -> ya-render-graph              :   31
  ya-foundation-rhi-backend    -> ya-foundation-rhi-backend    :   24
  ya-editor                    -> ya-render-3d                 :   23
  ya-foundation-rhi            -> ya-foundation-core           :   23
  ya-gui-runtime               -> ya-foundation-rhi            :   23
  ya-gui-runtime               -> ya-foundation-core           :   22
  ya-editor                    -> ya-gui-runtime               :   21
  ya-render-3d                 -> ya-resource                  :   20
  ya-gameplay-ecs              -> ya-host                      :   19
  ya-host                      -> ya-foundation-rhi            :   18
  ya-gameplay-ecs              -> ya-resource                  :   17
  ya-render-3d                 -> ya-gui-runtime               :   17
  ya-render-3d                 -> ya-host                      :   17
  ya-editor                    -> ya-foundation-rhi            :   16
  ya-gui-runtime               -> ya-gui-runtime               :   15
  ya-gameplay-ecs              -> ya-foundation-rhi            :   14
  ya-render-graph              -> ya-foundation-rhi            :   12
  ya-editor                    -> ya-resource                  :    7
  ya-host                      -> ya-foundation-rhi-backend    :    7
  ya-render-3d                 -> ya-foundation-rhi-backend    :    7
  ya-resource                  -> ya-foundation-rhi            :    7
  ya-gameplay-ecs              -> ya-gui-runtime               :    6
  ya-host                      -> ya-gui-runtime               :    5
  ya-host                      -> ya-render-graph              :    5
  ya-physics                   -> ya-foundation-core           :    5
  ya-render-graph              -> ya-foundation-core           :    5
  ya-render-graph              -> ya-render-graph              :    5
  ya-gameplay-ecs              -> ya-scene-3d                  :    2
  ya-host                      -> ya-resource                  :    2
  ya-physics                   -> ya-gameplay-ecs              :    2
  ya-physics                   -> ya-render-3d                 :    2
  ya-scene-3d                  -> ya-gameplay-ecs              :    2
  ya-editor                    -> ya-scene-3d                  :    1
  ya-host                      -> ya-physics                   :    1
  ya-host                      -> ya-scene-3d                  :    1
  ya-physics                   -> ya-host                      :    1
  ya-physics                   -> ya-physics                   :    1
  ya-render-3d                 -> ya-physics                   :    1
  ya-render-3d                 -> ya-scene-3d                  :    1
  ya-resource                  -> ya-gui-runtime               :    1
  ya-resource                  -> ya-host                      :    1
  ya-scene-3d                  -> ya-gui-runtime               :    1
```

## 4. 关键违规边（Phase 1-4 的直接清理对象）

| 边 | 文件数 | 代表文件 |
|---|---|---|
| `ya-gameplay-ecs -> ya-host` | 19 | LuaScriptComponent.h, ComponentLinkageSystem.cpp, ComponentLinkageSystem.h, ComponentLinkageSystem.h |
| `ya-gameplay-ecs -> ya-render-3d` | 36 | BillboardComponent.cpp, BillboardComponent.h, BillboardComponent.h, BillboardComponent.h |
| `ya-gameplay-ecs -> ya-resource` | 17 | EnvironmentLightingComponent.cpp, SkyboxComponent.cpp, LuaScriptComponent.cpp, PBRMaterialComponent.h |
| `ya-gameplay-ecs -> ya-gui-runtime` | 6 | BillboardComponent.cpp, PBRMaterialComponent.cpp, PhongMaterialComponent.cpp, UnlitMaterialComponent.cpp |
| `ya-scene-3d -> ya-gameplay-ecs` | 2 | Node3D.cpp, Node3D.cpp |
| `ya-resource -> ya-host` | 1 | AssetManager.cpp |
| `ya-resource -> ya-gui-runtime` | 1 | AssetRef.cpp |
| `ya-render-3d -> ya-host` | 17 | ShadowSettingsConfig.cpp, ShadowSettingsConfig.cpp, DeferredRenderPipeline.cpp, DeferredRenderPipeline.cpp |
| `ya-render-3d -> ya-gui-runtime` | 17 | RenderOverlay.cpp, RenderOverlay.cpp, PhysicsDebugDraw.cpp, GBufferStage.cpp |
| `ya-physics -> ya-render-3d` | 2 | PhysicsSystem.cpp, PhysicsSystem.cpp |
| `ya-physics -> ya-host` | 1 | PhysicsSystem.h |
| `ya-physics -> ya-gui-runtime` | 0 |  |
| `ya-host -> ya-gameplay-ecs` | 46 | AppSceneServices.cpp, AppSceneServices.cpp, AppSceneServices.cpp, AppAutomationControlService.cpp |

## 5. 当前 XMake 中表达的依赖图 vs include 事实

include 事实中**已存在依赖边但没有对应 `add_deps`** 的主要缺口
（依赖由 tier-wide public include root 掩盖）：

| include 事实 | XMake 表达 |
|---|---|
| `ya-gameplay-ecs -> ya-render-3d / ya-resource / ya-gui-runtime / ya-host / ya-scene-3d` | 仅 `ya-foundation-core` + `ya-foundation-rhi` |
| `ya-resource -> ya-host / ya-gui-runtime` | 仅 `ya-foundation-rhi* / backend* / gui*`（host 缺口） |
| `ya-render-3d -> ya-host` | 无 host dep（经 dynamic_lookup 掩盖） |
| `ya-physics -> ya-render-3d / ya-host` | 仅 core + ecs |
| `ya-host -> ya-gameplay-ecs`（46 文件） | 经 ya-render-3d 传递（render-3d 的 public dep） |

## 6. Forbidden include 规则 v1（Phase 0 定稿，Phase 6 落地为配置期检查）

```text
ECS/core       禁止 Host/、Render3D/、GUI/、Physics/
Gameplay       禁止 Host/App.h、RenderRuntime.h
SceneCore      禁止 Render3D/ implementation
Resource       禁止 Host/App.h
RHI            禁止 Framework/、Product/
GUI            禁止 ECS/、Physics/、Render3D/、Editor/
```

当前违反清单（脚本按上述规则扫描）：

| 模块 | 越界目标 | 文件数 |
|---|---|---|
| ya-gameplay-ecs | ya-render-3d | 24 |
| ya-gameplay-ecs | ya-resource | 14 |
| ya-gameplay-ecs | ya-host | 14 |
| ya-gameplay-ecs | ya-gui-runtime | 6 |
| ya-gameplay-ecs | ya-scene-3d | 2 |
| ya-physics | ya-render-3d | 1 |
| ya-physics | ya-host | 1 |
| ya-render-3d | ya-host | 14 |
| ya-resource | ya-host | 1 |

## 7. Baseline 存档

- `./baseline/show-<target>.txt`：13 个 target 的 `xmake show -t` 完整展开
  （deps/packages/links/flags/defines/headerfiles/includedirs/files/编译器与链接命令行）。
- 本文件第 3 节的全量边数据由脚本生成，可复跑：`python3 /tmp/ya_include_analysis.py`（v2 版本输出 JSON 至 `/tmp/ya_edges2.json`）。
