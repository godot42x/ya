# 游戏内 UI / 2D 渲染计划（Godot 风格统一 Scene Tree，已被取代）

> **状态：已被
> [`../ui-widget-tree-refactor/plan.md`](../ui-widget-tree-refactor/plan.md)
> 取代（2026-08-09）。**
>
> 本文件保留为历史决策记录，不再作为 Game UI 的执行依据。原路线把 Node2D
> 与 Node3D 放在统一 Scene Tree，并在渲染/输入阶段扫描 active Scene root；
> 新路线改为独立 WidgetTree、SceneWidgetEntry authoring recipe 和 immutable
> UIFrameSnapshot。真正的 world-space 2D Node 可在未来单独规划，不再与 Game UI
> 共用 Node2D 语义。
>
> 历史状态：2026-08-07 建立；当日两次修订（用户决策）：
> 1. UI 不接入 ECS（独立于组件系统）。
> 2. 采用 Godot 风格：**一棵统一 scene tree 混合 2D 与 3D 节点**，不做平行的
>    UI 树，也不做 UI 组件树。

## 1. 目标

像 Godot 一样，用**一棵场景树**同时承载 3D 世界实体与 2D/UI 节点：

```text
Scene::_rootNode（统一 node tree，唯一组织结构）
  ├── Node3D（世界实体，ECS-backed，现状不变）
  │     └── ...
  └── Node2D（2D/UI，纯树节点，entity-less，新增）
        ├── UICanvasNode（viewport 根）
        ├── UITextNode / UIButtonNode / UIPanelNode
        └── ...

渲染：一次树遍历分派
  Node3D -> 现有 RenderFrameExtractor / 世界 draw buckets（不变）
  Node2D -> UI 提取 -> 独立 screen-space UI pass（新增）

输入：Node2D 子树按 zOrder 命中测试，UI 命中优先吞事件
```

最终要求：

1. 单一 scene tree：Node3D（实体）与 Node2D（UI）混在同一棵树、同一 Hierarchy
   面板、同一序列化文档；游戏代码用 `createNode3D/createNode2D` 同构 API 构建。
2. Node2D = 纯节点：不引用 registry/entity/component，不新增 UI 相关 ECS 组件；
   `UIComponent`（ECS stub）保持不动，Phase 3 清理。
3. Node2D 渲染为 screen-space UI：独立 UI pass、支持裁剪（clip rect）、正确混合、
   不进 bloom（色彩策略见 §3.4）。
4. 输入路由：viewport 内 Node2D 命中优先消费事件，未命中才交给 gameplay。
5. UI 渲染后端与 ImGui 零依赖（现状已满足），为远期替换 ImGui 保留边界。

## 2. 非目标与停止线

- **UI 组件化进 ECS：明确排除**；Node2D 不引用 ECS 任何设施。
- **不做平行的 UI 树/UI 面板树**：2D 节点就在统一 scene tree 里（Godot 语义）。
- 替换或删除 ImGui（远期，仅在本计划结构层面预留边界）。
- 编辑器可视化 UI 画布/拖拽布局工具（Phase 2 起步树面板，画布后续再议）。
- 复杂文本 shaping、富文本、Emoji、RTL。
- 动画/过渡/数据绑定/脚本化 UI 事件（Phase 1 只做静态 UI + 按钮点击状态）。
- 世界空间 2D 精灵（Godot `Sprite2D` 语义，即"2D 游戏本体"）：v1 的 Node2D 只做
  screen-space UI；世界空间 2D 留待 Node2D 增加 world-space 模式后扩展
  （Phase 2+，见 §6）。
- 多窗口、多 viewport UI、DPI 缩放（v1 单窗口单 viewport，随 viewport 尺寸拉伸）。
- 跨 Deferred/Forward 提前抽象万能 UI pass；先按两图各自接线。

## 3. 当前代码事实（2026-08-07 调研）

### 3.1 统一树的基础已具备

- `Scene::_rootNode` 已是唯一 node tree：`Node`（纯层级：name/parent/children/
  entity）+ `Node3D : Node`（ECS 实体 + TransformComponent）。`createNode3D`
  与 `nodeTree` 序列化、PIE clone 均已成熟。
- 历史：entity-less 节点曾以 folder 形式短暂存在，因"无语义"被撤；本次 Node2D
  带真实渲染/命中语义，**复用当时验证过的所有权与序列化模式**（entity-less
  节点由 Scene 持有，nodeTree 条目无 entityRef 即非实体节点）。
