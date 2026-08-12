# AGENTS.md

本文件只提供主入口所需的最小上下文。先读这里，再按需进入 `./.agent/`。不要因为“可能有用”就预读全部 skills / memories。

## Project

YA Engine 是 C++20 游戏引擎，主渲染后端为 Vulkan，兼容 OpenGL；使用 EnTT ECS、ImGui 编辑器、Lua（sol2）和自定义反射系统。

## Main Commands

构建系统只有 XMake；日常工作流优先走 `python3 Script/ya.py`。

```bash
python3 Script/ya.py cfg
python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject
python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject
python3 Script/ya.py package --project Example/HelloMaterial/HelloMaterial.yaproject
python3 Script/ya.py test --target ya --filter Suite.Test
```

需要精细控制时再直接用 XMake：

```bash
xmake l targets
xmake b TargetName
xmake run TargetName
xmake ya-shader
xmake project -k compile_commands
```

更完整的构建、profiling、命令与排障规则，进入 `./.agent/skills/ya-build/SKILL.md`。

## Working Mode

- 主入口保持克制，只拿完成当前任务所需的最小上下文。
- 问题不明确时先读 `./.agent/skills/soul/SKILL.md`。
- 问题明确后，只进入一个主 skill；必要时再串行切换下一个。
- 遇到历史回归、已知坑、相似故障时，才额外读取 `./.agent/memories/*.md`。
- `./.agent/misc/` 不是规范来源，只是辅助分析资料。

完整索引见 `./.agent/AGENTS.md`。

## Skill Routing

默认优先级：`ya-build > gui-framework > cross-platform > profiling > vscode > resource-system > material-flow > render-arch > cpp-style > code-reorganize > debug-review`

- 构建、目标、编译、shader 生成、测试：`./.agent/skills/ya-build/SKILL.md`
- GUI 框架（WidgetTree/控件/布局契约/Render2D pass slot/GUI host 诊断与 teardown）：`./.agent/skills/gui-framework/SKILL.md`
- 跨平台（Windows/MSVC 与 macOS/Clang 切换、DLL 导出、designated initializer 顺序、平台差异编译/链接报错）：`./.agent/skills/cross-platform/SKILL.md`
- profiling、automation trace、性能冒烟：`./.agent/skills/profiling/SKILL.md`
- VS Code、clangd、launch、tasks：`./.agent/skills/vscode/SKILL.md`
- 资源加载、resolve、dirty queue、environment lighting：`./.agent/skills/resource-system/SKILL.md`
- ECS -> material -> render consumer：`./.agent/skills/material-flow/SKILL.md`
- RenderRuntime、后端边界、shader 生成链：`./.agent/skills/render-arch/SKILL.md`
- C++ 风格、所有权、类布局：`./.agent/skills/cpp-style/SKILL.md`
- 文件拆分、目录重组：`./.agent/skills/code-reorganize/SKILL.md`
- 崩溃排查、review、自检：`./.agent/skills/debug-review/SKILL.md`

## Core Rules

0. 禁止做 "目前能用就行" 的修复以及重构，应该以架构师的视角来思考问题。避免在补丁之上打补丁，hack之上补hack。遇到目前架构不合理，不支持的时候，及时重构，不断重构是项目良好迭代的关键。
1. 只使用 XMake，不引入 CMake。
2. 生成文件只读；修生成链，不手改 `Generated/*`。
3. Shader-facing C++ 类型以 Slang/GLSL 生成头为单一事实源；不要手写 UBO / SSBO / push constant / indirect command 镜像结构。
4. 保持最小改动，不混入无关重构。
5. 遵循现有抽象，不平行造新接口。
6. 不在帧录制中途重建 GPU 资源；延迟到安全时机。
7. 命令录制期引用到的 GPU 资源、image view、descriptor 数据必须至少活到 queue submit 完成。
8. `Render2D` 使用左上角原点坐标系。
9. 日志只用 `YA_CORE_TRACE/DEBUG/INFO/WARN/ERROR/ASSERT`。
10. 代码风格倾向成员变量在函数声明之前(data-orient-programming)

## Repo Facts

- `Engine/Source/Core/`：核心系统、日志、反射、脚本
- `Engine/Source/Render/`：渲染抽象层
- `Engine/Source/Framework/GUI/`：GUI 框架（widgets/compose/draw2d/resources/tooling/app-host 五模块 + 聚合 target）
- `Engine/Source/Platform/Render/`：Vulkan / OpenGL 后端
- `Engine/Source/Runtime/Application/`：应用入口、生命周期与自动化
- `Engine/Source/Runtime/Rendering/`：RenderRuntime、渲染管线与渲染服务
- `Engine/Source/Editor/`：编辑器层
- `Engine/Shader/`：Slang / GLSL 与生成头
- `Example/GUIWorkbench/`：retain-mode GUI demo app（页面注册进 FWorkbenchSurface）
- `Example/`：项目 / 示例
- `Test/`：GoogleTest

## Documentation Policy

- 稳定架构、长期工作流、可复用规则写到 `./.agent/skills/`
- 历史故障、回归根因、项目坑点写到 `./.agent/memories/`
- 阶段性重构目标与进度写到 `./.agent/plan/`；该目录是阶段性工件，不默认代表当前主工作流
- 顶层 `AGENTS.md` 只保留当前默认路径；兼容路径只做简短说明

## Git

提交格式：`[module] message`

例如：`[vulkan] fix swapchain resize`、`[material/phong] add specular`
