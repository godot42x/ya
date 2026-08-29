# GUI layout unified 进度

## 2026-08-29 — 已提交 4 个 checkpoint

### ✅ CP1 冻结新布局协议 — `[gui/layout] add UIConstraints / EWidgetSizeMode / UILayoutIntent protocol`

- 新增 `Runtime/Layout/UILayoutIntent.h`：`EWidgetSizeMode`（Fixed/Auto）、`UIConstraints`（measure 可用空间 min/max）、`UILayoutIntent`（合并 slot+path 的父→子 intent）。
- 仅类型定义，未接调用点。

### ✅ CP3 运行时：Canvas 成为一等 layout host — `[gui/layout] remove child-authored stretch anchors; migrate to canvas slot` + `[gui/layout] make Canvas a first-class layout host`

- 删除 `fillParent/setAnchors/fillWidth/fillHeight`（8 个重载），DSL 层面子节点不再能自写拉伸锚点。
- `UICanvasSlot` + `FCanvasSlotArgs`（原 Panel 绑定命名已改），`UISlot::as<T>()`。
- 新增 `UICanvasLayout`（createSlot / measure / arrange）。
- `UIPanel` 成为 Canvas layout host（layout / layoutAssigned / computeDesiredSize / createSlotForChild），镜像 UIContainer 模式。
- `UIElement::resolveCanvasRect()` 成为单一锚点+尺寸解析契约，`computeAnchorRect` 委托它，两条路径不会漂移。
- 基础 `UILayout::createSlot` 还原为 base slot：canvas 边由安装该 layout 的 host 提供。
- **有意偏离**：canvas slot 默认 `{0,0}-{0,0}`（历史绝对行为），`.fill()` 才拉伸——否则既往未授权锚点的 panel 子节点会被静默改成 fill。

### ✅ CP3 public DSL：`ui::layout()` + `parent[spec >> widget]` — `[gui/declarative] add unified ui::layout() spec and parent[spec >> widget] DSL` + `[gui/declarative] migrate all call sites to parent[ui::layout().fill() >> widget]`

- 新增 `LayoutSpec.h`：`FUILayoutSpec`（fill/grow/anchor/cell/align/margin/offsets/size/sizeMode capability + 能力掩码）与共享宿主解析 `applyLayoutSpecToSlot()`（canvas / box / table）。
- `ui::layout()` 构造器、`operator>>` 绑定（支持 builder 与 shared_ptr 两种 child 形态）、parent `operator[]` 收集。
- `ui::build/buildAs` 增加 `FUILayoutSpec` 重载；新增 `ui::attachLayout()`。
- 调用点全量迁移：EditorSurface / EditorInspectorTab / WorkbenchSurface / WorkbenchDemoPages / Menu.cpp。
- 测试：两个 `fillWidth()` 陷阱测试改写为边意图语义；契约测试改为断言 slot；late-child 放置改到边上表达。

### 验证（本轮）

- 构建：`ya-gui-framework`、`ya-game-editor`、`GUIWorkbench` 全通过。
- 测试：`ya-gui-closure` 260、`ya-gui-widgets` 223、`ya-gui-headless-host` 2、`ya-gui-workbench-workspace` 8 —— 全通过。

### ✅ CP5 部分：`UIElement` layout-host 钩子 + panel 子节点 / designer 回归修复

- `UIElement::installLayout()/getLayout()`：任何元素都可安装 layout；`layout()`/`layoutAssigned()`/`computeDesiredSize()` 有 layout 时走它，否则保持 legacy 自定位。**这消除了运行期的 path-A / path-B 二分**："是否有 layout" 是唯一区别，Canvas 只是 Box/Split/Table 之外的又一个 layout。
- `UIPanel` 改为通过共享钩子安装 `UICanvasLayout`，不再私有持有成员 + 重写三个布局入口。
- **发现并修复回归**：`UIPanel` 成为 canvas host 后，子节点自写锚点被静默忽略。已迁移全部此类内部点：
  - `WorkbenchSurface`：menuBar / workspaceSplit / pageRailTitle / pageRailCard / demoHost / statusText / commandResultText（父为 root / featureRail / contentFrame 等 panel）
  - `DockSpace`：dock leaf content 填充 body panel
  - `Dialog`：dialog stack 填充 dialog panel
  - `WidgetTree`：tooltip label（fill+inset）与 drag ghost label（fill）
  - `UIDesignerPanel`：拖拽读/写锚点改走 canvas slot（否则设计器对 panel 子节点的拖拽结果全丢失）
