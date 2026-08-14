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
- 输入：`dispatchEvent` 用显式 route：topmost candidate discovery 后执行 Preview
  (root -> parent) -> Target -> Bubble (parent -> root)。`Stop` 短路，`Pass` 继续 lower
  candidate；capture/focus/popup/modal/drag 都是 tree 级 route policy。`WidgetTree` 持有
  persistent pointer state、pointer path、focus path 和 route trace；`WidgetTreeDump`
  输出 `pointer`、`focusPath`、`lastRoute`（policy/path/phase/handled/result）。route callback
  可 detach 自身，executor 会持有 path 并重查 membership。drag&drop 会话
  （`beginDrag/updateDrag/endDrag/cancelDrag`，payload 为 string）由树管理，目标控件实现
  `canAcceptDrop/onDrop/setDropHighlight`。
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
- 布局正式分为 `UIElement / UILayout / UISlot`：`UIContainer` 只是第一个 layout host，
  持有 `UIBoxLayout`；它不再持有 `_direction/_spacing/_padding/...` 这类 box 字段。
  `UILayout` 只负责 measure/arrange，`UISlot` 是 parent-owned parent-child 边对象。
- `UIBoxSlot` 承载每 child 的 `Auto/Fill`、weight、margin、cross alignment、
  min/max/preferred size 与 layout participation；slot setter 会使所属 tree 的 layout 失效。
  Fill 按权重分配剩余主轴空间且遵守 max size；Hidden 默认保留空间，可由 slot 明确关闭。
- child 用 `getSlot()` 读取当前边，parent 用 `getSlotForChild()` / `UIContainer::getBoxSlot()`
  查询；reparent/detach 时旧 parent 销毁旧 slot，新 parent 创建默认 slot。不要缓存 slot
  裸指针跨越 reparent/detach。
- `UIBoxLayout` 主轴按 desired/slot 排列，cross 轴默认 stretch；`computeDesiredSize` 聚合
  child + margin + spacing + padding。scroll/split 仍读取内容 desired，specialized layout
  已收口为 `UIScrollLayout` / `UISplitLayout`；`UIButton` 使用
  `UISingleChildLayout`。specialized widget 只保留 paint/input transient state，不能再把
  ratio/offset/padding 等几何状态塞回 widget 字段。
- `UISplitLayout` 管 orientation/ratio/min extent/divider/padding + first-two-child arrange；
  `UIScrollLayout` 管 axis/offset/step/max offset + first-child arrange；scroll 到边界必须
  返回未处理，以便 route bubble 到外层。tree dump 的 `layout.type` 统一输出
  `box/singleChild/split/scroll`。
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

## GUI render surface

- `GUIRenderSurface`（`Runtime/Compose`）是 GUI compose target 的唯一资源边界：
  `createOffscreen()` 创建 Framework-owned `RenderImage`，`wrapExternal()` 包装
  imported swapchain image；二者都经同一 `prepare()` / `record()` 调用
  `Render2DComposePass`。
- surface 只拥有/保留目标 image、format 与最终 layout；**不** pump event、acquire
  swapchain image、present 或访问 live WidgetTree。window/present 仍属于 host，
  tree/snapshot 仍属于 WidgetTree。
- 最终 layout 是 surface 的不变量：offscreen 默认 `ShaderReadOnlyOptimal`，
  swapchain surface 为 `PresentSrcKHR`。调用方不能通过 compose desc 把二者留在
  错误 layout。
- 替换/销毁 surface 必须发生在 frame boundary，且旧 command buffer 的 submit 已完成；
  command recording 仅消费不可变 snapshot 与当前 surface。
- `RuntimeUIOffscreen` 是和 `RuntimeUIComposite` 分离的 compose kind / pass slot：
  可在同一 command buffer 中把**同一 snapshot**录制到 windowed 和 offscreen target，
  不复用 vertex/descriptor frame resources。`GUIAppHost` 的
  `--gpu-shot` + `--offscreen-shot` + `--offscreen-diff` 是零容差 parity 门禁。

## Host（ya-gui-app-host）

- 顶层命名：`GUIApp` 是 standalone GUI 的装配层（当前一个 primary
  `GUIWindowHost`）；`GUIWindowHost` 是一窗口一 tree / SDL window / presenter /
  pointer context 的真实 owner。`GUIAppHost` 仅为旧调用的 compatibility alias，新代码
  使用前两者。
- 生命周期：init → run（SDL event → WidgetTree dispatch → snapshot → compose → present）→
  shutdown。resize 只在帧边界重建 presentation 资源。
- `GUIHeadlessHost` 是同一 AppKernel/WidgetTree/delegate 合同的无窗口变体：只产生
  immutable `UIFrameSnapshot`（可由 callback 检查/落盘），不创建 SDL window、RHI、
  swapchain 或第二套 run loop。它用于 automation、结构断言与 windowed/offscreen
  交叉取证。
- 诊断：`--dump-snapshot=path --dump-frame=N`（CPU 侧 BMP 光栅化快照）、
  `--dump-snapshot-json=path --dump-frame=N`（snapshot 几何/clip/text JSON + digest）、
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

macOS / MoltenVK convergence gate (must be run on macOS, not emulated from a
Windows runner):

```bash
python3 Script/gui_convergence_macos_validation.py
```
