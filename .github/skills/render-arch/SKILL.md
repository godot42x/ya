---
name: ya-render-arch
description: YA Engine 渲染架构、RenderRuntime 边界与 shader 生成链路。
---

## 适用场景

- 修改渲染管线、RenderRuntime、后端实现或 RenderTarget / RenderPass
- 需要判断改动应放在抽象层、运行时编排层，还是平台后端层
- 关键词涉及：`RenderRuntime`、`IRender`、`IRenderTarget`、`IRenderPass`、`VulkanRender`、`OpenGLRender`

## 当前架构要点

1. `App` 不再直接编排整条渲染管线；当前是 `App` 持有 `std::unique_ptr<RenderRuntime>`，由 `RenderRuntime` 持有 `IRender*`、管线对象、screen RT / render pass、offscreen command buffer。
2. 渲染抽象仍以 `IRender` 为核心；Vulkan 是主后端，OpenGL 仍是兼容后端。
3. `IRenderPass` / `IRenderTarget` 仍然是有效抽象，不要假设项目已经完全去掉 render pass 概念。
4. 离屏预处理（如 cylindrical -> cubemap、cubemap -> irradiance）由 `ResourceResolveSystem` + `OffscreenJobRunner` 编排，不要把这类流程重新塞回组件里。
5. 资源时序仍是 Vulkan 改动时的第一检查项：避免在 frame recording 中途重建正在使用的 GPU 资源，必要时延迟到下一帧。

## 目录锚点

- `Engine/Source/Runtime/App/`
- `Engine/Source/Render/`
- `Engine/Source/Platform/Render/Vulkan/`
- `Engine/Source/Platform/Render/OpenGL/`
- `Engine/Shader/`

## 相关 skills

- `build`：改到 shader 生成、xmake 目标、测试入口时一起看
- `resource-system`：涉及 offscreen preprocess、skybox / environment lighting 时一起看
- `material-flow`：涉及材质上传、descriptor、runtime material 时一起看
- `cpp-style`：需要收敛类边界、生命周期、最小改动时一起看
- `debug-review`：做 Vulkan 问题复盘或提交前自检时一起看

## 决策原则

1. 公共能力先落抽象层，再按后端补实现。
2. 后端差异只在平台层扩散，不反向污染上层接口。
3. 渲染编排优先放在 `RenderRuntime` / pipeline / system，不要把 orchestration 打散到 component。
4. 遇到渲染异常时，优先检查初始化顺序、资源生命周期、layout transition，再查 shader / pipeline state。
5. 生成文件是只读产物；shader 头不对时修 `Shader.xmake.lua`、`slang_gen_header.py`、`glsl_gen_header.py`，不要直接改 `Generated/*`。

## Shader 生成流程

当前链路不是单一 Slang 路径，而是配置 + Slang + GLSL 三段：

```text
Engine/Config/Engine.jsonc
  -> Shader.xmake.lua Step 0
  -> 生成 Engine/Shader/GLSL/Common/Limits.glsl
  -> 生成 Engine/Shader/Slang/Common/Limits.slang

Slang 源文件
  -> slang_gen_header.py
  -> Engine/Shader/Slang/Generated/*.slang.h

GLSL 源文件
  -> glsl_gen_header.py
  -> Engine/Shader/GLSL/Generated/*.glsl.h

C++
  -> include 生成头
  -> 使用 slang_types:: / ya::glsl_types 命名空间中的常量与结构体
```

落地规则：

1. 不要手写与 shader uniform 对应的 C++ 结构体。
2. 配置常量优先以 `Engine/Config/Engine.jsonc` 为单一事实源，其余文件只消费不重定义。
3. 若值变更，优先改配置或生成脚本，再运行 `xmake ya-shader`。

## Dynamic Rendering / Layout 约束

1. Vulkan 路径仍依赖显式 layout transition，不要假设驱动会自动兜底。
2. `AttachmentDescription::initialLayout` / `finalLayout` 必须与实际 begin / end rendering 约定一致。
3. 多 layer image 的 barrier 必须覆盖所有 layer / mip，避免只过渡 layer 0。
4. 若怀疑 layout 问题，先看 `VulkanCommandBuffer`、`VulkanImage`、`VulkanRenderTarget` 的 transition 路径是否一致。

## 退出条件

- 能明确回答一段逻辑属于抽象层、RenderRuntime / pipeline 编排层，还是平台后端层
- shader 常量、生成头与 C++ 使用链路一致
- 渲染问题已经定位到初始化、时序、layout、shader 或 pipeline state 之一，而不是混在一起