- `Core/UI` 的 `UIElement` 树（children/parent + render/update/handleEvent +
  Canvas/Panel/Button/TextBlock + UIManager 单例）可演进为 Node2D 的渲染/事件
  实现载体；`UIRender.h` 是注释空壳。

### 3.2 渲染接线现状

- `RenderViewportOverlayRecorder::recordRenderViewportOverlayPass`（Deferred 与
  Forward 共用）：`Render2D::begin` → snapshot → `onRender` →
  `UIManager::render()` → `end`。UI 与 skybox/billboard/调试 sprite 同批次。
- Deferred 图序：GBuffer → SSAO → Light → ForwardOpaque → Skybox → Bloom →
  ForwardTransparent → EntityId → Overlay → Postprocess。
- Forward 图序：Opaque → Skybox → Transparent → EntityId → Overlay → Bloom →
  Postprocess。两图 overlay 相对 bloom 位置不一致（Forward 的 UI 会进 bloom）。
- `Render2D`：`FQuadRender`（16 槽纹理数组、screen ortho（top-left、Y 向下）、
  world 批次；`drawSubTexture` 可作 9-slice；`drawText` 走字体图集）；管线声明
  dynamic viewport/scissor，但**无裁剪栈 API**。
- `FontManager`（FreeType）：atlas + 动态字形 + measureText + "name:size" 缓存。
- `Render2D::init` 在 RenderRuntime 初始化；`tickLogic` 已调 `Render2D::onUpdate`。

### 3.3 树与序列化现状

- `nodeTree` 序列化：name + entityRef + children；反序列化按 entityRef 绑定
  实体。Node2D 需要：条目加类型判别 + 无 entityRef + 反射字段。
- Entity-less 节点所有权：folder 时期用过 `_folderNodes`（已撤）；Node2D 恢复
  同类所有权容器（更名 `_entityLessNodes` 或按语义 `_uiNodes`）。
- `UIComponent`（ECS）stub 被 DetailsView 手写名单引用；不扩展，Phase 3 清理。

### 3.4 已知不一致/坑

- **bloom**：UI 是否进 bloom 取决于管线（历史有 "billboard 不应进 blooming"
  修复）。**推荐 UI 不进 bloom**。
- **色彩空间**：overlay 目标线性 HDR → postprocess 输出 sRGB；UI 默认走 tonemap
  （v1），后续提供 "finalize 后直画 sRGB" 开关（Phase 1.4）。
- **坐标**：Render2D screen 空间 = top-left 原点、Y 向下（规则 #8）；
  `UIAppCtx.lastMousePos` 同坐标系；命中/绘制必须一致，并换算
  `viewportFrameBufferScale`（吸取 picking 坐标不一致教训）。
- **树选择**：Hierarchy 面板曾为 folder 实现过 entity-less 节点选择
  （`_selectedFolder` 模式），Node2D 需要同样的"节点级选择通道"
  （无实体 → 不进 gizmo/Details 实体流），复用之。

## 4. 关键决策

1. **统一 scene tree**：Node2D 直接挂进 `_rootNode`（可挂 root 或任意 Node3D/
   Node2D 之下）；不做平行 UI 树。Node3D 与 Node2D 可互为父子（Godot 语义：
   组织关系有效，变换按各自空间链独立计算，v1 不做跨类型变换传递）。
2. **Node2D = 纯节点**：`Node2D : Node`（2D 基类：相对父 position/size/
   visible/zOrder/锚点）+ 具体类型 `UICanvasNode/UITextNode/UIButtonNode/
   UIPanelNode`（反射字段）。entity-less；Scene 持有所有权；镜像
   `createNode3D` 提供 `createNode2D`。
3. **序列化**：`nodeTree` 条目扩展——有 entityRef = Node3D（现状）；无 entityRef
   的条目带 `nodeType`（"UICanvasNode" 等）+ 反射字段 + children。旧 scene 文件
   无 nodeType 条目按现状（实体节点）解析，向后兼容。PIE clone 的
   `cloneReferencedNodeTree` 增加 Node2D 分支（反射深拷贝，无实体）。
4. **渲染**：v1 复用 Render2D + 裁剪栈（`pushClipRect/popClipRect`，clip 变化
   flush 批次 + scissor）；独立 UI pass 放 bloom 后、postprocess 前；UI 渲染段
   数据来源 = 每帧遍历 active scene 树的 Node2D 子树（UIManager 或
   UISceneTreeExtractor）。
5. **输入**：`UIManager::onEvent` 返回"是否消费"；命中 Node2D 子树（zOrder、
   visible、hitTest）才吞事件；App fallback 未命中返回 false 给 gameplay。
