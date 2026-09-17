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

默认优先级：`ya-build > gui-framework > cross-platform > profiling > vscode > resource-system > material-flow > scene-object-boundary > render-arch > cpp-style > code-reorganize > debug-review`

- 构建、目标、编译、shader 生成、测试：`./.agent/skills/ya-build/SKILL.md`
- GUI 框架（WidgetTree/控件/布局契约/Render2D pass slot/GUI host 诊断与 teardown）：`./.agent/skills/gui-framework/SKILL.md`
- 跨平台（Windows/MSVC 与 macOS/Clang 切换、DLL 导出、designated initializer 顺序、平台差异编译/链接报错）：`./.agent/skills/cross-platform/SKILL.md`
- profiling、automation trace、性能冒烟：`./.agent/skills/profiling/SKILL.md`
- VS Code、clangd、launch、tasks：`./.agent/skills/vscode/SKILL.md`
- 资源加载、resolve、dirty queue、environment lighting：`./.agent/skills/resource-system/SKILL.md`
- ECS -> material -> render consumer：`./.agent/skills/material-flow/SKILL.md`
- 生成物边界（gizmo / 图标 / 子 mesh、派生视觉的序列化与可编辑规则、视图 feature 位）：`./.agent/skills/scene-object-boundary/SKILL.md`
- RenderRuntime、后端边界、shader 生成链：`./.agent/skills/render-arch/SKILL.md`
- C++ 风格、所有权、类布局：`./.agent/skills/cpp-style/SKILL.md`
- 文件拆分、目录重组：`./.agent/skills/code-reorganize/SKILL.md`
- 字体栈（FontManager/Atlas/Bitmap+SDF/flavor split/CJK fallback）：`./.agent/skills/font-rendering/SKILL.md`
- 崩溃排查、review、自检：`./.agent/skills/debug-review/SKILL.md`

## Core Rules

0. 禁止做 "目前能用就行" 的修复以及重构，应该以架构师的视角来思考问题。避免在补丁之上打补丁，hack之上补hack。遇到目前架构不合理，不支持的时候，及时重构，不断重构是项目良好迭代的关键。
1. 只使用 XMake，不引入 CMake。
2. 生成文件只读；修生成链，不手改 `Generated/*`。
3. Shader-facing C++ 类型以 Slang 生成头为单一事实源（Slang 是唯一 shader 语言，GLSL/shaderc 路径已退役）；不要手写 UBO / SSBO / push constant / indirect command 镜像结构。
4. 保持最小改动，不混入无关重构。
5. 遵循现有抽象，不平行造新接口。
6. 不在帧录制中途重建 GPU 资源；延迟到安全时机。
7. 命令录制期引用到的 GPU 资源、image view、descriptor 数据必须至少活到 queue submit 完成。
8. `Render2D` 使用左上角原点坐标系。
9. 日志只用 `YA_CORE_TRACE/DEBUG/INFO/WARN/ERROR/ASSERT`。
10. 代码风格倾向成员变量在函数声明之前(data-orient-programming)
11. 执行plan的时候，需要分批次分类提交代码，plan files 的改动和实际改动一个commit提交
12. 禁止以制造提交数量或表面进度为目标工作：每个 checkpoint 必须对应用户明确要求的单一可验收目标，不能把无关修复、占位实现、重复拆分或仅改文档伪装成推进。
13. 禁止污染提交历史：提交前必须检查 diff、测试和计划映射；若一个目标尚未形成完整闭环，不得拆成多个“进度”提交。发现方向偏离时先停止编码并报告，不得继续用新提交掩盖偏离。
14. 计划执行必须先复述当前目标和边界，再实施；每个 checkpoint 说明保留/未完成/偏离项。不得为了满足“继续”而臆造新任务。
15. 一个头文件只有一个物理位置：需要公开的头放在 `<module>/include/<Module>/...`，
    公开路径就是相对 include 根的路径；模块根不再保留同名副本，`include/` 下不得出现
    转发 stub，一个物理头只能有一个公开路径。include 字符串一律写公开路径。
    细则见 `./.agent/skills/code-reorganize/SKILL.md`。

## Repo Facts

- `Engine/Source/Framework/`：引擎无关可复用能力层（Core / RHI / App / GUI /
  Render / Resource / Scene / Physics / ECS / Hierarchy）。每个子目录是一个
  xmake target，自带 `xmake.lua`。
- `Engine/Source/Applications/`：组装出的应用形态（GameRuntime / GameEditor）。
- `Engine/Source/<tier>/<Module>/include/<Module>/...`：该模块公开头的唯一
  物理位置，公开路径即相对 `include/` 的路径（如 `Core/Base.h`）。模块根目录
  只放私有头与 `.cpp`；读任何头都从公开路径进。
- `Engine/Source/Framework/GUI/`：GUI 框架。`Runtime/{Widgets,Layout,Binding,Declarative,Compose}`
  是独立源码目录，但 `ya-gui-widgets` 一个 target 同时发布
  `Widgets/include`、`Layout/include`、`Binding/include`、`Declarative/include`
  四个 include 根，所以公开路径是 `GUI/Widgets/...`、`GUI/Layout/...` 等。
- `Engine/Source/Framework/RHI/Backend/{Vulkan,OpenGL}/`：Vulkan / OpenGL 后端。
- `Engine/Shader/`：Slang 源与生成头（`Generated/*` 只读）。
- `Engine/Programs/`：可执行入口（YARuntime / ShaderCompiler / ya-cli）。
- `Engine/Test/`：GoogleTest（`ya-testing`）；`test/`：零散的单文件 / 链接实验。
- `Example/`：示例项目；`Example/GUIWorkbench/` 是 retain-mode GUI demo app。

## Documentation Policy

- 稳定架构、长期工作流、可复用规则写到 `./.agent/skills/`
- 历史故障、回归根因、项目坑点写到 `./.agent/memories/`
- 阶段性重构目标与进度写到 `./.agent/plan/`；该目录是阶段性工件，不默认代表当前主工作流
- 顶层 `AGENTS.md` 只保留当前默认路径；兼容路径只做简短说明

## Git

提交格式：`[module] message`

例如：`[vulkan] fix swapchain resize`、`[material/phong] add specular`
