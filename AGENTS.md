# AGENTS.md


## Project

**YA Engine** (Yet Another Engine) — a C++ game engine with Vulkan (primary) and OpenGL backends. Uses EnTT ECS, ImGui editor, Lua scripting (sol2), and a custom reflection system.

## Build / Run / Test

Build system is **XMake** (NOT CMake). Default target is `ya`. Example targets are under `Example/`. The `make` wrapper is the normal entrypoint; use direct `xmake` only when you need finer control.

```bash
# Makefile wrapper (preferred)
make cfg                          # run Script/setup_3rd_party.py, configure debug mode, refresh compile_commands.json
make b t=HelloMaterial            # build one target
make r t=HelloMaterial            # build + run one target
make f=true r t=HelloMaterial     # clean, rebuild, run
make test                         # build + run default test target (ya-testing)
make test t=ya r_args="Suite.Test" # run a single GoogleTest case via --gtest_filter
make test t=ya r_args="Suite.*"    # run one GoogleTest suite
make profile                      # open Engine/Saved/Profiling/App.speedscope.json in speedscope

# Direct XMake
xmake                             # build all configured targets
xmake b TargetName                # build one target
xmake run TargetName              # run one target
xmake l targets                   # list available targets
xmake f -m debug -y               # reconfigure debug mode
xmake project -k compile_commands # regenerate compile_commands.json
xmake ya-shader                   # regenerate shader-generated headers
xmake b ya-testing && xmake r ya-testing --gtest_filter=Suite.Test
```

There is no dedicated lint/format target in the repo. `make cfg` / `xmake project -k compile_commands` is the command future tools should use to keep clangd and code navigation accurate.

Requirements: xmake, C++20 compiler (MSVC preferred), Vulkan SDK.

## Architecture

### Directory Layout

```
Engine/Source/
  Core/              Core systems, math, logging, reflection, scripting
  Platform/Render/   Backend implementations (Vulkan/, OpenGL/)
  Render/            Render abstraction (IRender, IRenderTarget, Material/)
  ECS/               Entity-component system (EnTT), components, systems
  Resource/          AssetManager, HandlePool, ResourceDirtyQueue
  Editor/            ImGui editor layer, property inspector, file pickers
  Runtime/App/       Application entry point (App.h), render runtime
  Scene/             Scene graph, Node hierarchy
Engine/Shader/
  Slang/             .slang shader sources
  Slang/Generated/   Auto-generated C++ headers (DO NOT HAND-EDIT)
  GLSL/              Legacy GLSL shaders
Engine/Plugins/      Internal libraries (log.cc, test.cc, reflects-core, yalua)
Example/             Runnable targets (HelloMaterial, GreedSnake)
Test/                Unit tests (GoogleTest)
```

### Key Systems

- **IRender** — abstract rendering interface; `VulkanRender` is the primary backend
- **IRenderTarget** — render surface abstraction (framebuffers, attachments)
- **ISystem** — base for all systems. Subtypes: `EngineSystem` (app lifetime), `GameInstanceSystem` (scene lifetime), `IRenderSystem`, `IMaterialSystem`
- **AssetManager** (singleton) — loads/caches textures and models via handle pools. Textures return `TextureHandle`, models return `ModelHandle`
- **TextureSlot** — serializable texture reference (`_path` + `_handle`), lives in materials
- **Material system** — `MaterialComponent` → `Material` → `TextureSlot[]` → descriptor sets
- **Reflection** — `YA_REFLECT_BEGIN`/`YA_REFLECT_FIELD`/`YA_REFLECT_END` macros; drives serialization, editor UI, and ECS registration
- **Lua scripting** — sol2 bindings, scripts in `Engine/Content/Lua/` and `Content/Scripts/`

### Runtime Flow

```text
EntryPoint.h / example Entry.cpp
  → App::init(AppDesc)
    → init core services (VFS, ConfigManager, Logger, FileWatcher, MaterialFactory)
    → create RenderRuntime
    → create SceneManager
    → register core systems in order:
      ModelInstantiationSystem
      ResourceResolveSystem
      TransformSystem
      ComponentLinkageSystem
    → attach EditorLayer and LuaScriptingSystem
    → load default scene
  → App::run()
    → per-frame logic/update
    → ResourceResolveSystem::onUpdate()
    → RenderRuntime::renderFrame()
```

`App` owns high-level lifetime and state (`SceneManager`, editor/runtime state, registered systems, current scene). `RenderRuntime` owns render backend objects, active render pipeline, screen/offscreen command buffers, skybox/environment-lighting shared resources, and shading-model switching.

### ECS, Resource, and Material Flow