- 父节点不是 layout host 的位置（tree root / layers / split pane）保持 legacy 自定位路径不变。

### ✅ CP3 完成：Canvas slot 能力补全 — `[gui/layout] complete Canvas slot: four-edge offsets, size mode and alignment`

- `UICanvasSlot` / `FCanvasSlotArgs`：新增四边 `FMargin` offsets（单边设置即令该轴拉伸，"四边"= fill 减 insets）、alignment H/V、per-axis size mode（Fixed/Auto），各自触发 invalidateMeasure 或 invalidateArrange。
- `UICanvasLayout::resolveChildRect()`：尺寸按轴解析（Auto→测量、拉伸→anchor span 减 insets、否则保留作者化尺寸），再在可用区域内对齐。**非拉伸轴保留父区域**，否则固定尺寸子节点没有可对齐的空间。
- `EWidgetSizeMode` 下沉到 `UILayout.h`（`UILayoutIntent.h` 依赖 `UILayout.h`，方向必须如此），slot 复用同一枚举。
- `ui::layout()` 暴露 `offsets(l,t,r,b)` / `widthSizeMode()` / `heightSizeMode()`。
- 新增 3 个测试（四边 insets → fill 减 insets；Auto 宽度 + 拉伸高度；对齐放置固定尺寸子节点）。

### ✅ CP3/§3.4 完成：capability 编译期隔离 — `[gui/declarative] reject unsupported layout intent at compile time`

**这是本计划的核心保证**：布局意图现在携带在**类型**里，宿主无法兑现的意图在编译期被拒绝，而不是静默丢弃。

- `LayoutSpec.h`：每类宿主的能力集合（box / canvas / grid / single-child / split / overlay）、`LayoutCapsCompatible` concept、`allowedLayoutCaps<T>()`（未迁移的 builder 默认宽松）。
- `SlotBuilders.h`：`FUILayoutSpecBuilder<Caps>` —— 每个 modifier 为 `&&` 限定并返回"能力加宽"后的 builder 类型，因此类型携带全部已应用能力的并集。
- `BuilderBase.h`：`TUILayoutAttachment<Caps, TChild>` + 宿主约束的 `operator[]`。
- `LayoutBuilders.h`：各宿主声明 `kAllowedLayoutCaps`。
- box 宿主现在真正应用 `size()`（此前是静默 no-op），保证没有能力被空转。
- **抓出并修复一个潜在误用**：`WorkbenchDemoPages` 的拖拽区挂在 column（box 宿主）上却指定 `anchor(...)` —— box 宿主从不支持 anchor，该意图此前就是死代码（静默失效）。已改为 `size()`（box 宿主确实实现）。这正是新检查要拦截的情形。
- 测试：覆盖每对 accept/reject、builder 类型的能力并集、以及两个真实宿主类型的 `static_assert`。

### ✅ CP3 收尾：pivot / preferred size / `ui::canvas()` 宿主 — `[gui/layout] finish Canvas: pivot, preferred size and a ui::canvas() host`

- `UICanvasSlot` / `FCanvasSlotArgs`：新增 `pivot`（子节点的哪个点落在解析出的位置上，归一化子空间）与 `preferredSize`（Auto 轴请求的尺寸；零分量回退到测量值）。
- `resolveChildRect()`：Auto 轴优先用 preferredSize；最终位置再减去 `pivot * size`，因此**无需事先知道子节点尺寸**即可让它居中/右/下对齐。
- `Pivot` 成为新的 capability，仅 canvas 宿主接受。
- `ui::layout()` 新增 `pivot()` / `preferredSize()`。
- **`ui::canvas()`**：不绑定 Panel 视觉的 canvas 宿主（无背景/圆角，`styleKey="canvas"`）。**Canvas 现在是一个独立的 layout 类型**，不再只是 Panel 的行为。
- 顺带修正仍引用已删除 `ui::panelSlot()` 的注释。
- 新增 3 个测试（pivot 居中、preferredSize 驱动 Auto 轴、canvas 宿主几何）。

**至此 CP3 全部完成。**

### ✅ CP6 部分：序列化侧旧锚点清理 — `[gui/serialize] drop the last authored-anchor reader from serialization`

