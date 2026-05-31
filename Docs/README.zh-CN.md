# YA Engine

[English README](../README.md)

YA Engine 是一个 C++20 游戏引擎项目，重点放在实用渲染、运行时架构和编辑器工具链。当前主渲染后端是 Vulkan，代码库中保留 OpenGL 兼容路径，但主要开发围绕 Vulkan 展开。

项目目前包含基于 EnTT 的 ECS、ImGui 编辑器层、基于 sol2 的 Lua 脚本、自定义反射系统、资源管理、shader 头文件生成链，以及可运行示例。

## 功能概览

- Vulkan 优先的渲染器，包含 Render/RHI 抽象和 OpenGL 兼容代码。
- 基于 EnTT 的 ECS 组件与系统层。
- 资源管线，覆盖 asset、handle、dirty queue、model、mesh、texture、font 和 metadata。
- ImGui 编辑器基础设施与 runtime app 入口。
- 通过 sol2 集成 Lua。
- 自定义反射工具，以及从 shader 生成的 C++ 侧类型头文件。
- 只使用 XMake 的构建工作流，并提供 Makefile 日常快捷命令。

## 环境要求

- C++20 编译器。
- [XMake](https://xmake.io/)。
- Vulkan SDK。
- Python 3，用于 setup 和 shader 头文件生成脚本。
- `make`，用于 README 中的快捷命令。

在 macOS 上，`make cfg` 会运行仓库 setup 脚本，并在需要时检查和初始化 `Engine/ThirdParty/VulkanSDK/` 下的本地 Vulkan SDK。

## Quick Start

```bash
git clone https://github.com/godot42x/ya.git
cd ya
git submodule update --init --recursive

make cfg
make r t=HelloMaterial
```

`make cfg` 会准备三方资源、配置 debug 构建，并刷新 `compile_commands.json`。

## 常用命令

```bash
make cfg                           # 配置依赖、debug 模式和 compile_commands.json
make b t=HelloMaterial             # 构建示例目标
make r t=HelloMaterial             # 构建并运行示例目标
make r t=GreedySnake               # 构建并运行另一个示例
make test                          # 构建并运行默认 ya-testing 测试目标
make test t=ya r_args="Suite.Test" # 运行单个 GoogleTest filter
```

需要更精细控制时，可以直接使用 XMake：

```bash
xmake l targets                    # 查看所有可用 target
xmake b TargetName                 # 构建 target
xmake run TargetName               # 运行 target
xmake project -k compile_commands  # 刷新 compile_commands.json
xmake ya-shader                    # 重新生成 shader C++ 头文件
```

`Engine/Shader/*/Generated/` 下的生成头应视为构建产物。需要修改 shader-facing 类型时，应修改 Slang/GLSL 源文件或生成脚本，不要手动编辑生成文件。

## 项目结构

```text
Engine/Source/Core/       核心系统、数学、日志、反射、脚本
Engine/Source/Render/     渲染抽象、材质、pipeline、RHI
Engine/Source/Platform/   平台渲染与窗口后端
Engine/Source/ECS/        EnTT 组件与系统
Engine/Source/Resource/   asset 加载、handle、metadata、dirty queue
Engine/Source/Editor/     ImGui 编辑器层
Engine/Source/Runtime/    app 入口与 runtime 编排
Engine/Source/Scene/      场景图与节点层级
Engine/Shader/            Slang/GLSL 源码与生成头
Example/                  可运行示例
Test/                     GoogleTest 目标
```

## 开发说明

- XMake 是唯一支持的构建系统。
- 日常构建、运行、测试和配置优先使用 Makefile 快捷命令。
- 默认引擎目标是 `ya`；可运行入口通常是 `Example/` 下的示例目标。
- Shader-facing C++ 类型由 Slang/GLSL 源文件生成。
- 当前主要开发 Vulkan 后端。

## License

YA Engine 使用 [MIT License](../LICENSE)。
