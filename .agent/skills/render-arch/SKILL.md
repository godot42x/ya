---
name: ya-render-arch
description: YA Engine 渲染架构、Renderer 边界与 shader 生成链路。
---

## 适用场景

- 修改渲染管线、Renderer/DeviceState、后端实现或 RenderTarget / RenderPass
- 需要判断改动应放在抽象层、运行时编排层，还是平台后端层
- 关键词涉及：`Renderer`、`RenderDeviceState`、`IRender`、`IRenderTarget`、`IRenderPass`、`VulkanRender`、`OpenGLRender`

## 当前架构要点

1. `App` 不再直接编排整条渲染管线。当前产品层持有 `RenderDeviceState` + `RenderFrameCoordinator`；这是一次不完整拆分（Coordinator 经 friend 写 DeviceState），不是两个独立 owner。目标是公开 `Renderer::recordFrame(plan, surfaceTarget) -> RecordedFrame`。`IRender` / `VulkanRender` 才是 RHI backend。不要再引入名为 Coordinator 的第二套产品层概念。
2. 渲染抽象仍以 `IRender` 为核心；Vulkan 是主后端，OpenGL 仍是兼容后端。
3. `IRenderPass` / `IRenderTarget` 仍然是有效抽象，不要假设项目已经完全去掉 render pass 概念。
4. 离屏预处理（如 cylindrical -> cubemap、cubemap -> irradiance）由 `ResourceResolveSystem` + `OffscreenJobRunner` 编排，不要把这类流程重新塞回组件里。
5. 资源时序仍是 Vulkan 改动时的第一检查项：避免在 frame recording 中途重建正在使用的 GPU 资源，必要时延迟到下一帧。
6. FrameGraph execute callback 只消费构图阶段生成的 immutable snapshot、typed params
   和 graph-resolved handles；active scene、ECS registry、ResourceResolveSystem 查询
   必须在 graph build 前完成。
7. Deferred/Forward world frame 各自只保留一个顶层 executor；Stage/pass helper 只
   append graph pass 或记录当前 pass，不自行 execute 子图。
8. Game UI 唯一 live 事实源是 `WidgetTree`（`ya-gui-widgets`）；Scene 只保存
   `SceneWidgetEntry` authoring data（inline UIDocument / .yaui 引用）。
   command recording 只消费 `UIFrameSnapshot`（RenderGraph 前由
   `WidgetTree::buildSnapshot` 生成），绝不遍历 live widget 树。
9. 旧 Node2D UI 语义已移除（Phase 6）：不要在新代码里把 UI 挂回 scene tree，
   也不要用 `UISceneRenderer`；场景只保存新格式 `widgetEntries`（inline
   UIDocument / .yaui 引用），`nodeType` legacy UI 迁移已删除（不再支持
   旧格式 scene 文件）。
10. `ya-gui-widgets` 无 Scene/ECS/Render3D/Host/RHI 依赖；texture 归资产缓存、
    font 由 snapshot item 强引用；UI 合成在 world graph 之后（不进 bloom）。
11. 一帧按 Camera 链组织，不是按窗口：`graphics → UI → view compose` 写该相机离屏 RT；
    `display compose → present` 才碰 swapchain。当前 `RenderFramePlan` 仍同时携带 Scene
    与 `PresentFrameInput`，`beginFrameCommandBuffer()` 无 Surface 则拒绝录制，因此
    Scene recording 仍与 present Surface 耦合。acquire/present 由 host `FPresentFrame`
    配对。`CameraFrameInput` 在 graph build 前携带 owner 计算的 view / projection /
    viewProjection / offscreen extent；Forward/Deferred/debug/overlay 只消费该包，
    不从 swapchain/window 猜尺寸。view compose 经 `recordCameraViewCompose` 写 Camera
    离屏 RT；display compose 经 `PresentationGraphService::recordDisplayCompose` 写
    `swapchain[imageIndex]`。swapchain blit 使用 Surface 生命周期的 tone-map CIS，
    不要把 View `post.toneMap` 或空 `RenderDesc` 默认值拿去 bind。`GUIRenderSurface` 只是 compose target，不 acquire/present、
    不读 live WidgetTree。Present 消费方只走 `IRenderSurfaceContext` / `ISwapchain` /
    `buildPresentationImages`，禁止 `as<VulkanSwapChain>()`。不要为 Material/UI 窗复制
    Renderer。对象模型见 `./.agent/plan/archive/gui-multi-os-window-editor/c2_view_model.md`
    与 `c2_present_compose_model.md`；R2 ownership 收口见
    `./.agent/plan/render-view-family/plan.md` 4.0.3。
