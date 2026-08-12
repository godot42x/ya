# GUI/editor 渲染生命周期：首帧管线 prep 时序、负尺寸 scissor 溢出、VMA teardown 顺序

> 2026-08-13，修 editor 启动/退出崩溃时定位。三类问题同属"GPU 资源生命周期与
> prep/teardown 顺序"，全部已修复并入 HEAD（见 `git log --oneline -12` 附近提交）。

## 1. 首帧管线未准备（启动崩溃，SIGTRAP）

**现象**：`make run t=HelloMaterial` 首帧崩溃：
`[Error] Render2D pipeline for pass slot 1 was not prepared before command recording`
（QuadRender::flush 断言）。

**根因**：pass-slot 重构把 RuntimeUIComposite 管线 prep 移到
`RenderRuntime::renderFrame` 的**有门槛**代码块（`if (uiFrameSnapshot) { uiTarget = getViewportDisplayImageShared(); if (uiTarget) prep(...); }`），
但 `getViewportDisplayImageShared()` 返回的 display image（postprocess output /
viewport output）是**帧图执行期间（renderWorldFrame）才创建**的：

- 首帧 prep（world 渲染前）→ image 不存在 → prep 跳过；
- 首帧 record（world 渲染后）→ image 存在 → record 执行 → flush → 断言崩溃。
- 重构前该管线在 `Render2D::init` 无条件预建，所以从不触发。

**修复**：`renderFrame` 顶部无条件预建 UI 管线，用新增
`RenderRuntime::getViewportDisplayImageFormat()`（postprocess 开启 → `getPostprocessColorFormat()`=R8G8B8A8，
否则 `getViewportColorFormat()`）；`IRenderPipeline` 增加 `getPostprocessColorFormat()`。
`preparePassPipeline` 按 format 缓存，display image 出现后自动换真实格式。

**规则**：prep 不能依赖"录制时才会存在"的资源；display image 的 format 是 pipeline 配置的
确定性值，可以在 image 创建前查询。

## 2. 负尺寸布局 → scissor 溢出（editor VUID，非致命但污染日志）

**现象**：editor 运行期 validation error 刷屏：
`VUID-vkCmdSetScissor-offset-00597`（offset.y + extent.height 有符号溢出）。

**根因**：`imgui.ini` 里 `[Window][GUI Workbench] Size=416,52`（极小窗口），workbench 布局
（menu 30 + toolbar 34 + tabbar 30）塞不下 → split pane 产生**负尺寸** rect → clip 负高度
强转 uint32 → scissor 溢出。

**修复**（三层）：
- `UIElement::layout/layoutAssigned` 把 `_layoutRect.extent` clamp ≥0（负尺寸是布局缺陷，
  会传染进 clip/scissor）；
- `QuadRender::flush` 的 scissor 防御性 clamp 到窗口边界；
- `GUIWorkbenchPanel` 首用默认尺寸 `ImGui::SetNextWindowSize(640,420, FirstUseEver)` +
  修掉 imgui.ini 残留的极小窗口。

**规则**：窗口/容器尺寸不可控时，布局必须产出非负 rect；scissor 永远在窗口边界内。

## 3. VMA teardown 顺序（退出崩溃/断言）

**现象**：
- standalone GUIWorkbench 退出 SIGSEGV：`~GUIAppHost → ~VulkanBuffer → vmaDestroyBuffer`；
- editor（ya-runtime --editor）退出断言：
  `Assertion failed: "Unfreed dedicated allocations found!" ~VmaDedicatedAllocationList`。

**根因**（两处独立）：
- `GUIAppHost::FImpl::gpuShotBuffer`（`--gpu-shot` 的 readback buffer）是 FImpl 成员，
  在 `delete render`（VMA 销毁）后随 `_impl` 析构才释放 → 用已销毁 allocator 调
  vmaDestroyBuffer。
- `AssetManager` 纹理缓存里的大图（sponza KTX，VMA **dedicated** allocation）在
  render backend（VMA）销毁后仍存活，进程退出才释放 → VMA 断言。小图走默认 pool
  不触发，所以游戏（纹理小）看似干净、editor（加载 sponza）才炸。

**修复**：
- `GUIAppHost::shutdown()`：`gpuShotBuffer` / `shaderStorage` / `tree` 显式提前到
  `render->destroy()/delete render` 之前释放；
- 新增 `AssetManager::clearTextures()`（走 `textureManager().clear()`），
  `AppLifecycle::quit` 在 `runtime->shutdown()` 前调用；
- 顺带补 `EditorModule::onDetach` 缺失的 `_guiWorkbenchCompositor.shutdown()`。

**规则**：**任何持有 GPU 资源的成员/缓存必须在 VMA allocator 销毁前释放**；teardown
顺序 = 先 waitIdle → 释放全部 GPU 资源（Render2D、command buffers、presentation
targets、readback、shader storage、tree、资产纹理缓存）→ 才 destroy render。

## 预防

1. Render2D 管线 prep 的时机 = "录制开始前"，且不能依赖录制期才产生的资源/格式；
   display image 之类用其确定性格式提前 prep。
2. 布局 rect 非负 + scissor 夹取是底线；遇到 VUID scissor 溢出先查负尺寸来源
   （小窗口 + 固定高度 shell 最典型）。
3. 排查 VMA 断言先看"哪些 VkBuffer/VkImage 在 VMA 销毁后仍存活"：
   大图（dedicated）最容易暴露；`--gpu-shot` / asset 缓存 / editor compose image
   是常见漏网点。
