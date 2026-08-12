---
name: gui-framework
description: YA GUI 框架（WidgetTree / 控件 / layout / Render2D pass slot / host 诊断）的模块地图与稳定契约。
---

## 适用场景

- 在 `Engine/Source/Framework/GUI/` 内改控件、布局、事件、快照、合成
- 开发 GUIWorkbench demo / 编辑器内嵌 panel
- 排查 GUI 渲染、布局、生命周期问题（GPU 资源 teardown、pass slot、clip/scissor）

## 模块地图

```text
Framework/GUI/
  Runtime/Resource/   ya-gui-resources   Font/glyph、texture-slot（FontManager::registerFont 可注入合成字体做无 GPU 测试）
  Runtime/Draw2D/     ya-gui-draw2d      Render2D：screen/world 精灵+文本+线条批处理；pass slot 资源池
  Runtime/Widgets/    ya-gui-widgets     UIElement / WidgetTree / UIFrameSnapshot / 控件
  Runtime/Compose/    ya-gui-compose     共享 2D 合成 pass（UIFrameSnapshot -> Render2D）
  Tooling/            ya-gui-tooling     WorkbenchSurface / Workspace（工具 UI 外壳，demo 无关）
  App/                ya-gui-app-host    standalone 宿主：SDL 窗口、Vulkan、帧循环、automation 入口
Example/GUIWorkbench/                    retain-mode demo app（页面注册进 FWorkbenchSurface）
```

`ya-gui-framework` 是聚合 meta target（widgets+compose+tooling 等）；GUI 测试只链 GUI closure
（`ya-gui-closure-test` 不依赖 Scene/ECS/Render3D/Host/Editor）。

## WidgetTree 模型

- 层：`Content / Popup / Tooltip / DragIme`（项目内容不能覆盖系统层 zOrder）。
- 单视觉父契约：`attach`（新成员）/ `reparent`（显式迁移）/ `detach`（递归、清 transient state）。
- 输入：`dispatchEvent` 顶层优先 + Pass/Stop 路由；focus（Tab 遍历）/ pointer capture / hover
  是树级状态；drag&drop 会话（`beginDrag/updateDrag/endDrag/cancelDrag`，payload 为 string）由树管理，
  目标控件实现 `canAcceptDrop/onDrop/setDropHighlight`。
- 快照：`buildSnapshot`（layout dirty 时才 layout + paint）→ 不可变 `UIFrameSnapshot`；
  录制只消费快照。命令录制期绝不读 live tree。

## 布局契约（SizeToContent）

- `UIElement::_bAutoSize`（SizeToContent / Slate DesiredSize 模型）：每轴解析优先级
  `anchor span（stretch）> AutoSize（computeDesiredSize 递归聚合子内容）> _size`。
- `UIText`：AutoSize 时 desired = `font.measureText(text) × lineHeight`；字体经
  FontManager 解析，closure 测试用 `registerFont` 注入合成字体。
- `UIButton`（Content-Slot）：`_contentPadding` + 内容子节点填入内缩 rect（`layoutAssigned`，
  非 anchor 数学）；AutoSize 时 desired = 首可见内容子节点 + padding×2；显式 `_size` 在容器内
  也优先（`computeDesiredSize` 返回 `_size`）。
- 容器（`UIContainer`）：主轴按 desired 排列、cross 轴拉伸到内容区；`computeDesiredSize`
  聚合子项。scroll/split 读取内容 desired。slot policy（Auto/Fill 每子项）是未来扩展点，
  v1 cross 统一拉伸。
- 布局 rect 尺寸永远 clamp ≥0（负尺寸会传染进 clip/scissor）。

## Render2D pass slot

- `Render2D` 不认识 game/editor pass；调用方经 `Render2D::acquirePassSlot()` 获取不透明
  `Render2DPassSlot`（每进程静态递增，上限 `FQuadRender::kMaxPassSlots = 8`），资源按 slot
  懒分配（GUI app 只用自己需要的 slot）。
- 映射位置：`Render2DComposePass::composePassSlot(kind)`（4 个 compose kind 各占一个 slot）、
  `RenderOverlay::viewportOverlayPassSlot()`。
- 管线 prep 必须在录制前：depthless（depthFormat==Undefined → uiPipeline）与 depth 变体
  （screenPipeline）按目标附件格式缓存。**运行时 UI 复合管线必须在首帧 world 渲染前 prep**
  （display image 在帧图执行期才创建；见 memory：first-frame prep 时序坑）。
- 单帧多批次 flush 共享一块 host-visible 顶点缓冲：每批次写不同区域 + `vertexOffset` 定位，
  容量 `MaxVertexCount × kFrameFlushSlots`，超限 assert（见 memory：multi-flush 覆盖坑）。
- clip 栈改动必须"先 flush 再改栈"；scissor 防御性 clamp 到窗口边界。

## Host（ya-gui-app-host）

- 生命周期：init → run（SDL event → WidgetTree dispatch → snapshot → compose → present）→
  shutdown。resize 只在帧边界重建 presentation 资源。
- 诊断：`--dump-snapshot=path --dump-frame=N`（CPU 侧 BMP 光栅化快照）、
  `--gpu-shot=path --gpu-shot-frame=N`（GPU readback BMP）。内置纹理 resolver
  （`builtin/white|black|multipixel|checkerboard`）供 image 控件在无资产系统时使用。
- **teardown 铁律**：任何持有 GPU 资源的成员（readback buffer、shader storage、widget tree、
  command buffers、presentation targets）必须在 `delete render`（VMA 销毁）前释放
  （见 memory：VMA teardown 顺序坑）。

## 编辑器内嵌

- `Editor/Panels/GUIWorkbenchPanel`：ImGui 窗口承载 workbench surface；`buildSnapshot` 每次
  同步 logical extent；事件经 ImGui 坐标换算后走同一 WidgetTree。
- `EditorModule` 的 `EditorToolSurfaceCompositor` 在离屏 target 合成 panel 快照；
  `onDetach` 必须 `shutdown()` 释放 composed image（早于 render teardown）。

## 构建 / 测试

```bash
make b t=GUIWorkbench && make r t=GUIWorkbench          # standalone demo
make r t=GUIWorkbench ARGS="--smoke-actions"            # 端到端自动化
make run t=HelloMaterial / make run-editor t=HelloMaterial
xmake b ya-gui-closure-test && xmake r ya-gui-closure-test
make test-gui                                            # closure + widgets + workspace
```
