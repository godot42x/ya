# AGENTS.md

本文件只提供最小必要上下文。先读这里，按需进入 `./.agent/` 的具体资料，不要一次性加载全部 skills / memories。

## Project

YA Engine 是 C++20 游戏引擎，主渲染后端为 Vulkan，兼容 OpenGL；使用 EnTT ECS、ImGui 编辑器、Lua（sol2）和自定义反射系统。

## Build / Run / Test

构建系统只有 XMake，日常优先走 `make` 包装：

```bash
make cfg                           # 配置三方依赖、debug、刷新 compile_commands.json
make b t=HelloMaterial             # 构建目标
make r t=HelloMaterial             # 构建并运行目标
make test                          # 运行默认测试目标
make test t=ya r_args="Suite.Test" # 运行单个 GoogleTest case
```

需要精细控制时再直接用 XMake：

```bash
xmake l targets
xmake b TargetName
xmake run TargetName
xmake project -k compile_commands
xmake ya-shader
```

要求：`xmake`、C++20 编译器、Vulkan SDK。

## Repo Facts

- `Engine/Source/Core/`：核心系统、数学、日志、反射、脚本
- `Engine/Source/Render/`：渲染抽象层
- `Engine/Source/Platform/Render/`：Vulkan / OpenGL 后端
- `Engine/Source/ECS/`：EnTT ECS、组件、系统
- `Engine/Source/Resource/`：AssetManager、HandlePool、ResourceDirtyQueue
- `Engine/Source/Editor/`：ImGui 编辑器层
- `Engine/Source/Runtime/App/`：应用入口与 RenderRuntime
- `Engine/Source/Scene/`：场景图与节点层级
- `Engine/Shader/`：Slang / GLSL 与生成头
- `Example/`：可运行示例
- `Test/`：GoogleTest

## Working Mode

- 主入口保持克制，只拿完成当前任务所需的最小上下文。
- 问题不明确时先读 `./.agent/skills/soul/SKILL.md`。
- 问题明确后，只进入一个主 skill；必要时再串行切换下一个。
- 遇到历史回归、已知坑、相似故障时，才额外读取 `./.agent/memories/*.md`。
- `./.agent/misc/` 不是规范来源，只是辅助分析资料。

完整索引见 `./.agent/AGENTS.md`。

## Skill Routing

默认优先级：`ya-build > vscode > resource-system > material-flow > render-arch > cpp-style > code-reorganize > debug-review`

- 构建、目标、编译、shader 生成、测试：`./.agent/skills/ya-build/SKILL.md`
- VS Code、clangd、launch、tasks：`./.agent/skills/vscode/SKILL.md`
- 资源加载、resolve、dirty queue、environment lighting：`./.agent/skills/resource-system/SKILL.md`
- ECS -> material -> render consumer：`./.agent/skills/material-flow/SKILL.md`
- RenderRuntime、后端边界、shader 生成链：`./.agent/skills/render-arch/SKILL.md`
- C++ 风格、所有权、类布局：`./.agent/skills/cpp-style/SKILL.md`
- 文件拆分、目录重组：`./.agent/skills/code-reorganize/SKILL.md`
- 崩溃排查、review、自检：`./.agent/skills/debug-review/SKILL.md`

## Core Rules

1. 只使用 XMake，不引入 CMake。
2. 生成文件只读；修生成链，不手改 `Generated/*`。
3. Shader-facing C++ 类型以 Slang/GLSL 生成头为单一事实源；不要手写 UBO / SSBO / push constant / indirect command 的镜像结构。
4. 保持最小改动，不混入无关重构。
5. 遵循现有抽象，不平行造新接口。
6. 不在帧录制中途重建 GPU 资源；延迟到安全时机。
7. `Render2D` 使用左上角原点坐标系。
8. 文件命名按类名或稳定职责，不用 `module.part.cpp` 这类命名。
9. 目录按稳定职责分层；facade/owner 与 helper/importer 分开收敛。
10. 日志只用 `YA_CORE_TRACE/DEBUG/INFO/WARN/ERROR/ASSERT`。
11. 非明确要求时，不生成多余文档或显而易见的注释。

## Code Style

- 类型：`PascalCase`
- 枚举：`E<Name>::T` 或 `enum class E<Name>`
- 私有成员：`_camelCase`
- 公有成员：`camelCase`
- 函数 / 局部变量：`camelCase`
- 常量：`UPPER_SNAKE_CASE`
- 接口前缀：`I`
- 数据类型前缀：`F`
- 类中成员变量声明放在方法前面

## Documentation Policy

- 稳定架构、长期工作流、可复用规则写到 `./.agent/skills/`
- 历史故障、回归根因、项目坑点写到 `./.agent/memories/`
- 完成任务后，如产生可复用经验，判断是否需要补充 memory 或更新相关 skill

## Git

提交格式：`[module] message`

例如：`[vulkan] fix swapchain resize`、`[material/phong] add specular`