6. **编辑器**：Hierarchy 天然显示混合树（Node2D 行无实体 → 节点级选择通道，
   复用 folder 时期模式）；Inspector 反射编辑 Node2D 字段；DetailsView 实体
   流不受影响。

## 5. Phase 1 —— 最小闭环（统一树 + 游戏内 UI 渲染）

### 5.1 UI pass 独立 + 裁剪（渲染地基）

- `Render2D`/`FQuadRender` 增加 `pushClipRect/popClipRect` 裁剪栈（裁剪变化
  flush 批次 + 命令级 scissor）。
- Deferred/Forward 各在 overlay 之后、postprocess 之前加 UI 渲染段（v1 复用
  overlayInput 目标；实现时验证 bloom 输入依赖，必要时调整 bloom 输入引用，
  保证 UI 不进 bloom）；`UIManager::render()` 从 overlay recorder 移入。
- 验收：HelloMaterial 显示 canvas 文本 + 图片按钮；UI 无 bloom 泛光；无残留。

### 5.2 Node2D 节点（统一树的数据源）

- `Node2D : Node`：`_position/_size/_visible/_zOrder`（相对父 Node2D 链）、
  `getScreenTransform()`（累加父链，v1 无旋转/缩放/锚点）、`hitTest(point)`。
- 具体类型（反射字段，可序列化）：
  - `UICanvasNode`：默认填满 viewport（UI 根）
  - `UIPanelNode`：背景色/图片 + 9-slice 开关
  - `UITextNode`：text/fontSize/color/alignment/自动尺寸（measureText）
  - `UIButtonNode`：面板样式 + 状态 + 点击回调（v1 日志/脚本钩子留口）
- `Scene`：
  - `_entityLessNodes`（entity-less 节点所有权，替代 folder 时期的
    `_folderNodes`）；`clear()` 一并清理。
  - `createNode2D<T>(name, parent)`（镜像 createNode3D）；`Node::is2D()/
    isEntityBacked()` 辅助。
- 树遍历渲染：UIManager 每帧收集 active scene 树中 Node2D（DFS、按 zOrder
  分组）→ Render2D 批次；`UIManager::update(dt)` 接入 tickLogic（应用层）。
- `Node3D` 侧渲染/提取完全不动。

### 5.3 序列化 + PIE clone

- `SceneSerializer::serializeNodeTree`：Node2D 条目 = name + `nodeType` +
  反射字段 + children（无 entityRef）；Node3D 条目不变。
- `deserializeNodeTree`：无 entityRef 且有 nodeType → 按类型创建 Node2D +
  反射填字段；旧文件兼容（无 nodeType → 现状）。
- `cloneReferencedNodeTree`：Node2D 分支 = createNode2D + 反射序列化 roundtrip
  深拷贝；Node3D 分支不变。
- 测试：uiTree 混合树 roundtrip（Node3D + Node2D 混合父子）、PIE clone。

### 5.4 命中测试 + 事件语义

- `UIManager::onEvent` 返回 bool；命中 Node2D 子树（zOrder 排序、visible、
  hitTest）才消费；App fallback 未命中返回 false。
- `UIButtonNode` 事件走 hitTest + `UIAppCtx` 坐标缩放（viewportFrameBufferScale）。

### 5.5 编辑器（v1 最小）

- **视口模式（用户决策 2026-08-07，Godot 风格）**：编辑器视口加 `3D / 2D /
  Mixed` 切换——`3D`（默认）只渲染 Node3D 世界、跳过 UI pass；`2D` 画布视图只
  渲染 Node2D（轻量 canvas-only 渲染路径：清屏 + UI pass，不跑 GBuffer/Light）；
  `Mixed` 仅供 PIE/runtime 使用（2D over 3D 真混合）。运行时始终 Mixed。
- Hierarchy：Node2D 行显示（图标/文本区分 2D），节点级选择通道（复用 folder
  时期 `_selectedNode` 模式；单击折叠交互保留）；选中 Node2D 节点自动切 2D 视图。
- Inspector：Node2D 选中时反射绘制字段（新增轻量节点检查器，不动实体流）。
- 创建菜单：SceneHierarchy 空白菜单加 "2D" 子菜单（Canvas/Text/Button/Panel）。

## 6. Phase 2 —— 布局、文本与 2D 世界空间

- anchor/pivot、百分比尺寸、HBox/VBox/Stack、文本驱动尺寸。
- 9-slice 封装、字形缓存上限/清理、字体 fallback、焦点/键盘导航。
- Node2D 增加 world-space 模式（Godot Sprite2D 语义）→ 2D 游戏实体混入同一棵树
  （渲染走世界 buckets 或独立 2D pass）。