12. 宿主视口是哪个 View 只有一个判据：`SceneViewDesc::ownsHostViewport()`
    （`composeOntoViewId == 0`，结构属性）。`kPrimarySceneViewId` 不是“主 View”的
    别名，它只是**相机预览 inset 这类离屏 View compose 的目标槽位**；按 id 去找宿主
    View 会造出第二个定义，并让一个 `viewId=7` 的声明在 plan 里成为 display root 却
    不是宿主相机的来源。`SceneRenderPlan::displayRootTask()` 在没人 owns 时返回 null，
    这是正常答案（该 tick 不往宿主视口显示任何东西）；`ExtractedSceneRender::
    hostFrameData()` 同理返回 null，绝不回落到“配对 slot 0”——配对顺序是声明顺序，
    与谁是宿主 View 无关。
13. 宿主视口的图片/尺寸访问器只有**一个来源**，没有兜底链：
    `getActiveViewportImageShared` / `getViewportDisplayImageShared` /
    `getViewportExtent` / `getViewOutput` 都只读本帧 published 的 display root，
    未发布就返回 `nullptr` / `{}`。要回落的调用方自己回落（编辑器 2D 画布用面板
    尺寸、host 用 viewportRect → 窗口尺寸），因为只有调用方知道“没有视口时该显示
    什么”。published 身份由 `RenderDeviceState::publishViewOutputIdentity` 一处写入
    （`0` 清空），不要回到“遍历 family 逐个写、最后写入者赢”，也不要让 pipeline 上
    再留一份上次发布的图当兜底。
14. 录制期不许向“当前状态”提问。帧级事实在 `FramePacket`（tick/flight/clock、host render
    scale、shadow settings、overlay、UI snapshot），View 级事实在该 View 自己的
    `SceneViewDesc` / `SceneViewportTask` / `RenderFrameData` 上。没有 `CameraFrameInput`
    这种“既是这一帧又是主 View”的包，也没有 `cameraForViewRecording` 这种“拷贝宿主再
    逐字段覆盖”的 patching；需要 Scene 相关的 GPU 绑定（skybox / IBL descriptor set、
    env lighting 派生资源）时，由 `RenderDeviceState::resolveViewSceneResources` 在
    `beginFrameCommandBuffer` 之前按 View 解析进 `RenderFrameData.sceneResources`，
    而不是让 pass 在录制中途问 owner“现在哪个 Scene 是当前的”。
15. 不要在 pass 之间共享一个 `IRenderRuntimeServices` 式接口去取 device 上的东西。
    需要的东西要么进 View 数据（上面的 scene resources / clock），要么在构造时注入
    （`DebugRenderSystem` 走 InitDesc）。`getGameplayResourceBinding()` 这类只有声明没有
    消费者的接口方法，出现即删——否则它会成为下一个“录制期全局查询”的入口。

## 目录锚点

- `Engine/Source/Applications/GameRuntime/`：产品应用循环、生命周期与自动化
- `Engine/Source/Framework/RHI/`：渲染硬件接口层（`Render.h` / `Core/` / `Shader/`）
- `Engine/Source/Framework/RHI/Backend/Vulkan/` 与 `Backend/OpenGL/`：后端实现
- `Engine/Source/Framework/Render/Render3D/`：DeviceState/Coordinator、管线与渲染服务
- `Engine/Source/Framework/Render/Graph/`：RenderGraph 编译与执行
- `Engine/Shader/`：Slang 源与生成头

