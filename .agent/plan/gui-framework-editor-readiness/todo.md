# Todo

## Phase 0

- [x] 更新过时的 `gui-framework` skill 描述。
- [x] 建立 GUI 能力覆盖矩阵。
- [x] 为每个高风险缺口列出当前测试和缺失测试。
- [ ] 确认 Phase 1 的 cache identity 方案。

## Phase 1：生命周期与缓存

- [x] 稳定 widget runtime identity / cache generation。
- [x] detach/destroy 清理 paint cache。
- [x] 完整 draw-item equality/debug hash。
- [x] stale cache、reparent、cross-tree 测试。
- [x] 对象重分配、popup/drag teardown 和 snapshot build mutation policy。

## Phase 2：Invalidate / Layout

- [x] 冻结 dirty taxonomy。
- [x] child desired-size propagation（UIElement measure dirty 向祖先传播）。
- [x] dirty subtree（assigned rect proof 下的局部 layout skip）。
- [ ] measure/arrange cache。
- [x] layout proof generation / attach-reparent invalidation boundary。
- [x] layout performance counters（tree-level scope 累计统计 + skipped widgets）。

## Phase 3：Reactive

- [x] UI-thread 与 reentrancy contract（foreign-thread reject + reentrant notify defer）。
- [x] transaction/batch（同步嵌套事务、pending 去重、依赖快照）。
- [x] keyed ReactiveList identity/diff baseline（revision + replaceKeyed + TreeView state prune）。
- [x] keyed ReactiveList mutation/update contract（insert/update/move、边界拒绝、clear/replace diff）。
- [x] Computed dependency graph（lazy cache + upstream dirty propagation + cycle diagnostics）。
- [x] detach/unbind safety（upstream destroy -> downstream computed unlink）。

## Phase 4：DSL

- [x] fragment/group/helper。
- [x] conditional construction helpers (when / unless / ifElse); runtime switcher/repeater remains pending。
- [x] duplicate key diagnostics。
- [x] compile-time rejection tests for canvas/box slots; broader layout examples remain pending。
- [x] keyed child reconciler + Content Browser / Scene Save list consumers。
- [x] Content Browser entry visible-range window + spacer scroll extent; TableGrid/TreeView virtualization remains pending。

## Phase 5：Style / Theme

- [x] style field impact metadata。
- [x] style key/type catalog。
- [x] resource-ready invalidation。
- [x] visual state matrix。
- [x] panel naked-color cleanup（paint 只读 fillColor；`_color` 仅 no-theme fallback）。
- [x] 无主题 / 资源缺失 snapshot 回归（headless host 同契约；windowed GPU/offscreen 仍属 Phase 9）。

## Phase 6：Editor data

- [x] SelectionModel（identity 单选/多选/primary/hover/active/focus；不持有 ECS 指针）。
- [x] Command/Action routing（ActionMap；菜单/快捷键/toolbar 共用 execute）。
- [x] Undo/Redo transaction（UndoStack；拖动 merge；Inspector 属性/重命名接入 ActionMap）。
- [x] Property projection（`PropertyGraph::project`；Transform setter 进 projection；Inspector 按 component 物化 AutoPropertySection）。
- [x] multi-selection mixed value（交集 component、DragFloat "—"、批量写回、按 instance undo）。
- [x] viewport 选择写入 SelectionModel（`EditorLayer::selectionGeneration` + `syncSelectionFromLayer`；`SelectionModel::replace`）。
- [x] validation / missing resource error state（`PropertyHandle::validationError` + manipulate spec；`UIDragFloat`/`UITextField`/`UIImage` error fill；viewport `setResourceMissing`）。

## Phase 7-9：Editor migration and release

