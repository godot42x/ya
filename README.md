# YA Engine

[中文说明](./Docs/README.zh-CN.md)

YA Engine is a C++20 game engine project focused on practical rendering, runtime architecture, and editor tooling. Vulkan is the primary rendering backend, with OpenGL compatibility kept in the codebase while active development centers on Vulkan.

The engine currently includes an EnTT-based ECS, ImGui editor layers, Lua scripting through sol2, a custom reflection system, resource management, shader header generation, and runnable examples.

## Features

- Vulkan-first renderer with Render/RHI abstractions and OpenGL compatibility code.
- EnTT ECS with component and system layers for runtime features.
- Resource pipeline for assets, handles, dirty queues, models, meshes, textures, fonts, and metadata.
- ImGui-based editor infrastructure and runtime app entry points.
- Lua integration through sol2.
- Custom reflection utilities and generated shader-facing C++ headers.
- XMake-only build workflow with Makefile shortcuts for daily use.

## Requirements

- A C++20 compiler.
- [XMake](https://xmake.io/).
- Vulkan SDK.
- Python 3 for setup and shader header generation scripts.
- `make` for the documented shortcut commands.

On macOS, `make cfg` also runs the repo setup script that checks and initializes the local Vulkan SDK under `Engine/ThirdParty/VulkanSDK/` when needed.

## Quick Start

```bash
git clone https://github.com/godot42x/ya.git
cd ya
git submodule update --init --recursive

make cfg
make r t=HelloMaterial
```

`make cfg` prepares third-party assets, configures a debug build, and refreshes `compile_commands.json`.

## Common Commands

```bash
make cfg                           # configure dependencies, debug mode, compile_commands.json
make b t=HelloMaterial             # build an example target
make r t=HelloMaterial             # build and run an example target
make r t=GreedySnake               # build and run another example
make test                          # build and run the default ya-testing target
make test t=ya r_args="Suite.Test" # run a single GoogleTest filter
```

Use XMake directly when you need finer control:

```bash
xmake l targets                    # list available targets
xmake b TargetName                 # build a target
xmake run TargetName               # run a target
xmake project -k compile_commands  # refresh compile_commands.json
xmake ya-shader                    # regenerate shader C++ headers
```

Generated shader headers under `Engine/Shader/*/Generated/` should be treated as build outputs. Change the Slang/GLSL sources or generation scripts instead of editing generated headers by hand.

## Project Layout

```text
Engine/Source/Core/       core systems, math, logging, reflection, scripting
Engine/Source/Render/     renderer abstractions, materials, pipelines, RHI
Engine/Source/Platform/   platform render and window backends
Engine/Source/ECS/        EnTT components and systems
Engine/Source/Resource/   asset loading, handles, metadata, dirty queues
Engine/Source/Editor/     ImGui editor layers
Engine/Source/Runtime/    app entry points and runtime orchestration
Engine/Source/Scene/      scene graph and node hierarchy
Engine/Shader/            Slang/GLSL sources and generated headers
Example/                  runnable examples
Test/                     GoogleTest targets
```

## Development Notes

- XMake is the only supported build system.
- Prefer the Makefile shortcuts for routine build, run, test, and configuration work.
- The default engine target is `ya`; runnable entry points are usually example targets.
- Shader-facing C++ types are generated from Slang/GLSL sources.
- The engine currently develops Vulkan as the main backend.

## License

YA Engine is licensed under the [MIT License](./LICENSE).