公开头一律从公开路径读（`RHI/Render.h`、`Render3D/RenderDeviceState.h`），
模块根目录只有私有头与 `.cpp`。

## 相关 skills

- `ya-build`：改到 shader 生成、xmake 目标、测试入口时一起看
- `resource-system`：涉及 offscreen preprocess、skybox / environment lighting 时一起看
- `material-flow`：涉及材质上传、descriptor、runtime material 时一起看
- `cpp-style`：需要收敛类边界、生命周期、最小改动时一起看
- `debug-review`：做 Vulkan 问题复盘或提交前自检时一起看

## Windows DLL Boundary

1. Windows 下若某个库包含单例、全局状态、registry、延迟初始化队列或 UI context，不要让多个 DLL 各自静态持有一份。
2. 这类库要么集中到唯一 shared owner，要么由 engine API 做边界封装。
3. 宏/模板注册允许多模块执行，但注册目标必须共享，且注册 API 要幂等。
4. 具体案例与排查单见 `./windows-dll-boundary.md` 与 `../../memories/windows_dll_boundary.md`。

## 决策原则

1. 公共能力先落抽象层，再按后端补实现。
2. 后端差异只在平台层扩散，不反向污染上层接口。
3. 渲染编排优先放在公开 `Renderer` / pipeline / system，不要把 orchestration 打散到 component，也不要再并列一个几乎无状态的 Coordinator。
4. 遇到渲染异常时，优先检查初始化顺序、资源生命周期、layout transition，再查 shader / pipeline state。
5. 生成文件是只读产物；shader 头不对时修 `Shader.xmake.lua`、`slang_gen_header.py`，不要直接改 `Generated/*`。

## Shader 生成流程

Slang 是引擎唯一的 shader 语言（GLSL / shaderc 路径已退役，`Engine/Shader/GLSL` 与 `GLSLProcessor` 都已删除）。当前链路是配置 + Slang 两段：

```text
Engine/Config/Engine.jsonc
  -> Shader.xmake.lua Step 0
  -> 生成 Engine/Shader/Slang/Common/Limits.slang

Slang 源文件
  -> slang_gen_header.py
  -> Engine/Shader/Slang/Generated/*.slang.h

C++
  -> include 生成头
  -> 使用 slang_types:: 命名空间中的常量与结构体
```

落地规则：

1. 不要手写与 shader-facing layout 对应的 C++ 结构体，包括 UBO、SSBO、push constant、indirect command。
2. C++ 侧必须 include `Engine/Shader/Slang/Generated/*.slang.h`，消费 `slang_types::` 里的生成类型。
3. 若生成头缺少需要的 shader-facing 类型，修 shader 源或生成脚本，不在 C++ 侧补 mirror struct。
4. 配置常量优先以 `Engine/Config/Engine.jsonc` 为单一事实源，其余文件只消费不重定义。
5. 若值变更，优先改配置或生成脚本，再运行 `xmake ya-shader`。

## Dynamic Rendering / Layout 约束

1. Vulkan 路径仍依赖显式 layout transition，不要假设驱动会自动兜底。
2. `AttachmentDescription::initialLayout` / `finalLayout` 必须与实际 begin / end rendering 约定一致。
3. 多 layer image 的 barrier 必须覆盖所有 layer / mip，避免只过渡 layer 0。
4. 若怀疑 layout 问题，先看 `VulkanCommandBuffer`、`VulkanImage`、`VulkanRenderTarget` 的 transition 路径是否一致。

## 退出条件

- 能明确回答一段逻辑属于抽象层、Renderer / pipeline 编排层，还是平台后端层
- shader 常量、生成头与 C++ 使用链路一致
- 渲染问题已经定位到初始化、时序、layout、shader 或 pipeline state 之一，而不是混在一起