- 编辑器 UI 树面板完善（层级/属性/运行时预览）。

## 7. Phase 3 —— 独立 UI 后端（替换 ImGui 的结构基础）

- 把 `UIRender.h` 做实：独立 UI 顶点批 + 纹理图集 + 裁剪 + 批次状态机，
  Node2D 渲染从 Render2D 切到 UIRender；Render2D 与 UI 解耦。
- 清理 `UIComponent`（ECS stub）与 DetailsView 引用。
- 试点：把某一 editor 面板从 ImGui 迁到游戏 UI（远期，不承诺时间）。

## 8. 验证

### 构建与冒烟

```bash
xmake b ya-editor
xmake b ya-testing
python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=120 --log-level=warn --log-detail-level=error
```

### 自动测试（无 GPU 优先）

- 混合树序列化 roundtrip：Node3D + Node2D 混合父子、各 Node2D 类型字段、
  旧文件兼容（无 nodeType 条目按实体节点）。
- PIE clone 深拷贝一致性（Node2D 字段 + 树结构）。
- 命中测试单测：zOrder 重叠、visible=false 不命中、裁剪外不命中。
- 裁剪栈单测：push/pop 行为与批次 flush 计数。

### 手动验收（HelloMaterial）

- 一棵树里同时存在 Cube（Node3D）与 Canvas/Text/Button（Node2D），
  Hierarchy 正常显示与折叠。
- 点击按钮状态切换（Normal->Pressed->Hovered），日志可见回调；点击 UI 不穿透
  到世界（不误选背景实体）。
- 保存/加载 scene、进出 PIE 后混合树一致。
- UI 不产生 bloom 泛光；移除 UI 根后画面无残留。

## 9. 风险与待验证点

- graph 依赖：bloom 输入是否引用 overlayInput（决定 UI 渲染段位置），实现时先
  验证依赖再定。
- entity-less 节点所有权回归：`_entityLessNodes` 与 `_nodeMap` 的生命周期边界
  （folder 时期已踩过一遍，本次按既定模式恢复并补测试）。
- Hierarchy 选择通道：Node2D 无实体 → gizmo/Details 实体流不受污染；单节点
  选择与多选实体的交互规则要定（Node2D 选择清空实体多选，反之亦然）。
- `dispatchInputFallbackEvent` 恒消费行为改动影响 "Drawing 模式点击标记"，
  需回归。
- Render2D 16 槽纹理数组按帧重建：多纹理 UI 需图集/扩容，Phase 2 处理。
- editor 合成 pass（EditorModule compose）叠加第二层 Render2D：编辑器里游戏
  UI 与编辑器 overlay 需确认无重复绘制。
- 跨类型父子（Node3D 挂 Node2D 或反之）：v1 只做组织，变换不传递，需在文档与
  测试中固化该语义，避免后续误以为继承变换。

## 10. 相关文件索引

- `Engine/Source/Scene/Node.h`（新增 `Node2D` 系列）
- `Engine/Source/Scene/Scene.{h,cpp}`（`createNode2D` + `_entityLessNodes`）
- `Engine/Source/Core/Serialization/SceneSerializer.{h,cpp}`（nodeType + 反射
  字段 + clone 分支）
- `Engine/Source/Core/UI/`（Node2D 渲染/事件载体，演进为节点渲染实现）
- `Engine/Source/Render/2D/Render2D.{h,cpp}`（裁剪栈改造点）
- `Engine/Source/Render/UIRender.h`（Phase 3 空壳）
- `Engine/Source/Runtime/Rendering/Common/RenderViewportOverlayRecorder.cpp`
  （`UIManager::render` 移出点）
- `Engine/Source/Runtime/Rendering/Deferred/DeferredFrameGraphPasses.cpp`
- `Engine/Source/Runtime/Rendering/Forward/ForwardFrameGraphPasses.cpp`
- `Engine/Source/Runtime/Application/Lifecycle/AppFrameLoop.cpp`（tickLogic +
  UI update 接线）
- `Engine/Source/Runtime/Application/Lifecycle/AppEventRouter.cpp` +
  `Engine/Source/Runtime/Application/App.cpp`（dispatchInputFallbackEvent）
- `Engine/Source/Editor/Panels/SceneHierarchyPanel.*`（Node2D 行 + 节点选择通道）
- `Engine/Source/ECS/Component/2D/UIComponent.h`（stub，保持不动，Phase 3 清理）