- `ModelInstantiationSystem` expands imported model data into scene entities/components; `ResourceResolveSystem` then resolves the pending mesh/material/skybox/environment-lighting resources for those entities.
- `ResourceResolveSystem` is the cross-cutting bridge between scene authoring data and render-ready GPU resources. It resolves pending meshes/materials/UI/billboards plus skybox + PBR environment lighting assets.
- Material data is intentionally split into authoring components and runtime render materials: component state (for example `PhongMaterialComponent`) syncs into runtime `Material` objects, then `MaterialDescPool<TMaterial, TParamUBO>` uploads per-material UBOs and descriptor sets only when param/resource versions change.
- `TextureSlot` carries serializable authoring references; runtime consumers resolve them into actual textures and descriptor bindings.
- `ResourceDirtyQueue` exists to defer GPU-side refresh/recreation until a safe point in a later frame. Do not recreate render resources in the middle of frame recording.

### Editor and Examples

- `Editor/` is not a separate app: the editor layer is attached from `App` and lives inside the same runtime loop.
- `Example/*` targets are the fastest way to understand expected engine usage patterns. `Example/HelloMaterial` is the main material/render reference target; `GreedSnake` is a 2D-oriented example.

### Shader Pipeline

```text
Engine/Config/Engine.jsonc shader.defines
  → shader_config.py
  → Common/Limits.glsl + Common/Limits.slang

.slang source
  → xmake ya-shader
  → slang_gen_header.py
  → Slang/Generated/*.slang.h

.glsl source
  → xmake ya-shader
  → glsl_gen_header.py
  → GLSL/Generated/*.glsl.h
```

GPU struct definitions live in generated namespaces such as `slang_types::` / `ya::glsl_types`. **Never hand-write C++ structs matching shader uniforms** — always use the generated headers with their `alignas(16)` and `static_assert` guards.

## Domain Knowledge (Skills)

Detailed architecture docs are in `.github/skills/<name>/SKILL.md`:

| Skill | When to read |
|-------|-------------|
| `resource-system` | Handle/HandlePool, AssetManager, texture lifecycle, dirty queue |
| `material-flow` | ECS → material → render pipeline data flow, TextureSlot resolve |
| `render-arch` | Render pipeline, backend boundaries, pipeline/material architecture |
| `cpp-style` | C++ conventions, ownership rules, refactoring patterns |
| `debug-review` | Crash investigation, log analysis, pre-commit review |
| `vscode` | VS Code tasks, launch configs, clangd setup |
| `ya-build` | Build errors, target selection, compiler issues |

Priority when task spans multiple: `BUILD > VSCODE > RESOURCE_SYSTEM > MATERIAL_FLOW > RENDER_ARCH > CPP_STYLE > DEBUG_REVIEW`

## Self improvement

在每次成功完成任务后，判断是否需要记录一下进度和经验到Memory.md中，以便后续参考。并且如果改动了架构，需要及时更新skills和文档中的落后说明与归档。

## Code Style

### Naming

- Types: `PascalCase` — `VulkanRender`, `IRenderTarget`
- Enums: `E<Name>::T` or `enum class E<Name>`
- Private members: `_camelCase`
- Public members: `camelCase`
- Functions/locals: `camelCase`
- Constants: `UPPER_SNAKE_CASE`
- Prefixes: `I` for interfaces, `F` for data types (`FName`, `FAssetPath`)

### Class layout

Member variables declared **before** methods. Prioritize data organization over method grouping.

Small, stable wrapper functions should prefer header inline definitions. Keep `.cpp` implementations for heavier logic, hidden dependencies, or code that should not be duplicated across translation units.

### Memory

- `stdptr<T>` = `std::shared_ptr<T>`, `makeShared<T>(...)` = `std::make_shared<T>(...)`
- Prefer smart pointers; never raw `new/delete` without full lifecycle understanding
- Interfaces return `shared_ptr`

### Logging

Use `YA_CORE_TRACE/DEBUG/INFO/WARN/ERROR/ASSERT` macros only. Never `std::cout` or `printf`.

## Git

Commit format: `[module] message` — e.g. `[vulkan] fix swapchain resize`, `[rhi x material] init pipeline wiring`, `[material/phong] add specular`

## Rules

1. **XMake only** — never introduce CMake
2. **Generated files are read-only** — fix `slang_gen_header.py`, not `*.slang.h` output
3. **No gratuitous docs/comments** — don't generate markdown or add obvious comments unless asked
4. **Follow existing abstractions** — don't introduce parallel interfaces
5. **Minimal changes** — no unrelated refactors in the same change
6. **Resource timing** — avoid resource recreation during frame recording (causes `vk device lost`); defer to next frame
7. **Render2D** — uses top-left origin coordinates
8. **Keep files scoped** — `.cpp` / `.h` files should generally stay under about 1000 lines; when they grow past that, split by stable feature areas or helper responsibilities instead of accumulating unrelated logic in one file
9. **Name files by class or stable responsibility** — prefer names like `AssetTextureManager.h`, `AssetModelManager.cpp`, `AssetTextureImport.cpp`; avoid `module.part.h` / `module.part.cpp` naming such as `AssetManager.TexturePipeline.cpp`
10. **Group files by function/layer** — when a subsystem grows, place facade/owner classes, domain-specific helpers, and implementation details in stable subfolders such as `Resource/Manager/` and `Resource/Texture/` instead of keeping all files flat in one directory