- [x] Inspector retained controls（bool/float/vec3/string/enum/color/asset-ref via `EditorAutoPropertySection` + `PropertyHandle`）。
- [x] Content Browser retained controls（EditorSurface mount/entry lists、search、selection、navigate、visible-window；ImGui panel 仍平行）。
- [x] Hierarchy virtualization（UITreeView scroll-window paint + EditorSurface HierarchyScroll）。
- [x] Hierarchy filter + entity drag-drop reorder（`HierarchyFilter` + `bindFilter`；`setReorderable` + `EditorHierarchyOps`）。
- [x] Viewport overlay contract（`EditorViewportHost` + `EditorSurface` host sync/dispatch）。
- [x] Viewport/gizmo ImGuizmo bridge。
- [x] Gizmo transform undo session（稳定 UUID 快照 + UndoStack）。
- [x] Bounded widgettree editor smoke gate（automation control + frame progression + presentation screenshot + clean quit）。
- [x] Asset Inspector retained tab（路径状态 + 预览区域，移除 pending placeholder）。
- [x] UI Designer retained shell（新建/保存/关闭文档 + 状态同步，移除 pending placeholder）。
- [x] Dock panel content 使用 parent-owned box slot fill，消除 path-A 下 child anchor 冲突。
- [x] Runtime Tools retained shell（Play/Simulate/Stop + status/frame 同步，移除 pending placeholder）。
- [x] Runtime Tools retained diagnostics summary（RenderDoc 状态/路径/capture 状态同步）。
- [x] Runtime Tools retained diagnostics controls（capture enabled/HUD/next-frame/after-120）。
- [x] Runtime Tools retained render settings（pipeline、viewport scale、VSync、present mode、pipeline reload）。
- [x] Runtime Tools retained profiling summary（compile/session 状态、CPU/GPU frame metrics、profiling toggles、average window）。
- [x] Runtime Tools retained render graph summary（pipeline + pass/dependency 状态）。
- [x] Runtime Tools retained render target summary（target/owner/extent/read-only 状态）。
- [x] Runtime Tools retained debug primitives（开关与 pending/frame/immediate 计数）。
- [x] Phase 8A：移除 ImGui Content Browser panel（retained EditorSurface 为唯一路径）。
- [x] Phase 8B：移除 ImGui Scene Hierarchy panel render（retained EditorSurface Hierarchy 为唯一 UI）。
- [x] Phase 8C：移除 ImGui DetailsView panel render（retained EditorInspectorTab 为唯一 Inspector UI）。
- [x] Phase 8D：移除 ImGui Frame Stats panel render（retained EditorSurface Frame Stats 为唯一路径）。
- [x] Phase 8E：移除 ImGui Asset Inspector panel render（retained EditorSurface Asset Inspector tab 为唯一路径）。
- [x] Phase 8F：移除 ImGui Runtime Tools panel render（retained EditorSurface Runtime Tools tab 为唯一路径）。
- [x] Phase 8G：移除 ImGui UI Designer panel render（retained EditorSurface UI Designer tab 为唯一路径）。
- [x] Phase 8H：移除 ImGui GUI Workbench panel render/compositor（retained EditorSurface Workbench host 为唯一路径）。
- [x] Phase 8I：默认 editor chrome 切到 widgettree（`editor.chrome.host` / CLI 默认）。
- [x] Phase 8J：移除 ImGui Render Graph debug 窗口（retained RuntimeRenderGraphSection 为唯一路径）。
- [x] Phase 8K：移除 ImGui demo window（legacy imgui chrome 仅保留 FilePicker modal）。
- [x] Phase 8L：widgettree Save Scene As 走 retained EditorSurface 对话框（不再经 ImGui FilePicker scene-save 模式）。
- [x] Phase 8M：widgettree Inspector asset Browse 走 retained EditorSurface asset-picker popup（不再经 ImGui FilePicker texture/model 模式）。
- [x] Phase 8N：删除未调用的 ImGui DetailsView 实现与 AssetInspectorPanel ImGui render 死代码。
- [x] Phase 8O：删除 SceneHierarchyPanel ImGui sceneTree 与 RuntimeTools/RenderTarget ImGui 死代码。
- [x] Phase 8P：删除 UIDesignerPanel 未调用的 ImGui draw helpers（保留 preview/document 数据层）。
- [x] Phase 8Q：EditorSurface dock layout persistence（stable panel key + `editor.dockLayout` JSON）。
- [x] Phase 8R：widgettree viewport 创作菜单（`EditorLayer` 命令 + retained `UIMenu` 右键 + Delete/Duplicate 快捷键）。
- [x] Phase 8S：通用 retained file picker（`FEditorFilePickerRequest` + `EditorSurface::openFilePickerDialog`；script/material/directory/scene 工厂）。
- [x] Phase 8U：Editor Settings retained 面板（View 菜单 + sampler/overlay/startup scene）。
- [x] Phase 8T：UI Designer retained palette + PropertyGraph inspector（`addPaletteWidget` + `EditorAutoPropertySection`）。
- [x] Phase 8V：Debug images retained dock tab（`EditorDebugImagesTab` + `EditorLayer` catalog/mask/group API；ImGui `debugWindow` 仍保留至 8W）。
- [x] Multi-window/docking persistence（floating geometry：`UIDockWorkspace` layout JSON + editor tear-off host）。
- [x] Phase 8W：删除 `onImGuiRender` chrome shell；WidgetTree 为唯一 editor chrome（`imgui-local` 仍服务 ImGuizmo）。
- [x] Phase 9B：editor-scale 性能基线（Hierarchy 视口 paint 窗口、Content keyed window、Inspector 干净 snapshot）。
- [x] Phase 9C：长时间运行 attach/detach、theme switch 压测。
- [x] Phase 9D：DPI、CJK fallback、键盘、IME、剪贴板、文本编辑。
- [x] Phase 9E：snapshot digest、GPU/offscreen parity、automation route trace。
- [x] Phase 9F：editor release checklist（macOS/Clang/Vulkan 证据入表；Windows/MSVC 与 OpenGL 为 blocker）。
- [x] Phase 10A：compact dock chrome（矮 tab strip、左上角隐藏 title bar、dock content 不再 inset）。
- [ ] Phase 10B：Inspector 补齐 ImGui DetailsView 类型覆盖（剩余：容器/custom renderer）。
  - [x] `PropertyGraph` 递归展开 nested/composite property（dot-path leaf nodes）。
  - [x] retained inspector 补齐 `vec2` / `vec4` / `int` / `int32_t` / `uint32_t` 的 mixed/edit/undo/test。
  - [x] 提炼 reflection-layer `PropertyAccessor` / path-walk / copy-compare-restore，收瘦 `PropertyHandle` 为 editor adapter。
  - [ ] retained inspector 容器与 custom renderer parity。
- [x] Phase 10C：自研 viewport gizmo（WidgetTree/Render2D），移除 editor 帧内 GuiSystem/ImGuizmo。
- [ ] Windows/MSVC 组合回归（本机未跑；未通过则不得宣称 retained editor ready）。
- [ ] OpenGL GUI/editor presentation（`GUIAppHost` 现为 Vulkan-only）。
