---
name: ya-render-arch
description: YA Engine 渲染架构、Renderer 边界与 shader 生成链路。
---

## 适用场景

- 修改渲染管线、Renderer/DeviceState、后端实现或 RenderTarget / RenderPass
- 需要判断改动应放在抽象层、运行时编排层，还是平台后端层
- 关键词涉及：`Renderer`、`RenderDeviceState`、`IRender`、`IRenderTarget`、`IRenderPass`、`VulkanRender`、`OpenGLRender`


## 目录锚点

- `Engine/Source/Applications/GameRuntime/`：产品应用循环、生命周期与自动化
- `Engine/Source/Framework/RHI/`：渲染硬件接口层（`Render.h` / `Core/` / `Shader/`）
- `Engine/Source/Framework/RHI/Backend/Vulkan/`：后端实现；`Backend/OpenGL/` 仅历史参考，不构建
- `Engine/Source/Framework/Render/Render3D/`：DeviceState/Coordinator、管线与渲染服务
- `Engine/Source/Framework/Render/Graph/`：RenderGraph 编译与执行
- `Engine/Shader/`：Slang 源与生成头

公开头一律从公开路径读（`RHI/Render.h`、`Render3D/RenderDeviceState.h`），
模块根目录只有私有头与 `.cpp`。


## 细节参考（按需读，不要全载）

| 何时读 | 文件 |
| --- | --- |
| 改管线 / 组合层 / 2D draw path / Dynamic Rendering 约束 | `references/architecture.md` |
| 理解帧索引与五个计数器（多窗口世界） | `references/frame-indices.md` |
| 改 shader 生成链（Slang → 生成头） | `references/shader-pipeline.md` |

## 相关 skills

- `ya-build`：改到 shader 生成、xmake 目标、测试入口时一起看
- `resource-system`：涉及 offscreen preprocess、skybox / environment lighting 时一起看
- `material-flow`：涉及材质上传、descriptor、runtime material 时一起看
- `cpp-style`：需要收敛类边界、生命周期、最小改动时一起看
- `debug-review`：做 Vulkan 问题复盘或提交前自检时一起看


## Windows DLL Boundary

规则与排查单统一在 `../cross-platform/SKILL.md` 规则 6 与
`../../memories/windows_dll_boundary.md`，不在这里重复。
render/runtime 视角只需记住一句：运行时库若含单例 / registry / UI context，
必须有唯一 owner，不能由多个 DLL 各自静态持有。


## 决策原则

1. 公共能力先落抽象层，再按后端补实现。
2. 后端差异只在平台层扩散，不反向污染上层接口。
3. 渲染编排优先放在公开 `Renderer` / pipeline / system，不要把 orchestration 打散到 component，也不要再并列一个几乎无状态的 Coordinator。
4. 遇到渲染异常时，优先检查初始化顺序、资源生命周期、layout transition，再查 shader / pipeline state。
5. 生成文件是只读产物；shader 头不对时修 `Shader.xmake.lua`、`slang_gen_header.py`，不要直接改 `Generated/*`。


## 退出条件

- 能明确回答一段逻辑属于抽象层、Renderer / pipeline 编排层，还是平台后端层
- shader 常量、生成头与 C++ 使用链路一致
- 渲染问题已经定位到初始化、时序、layout、shader 或 pipeline state 之一，而不是混在一起