- 核查范围：schema / 文档 / dump / 脚本绑定。结论：**`serializeFields()` 早已不再输出锚点**（反射中已移除），唯一残留的 JSON 锚点输出是新 slot 的 `appendRuntimeDiagnostics`（属于新 schema，保留）。
- `SceneWidgetEntry`：拖放时"双方均为 point-anchored"才做父相对位置校正的判定 —— 锚点不可作者化后该条件**恒真**（`instantiate()` 后锚点恒为运行时默认 `{0,0}`）。移除死条件，行为不变。
- 删除过期保存文档 `Engine/Saved/GUIWorkbench/smoke.yaui`（含 2 处旧 `_anchorMin`，无任何引用；plan 明确旧文档不做兼容读取，下次运行会重新生成）。
- `_bAutoSize` 仍广泛使用（SizeToContent 机制，仍是合法运行时状态），**保留**。

### ⚠️ 发现既有故障（非本次改动引入，需用户处理）

`ya-testing` 中 5 个 `GameUIHostTest` 失败，全部同一根因：

```
C++ exception: [json.exception.type_error.307] cannot use erase() with null
  - ActivateMountsAutoMountEntriesByZOrder
  - BuildSnapshotComposesMountedWidgets
  - ControllerReplacementPerformsHandover
  - PieRestartDoesNotAccumulateWidgets
  - SceneSwitchUnmountsPreviousAndMountsNext
```

根因：`UIDocument::instantiate()` 无条件调用 `root->deserializeFields(fields)`，而 `UIDocument::fields` 默认构造为 **null**；`UIPanel::deserializeFields` 里的 `rest.erase("_bExplicitFill")` 对 null json 会抛 `type_error.307`。

**证据表明这是既有问题，非本次改动引入**：
1. `UIPanel::deserializeFields` 的未加保护 `erase` 与我动手前 `fd631393^` **逐字节相同**（`git show` 验证）。
2. `UIDocument::instantiate` 同样未变。
3. `GameUIHost*`、`GameUIHostTest.cpp` 均不在我改动的 31 个文件内。
4. 抛出点代码我从未触碰。

最小修复是给 `UIPanel::deserializeFields` 加 null 保护（`if (!rest.is_object()) rest = nlohmann::json::object();`），或在 `UIDocument::instantiate` 侧跳过空 fields。**因属计划外，未擅自修改**，等你决定。

## 未完成（相对当前 plan 的差距）

- **CP2 未做**：`_anchorMin/_anchorMax/_position/_size/_minSize/_maxSize/_bAutoSize`、`setPosition/setSize/getPosition/getSize`、`computeAnchorRect`、`reportStretchAnchorsIgnored` 仍在 UIElement 上。
  - 现状：anchor 已从反射移除并降为 runtime-only；几何字段仍是 legacy fallback 的运行时 I/O，大量非 DSL 代码（DockSpace / Dialog / WidgetTree / 测试）仍直写它们。
- **CP3 已完成**：`UICanvasSlot` 已具备 anchor / offsets / alignment / size mode / pivot / preferred size / min-max；`ui::canvas()` 宿主 DSL 已建。
- **§3.4 已做**：capability 编译期隔离。plan 要求的 `column[anchor(...) >> w]` 现在确实编译失败。
- **CP4 未做**：Box/Overlay/SingleChild/Split/Scroll/Grid 尚未全部统一到 typed slot arrange；reparent/detach 的 slot 重建未处理。
- **CP5 未完**：tree root / layers 仍是 legacy 自定位（`makeFillElement` 直写 `_anchorMin`）；DockSpace 的 `_previewOverlay`（挂 DragIme layer）未迁移；Popup / 测试中的直写未迁移。
  - 注意：把 layers 改成 canvas host 会影响 217 处 `attachToLayer`（多为测试），需整体迁移，风险高，尚未做。
- **CP6 未做**：旧 JSON 字段 / schema / UIDocument / Designer inspector / 快照 dump 未清理。
- **CP7 未完**：编译期断言、几何测试（四边 offsets / alignment / min-max / reparent）、snapshot parity 未补。

## 仍存在的过渡物（plan 明确要求删除，尚未删）

- `ui::panelSlot()` / `FCanvasPanelSlotBuilder`：已无调用点，但代码仍在（删除时用户中断，需再确认）。
- 过渡别名 `FCanvasPanelSlotArgs` / `UICanvasPanelSlot`。
- `child(node)` 默认重载（plan：只能是新布局系统的 default slot，不是 legacy 兼容）。
