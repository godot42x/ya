# AGENTS.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**YA Engine** (Yet Another Engine) — a C++ game engine with Vulkan (primary) and OpenGL backends. Uses EnTT ECS, ImGui editor, Lua scripting (sol2), and a custom reflection system.

## Build / Run / Test

Build system is **XMake** (NOT CMake). Default target is `ya`. Example targets are under `Example/`.

```bash
# Makefile wrapper (preferred)
make r t=HelloMaterial           # build + run target
make b t=HelloMaterial           # build only
make f=true r t=HelloMaterial    # clean + build + run
make test t=ya r_args="TestName" # run single test (GoogleTest filter)
make cfg                         # reconfigure (debug, compile_commands)

# Direct XMake
xmake                            # build all
xmake b TargetName               # build single target
xmake run TargetName             # run target
xmake l targets                  # list available targets
xmake f -m debug -y              # reconfigure debug mode
xmake ya-shader                  # regenerate shader headers

# Tests (GoogleTest)
xmake b ya-testing && xmake r ya-testing --gtest_filter=Suite.Test
```

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
- **ResourceHandlePool<T>** — type-safe handle-based resource storage (index + generation). `TextureHandlePool` specializes this
- **ResourceDirtyQueue** — tracks resources needing GPU re-upload
- **TextureSlot** — serializable texture reference (`_path` + `_handle`), lives in materials
- **Material system** — `MaterialComponent` → `Material` → `TextureSlot[]` → descriptor sets
- **Reflection** — `YA_REFLECT_BEGIN`/`YA_REFLECT_FIELD`/`YA_REFLECT_END` macros; drives serialization, editor UI, and ECS registration
- **Lua scripting** — sol2 bindings, scripts in `Engine/Content/Lua/` and `Content/Scripts/`

### Shader Pipeline

```
.slang source → xmake ya-shader → slang_gen_header.py → Generated/*.slang.h → #include in C++
```

GPU struct definitions live in `slang_types::` namespace. **Never hand-write C++ structs matching shader uniforms** — always use the generated headers with their `alignas(16)` and `static_assert` guards.

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
| `build` | Build errors, target selection, compiler issues |

Priority when task spans multiple: `BUILD > VSCODE > RESOURCE_SYSTEM > MATERIAL_FLOW > RENDER_ARCH > CPP_STYLE > DEBUG_REVIEW`

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
