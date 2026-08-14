# GUI 能力盘点与差距分析（对照 Slate / Qt / Flutter / Godot）

> 建立日期：2026-08-15
> 作用：盘点当前引擎 `GUI/Runtime` 的实际能力，对照 UE Slate / Qt / Flutter / Godot 的基础容器/控件/样式/数据/输入，蒸馏出「当前最需要」的功能清单。这是「GUI 功能可用性最小闭环」的调研前置。

## 1. 当前能力（代码盘点，非记忆）

### 1.1 布局（`GUI/Runtime/Layout/UILayout.h`）

| 布局 | 能力 |
|---|---|
| `UISlot`（基类 edge） | parent-owned child edge；reparent 时销毁重建 |
| `UIBoxSlot` | sizeRule(Auto/Fill)、weight、margin、crossAlignment(Stretch/Start/Center/End)、min/max/preferred size、participatesInLayout、reserveSpaceWhenHidden |
| `UIBoxLayout` | H/V direction、spacing、padding、mainAxisAlignment(Start/Center/End)、clipChildren、stretchLastChild |
| `UISingleChildLayout` | 单 child + padding |
| `UISplitLayout` | 两栏 split、orientation、splitRatio、minFirst/SecondExtent、dividerThickness |
| `UIScrollLayout` | 单轴 scroll、axis(V/H)、scrollOffset、scrollStep |

### 1.2 控件（`GUI/Runtime/Widgets/Controls/`，16 种）

Button、CheckBox、ComboBox、Container、Image、Menu、MenuBar、Panel、PopupOverlay、ScrollViewport、SelectableRow、Slider、SplitPane、TabBar、Text、TextField

### 1.3 事件与输入（`UIElement.h` + `Core/Event.h`）

- 三阶段路由：`previewInputEvent`(tunnel) → `handleInputEvent`(target) → `bubbleInputEvent`(bubble)，`EWidgetEventRoutePhase`
- pointer：Moved / ButtonPressed / ButtonReleased / **MouseScrolled**；pointer capture(`bViaCapture`)
- keyboard：KeyPressed / KeyReleased / KeyTyped
- window：Resize / Focus / FocusLost / Minimize / Restore / Moved / Close
- hover：`isHoverable` / `isHoverTransparent` / `onPointerEnter` / `onPointerLeave`
- focus：`setFocus(widget, bFromKeyboard)` / `getFocusPath()` / Tab 遍历(`EWidgetFocusPolicy::Focusable`)
- drag & drop：`canAcceptDrop` / `onDrop`（payload 为 string）
- cursor：`getCursor()`（Arrow / ResizeEastWest / ResizeNorthSouth）

### 1.4 样式与数据

- **无样式系统**：`grep Style|Theme|Brush|StyleSet` 在 `GUI/` 下零命中。样式属性（字体/颜色/背景）散落在各控件类里，无数据驱动样式，无「布局与样式分离」。
- **无数据绑定**：retain 模式，靠手动 set + 重绘。无 Slate `TAttribute` / lambda 绑定、无 Flutter 声明式重建。
- 有反射序列化：`UIElement::serializeFields/deserializeFields` + `UIDocument`（.yaui authoring），但这是 authoring 快照，不是运行时数据驱动。

## 2. 对照外部框架的差距

### 2.1 布局容器对照

| 能力 | Slate | Qt | Flutter | Godot | 当前引擎 |
|---|---|---|---|---|---|
| Box(H/V) | SHorizontalBox/SVerticalBox | QHBoxLayout/QVBoxLayout | Row/Column | HBoxContainer/VBoxContainer | ✅ UIBoxLayout |
| SingleChild | SBox/SBorder | （layout 组合） | SizedBox/Padding | MarginContainer | ✅ UISingleChildLayout |
| Split | SSplitter | QSplitter | — | SplitContainer | ✅ UISplitLayout |
| Scroll | SScrollBox | QScrollArea | ListView/ScrollView | ScrollContainer | ⚠️ UIScrollLayout（单轴，缺双向/惯性） |
| **Overlay/Stack** | SOverlay | QStackedLayout | Stack | Control 嵌套 | ❌（PopupOverlay 是控件不是通用布局） |
| **Grid** | SGridPanel | QGridLayout | GridView | GridContainer | ❌ |
| **Wrap/Flow** | SWrapBox | FlowLayout | Wrap | FlowContainer | ❌ |
| **Canvas/绝对定位** | SCanvas | — | Stack+Positioned | Control(anchor) | ⚠️ 有 anchor(_anchorMin/Max)，无独立 canvas 布局 |
| Form | — | QFormLayout | — | — | ❌ |
| AspectRatio | — | — | AspectRatio | AspectRatioContainer | ❌（slot 有 min/max，无 aspect） |

### 2.2 控件对照

| 控件 | Slate | Qt | Flutter | Godot | 当前 |
|---|---|---|---|---|---|
| Button | SButton | QPushButton | ElevatedButton | Button | ✅ |
| Text | STextBlock | QLabel | Text | Label | ✅ |
| TextField | SEditableTextBox | QLineEdit | TextField | LineEdit | ✅ |
| CheckBox | SCheckBox | QCheckBox | Checkbox | CheckBox | ✅ |
| ComboBox | SComboBox | QComboBox | DropdownButton | OptionButton | ✅ |
| Slider | SSlider | QSlider | Slider | HSlider/VSlider | ✅ |
| Image | SImage | QLabel(pixmap) | Image | TextureRect | ✅ |
| Menu/MenuBar | SMenuBar | QMenuBar | — | MenuBar | ✅ |
| TabBar | STabBar | QTabWidget | TabBar | TabContainer | ✅ |
| **TreeView** | STreeView | QTreeView | — | Tree | ❌ **（editor hierarchy 必需）** |
| **ListView/TableView** | SListView/STableView | QListView/QTableView | ListView | ItemList | ❌（SelectableRow 是单行，无虚拟化） |
| **ProgressBar** | SProgressBar | QProgressBar | LinearProgressIndicator | ProgressBar | ❌ |
| **SpinBox** | SSpinBox | QSpinBox | — | SpinBox | ❌ |
| **RadioButton** | — | QRadioButton | Radio | CheckButton | ❌ |
| **Switch/Toggle** | — | — | Switch | CheckButton(toggle) | ❌ |
| **ToolTip** | SToolTip | QToolTip | Tooltip | — | ❌ |
| ColorPicker | SColorPicker | QColorDialog | — | ColorPickerButton | ❌ |

### 2.3 样式系统对照

| 框架 | 样式机制 |
|---|---|
| Slate | `FSlateStyleSet` + `FSlateBrush`（9-patch/图像/颜色），`FWidgetStyle`，样式表式数据驱动 |
| Qt | QSS（类 CSS）+ QPalette + QStyle 绘制引擎 |
| Flutter | ThemeData + TextStyle + ButtonStyle（Material） |
| Godot | Theme resource + StyleBox（9-patch）+ 每控件 theme override |
| **当前引擎** | ❌ 无。样式散落在控件里，改整 UI 风格需逐个改控件 |

### 2.4 数据驱动对照

| 框架 | 数据绑定 |
|---|---|
| Slate | `TAttribute<T>`（`.Text_Lambda([](){...})`），数据变化自动刷新 |
| Qt | Model/View（QAbstractItemModel）+ 信号槽 |
| Flutter | 声明式：setState/InheritedWidget，widget 树重建 |
| Godot | 信号（signal）+ 手动绑定 |
| **当前引擎** | ❌ 无。retain 模式手动 set |

### 2.5 输入对照（用户特别关注的 touch/手柄/navigation）

| 能力 | Slate | Godot | 当前引擎 |
|---|---|---|---|
| 鼠标 | ✅ | ✅ | ✅ |
| 键盘 | ✅ | ✅ | ✅ |
| **Touch** | ✅（Slate 手势/触屏） | ✅（InputEventScreenTouch） | ❌ **无 touch 事件** |
| **Gamepad** | ✅（Navigation + 手柄） | ✅（InputEventJoypad*） | ❌ **无手柄** |
| **方向键 navigation** | ✅（Focus Navigation，方向键在控件间移动） | ✅（focus_neighbor 四方向） | ❌ **只有 Tab 顺序遍历，无方向键** |

## 3. 蒸馏：当前最需要的功能（按优先级）

> 结合「Editor UI 迁移到 retain UI」+「游戏内 UI 可用」+「最小闭环」目标。

### P0 — 最小闭环必需（没有它 Editor UI 做不了 hierarchy/inspector）

1. **TreeView**：hierarchy 面板的第一块砖，也是 list/table 的通用基础。
2. **数据绑定**（Slate `TAttribute` 式 lambda 绑定或轻量 observe）：让 UI 随引擎数据刷新，摆脱手动 set，这是「数据来回展示/写入」的核心。
3. **Styles/StyleSet**（布局与样式分离，数据驱动样式）：用户明确要的「浏览器式布局/风格分离」，也是「改整 UI 风格」的前提。

### P1 — 补齐基础容器（接近 Slate/Qt 完整度）

4. Overlay/Stack（重叠层，模态/提示/拖拽 ghost 的通用底座）
5. Grid（网格布局）
6. Wrap/Flow（自动换行，工具条/标签流）
7. ListView + 虚拟化（大列表性能）

### P2 — 补齐基础控件

8. ProgressBar、SpinBox、RadioButton、Switch、ToolTip（低风险，逐个补）

### P3 — 输入完备（跨 PC/手游/主机）

9. Touch（touch begin/end）→ 手游端闭环
10. Gamepad + 方向键 Navigation（focus 在控件间四向移动）→ 手柄闭环

### P4 — 声明式 authoring（用户提到的 DSL/XML，方向未定）

11. 声明式 DSL（参考 EUI-NEO 的链式 builder `ui.column().content().build()`）或 XML 数据驱动。当前 retain builder 繁琐，但 DSL 形态（链式 vs XML vs 反射）尚未定，**不阻塞 P0-P3**。

## 3.5 浏览器声明式框架参照（React / Vue）

> 补充（2026-08-15）：原生 GUI 框架（Slate/Qt/Flutter/Godot）只覆盖了「控件集/布局/样式/输入」的完备度；浏览器声明式框架（React/Vue）提供了另一脉更关键的思想——**数据驱动 + 声明式描述 + 样式分离**。用户反复提到的「浏览器式布局/风格分离」「声明式语法」「XML 数据驱动」指向的就是这一脉。

### 值得吸收的映射

| 浏览器概念 | 引擎现状 | 值得吸收 |
|---|---|---|
| Vue 响应式（Proxy 依赖追踪） | 无数据绑定 | 「数据来回展示/写入」的最强参考：数据变 → 依赖它的 UI 自动更新，比 Slate `TAttribute` 显式 lambda 更自动 |
| 声明式 UI = f(state) | retain 手动 set | 声明式描述（对应 DSL 方向） |
| React 单向数据流（props/state） | 无 | 数据流可预测 |
| 组件化 + 生命周期 | `UITypeRegistry`（有注册，无组件模型） | 组件 = 声明式函数 + mount/unmount/update |
| Virtual DOM + diff | `UIFrameSnapshot`（整帧 rebuild，无 diff） | 最小更新：只重算变化分支 |
| Vue SFC（template+script+style 三段） | 样式散落控件 | 「布局/样式分离」的直接范本 |
| v-model 双向绑定 | 无 | 表单数据双向 |
| computed 派生状态 | 无 | 派生状态自动缓存 |

### 关键同构洞察

引擎的 `WidgetTree + UIFrameSnapshot`（live tree → 每帧构建 immutable snapshot）和 React 的 Virtual DOM 模型**同构**——都是「描述树 → 渲染层」。差别只在：

- React：状态变 → diff → 只更新变化部分
- 引擎：任何变化 → 整帧 rebuild snapshot（无 diff）

所以引擎缺的不是「声明式模型的地基」，而是 React/Vue 之上的**「数据驱动」层**：数据变 → 自动标记对应 widget dirty → 局部重建，而不是手动 set + 整帧 rebuild。

### 对 P0 排序的修正

对照 React/Vue 后，P0 三件套的顺序更清晰：

1. **响应式数据绑定**（参考 Vue reactive 的依赖追踪，而非照抄 Slate TAttribute）——声明式 UI / v-model / computed 的共同地基，也是「数据来回展示写入」的根
2. **Styles/StyleSet**（参考 Vue SFC 三段式 / React CSS-in-JS）——「布局/样式分离」直接落地
3. **TreeView**——在前两者之上用声明式 + 样式做 hierarchy

## 3.6 EUI-NEO 蒸馏（用户指定的 C++ 声明式 UI 参照）

> `https://github.com/sudoevolve/EUI-NEO`：跨平台 C++17 声明式 UI 框架（GLFW/SDL2 + OpenGL/Vulkan）。已抓取 DSL.md、布局.md、retained_layer_cache.md 三份文档。

### 核心设计

| 维度 | EUI-NEO 的做法 |
|---|---|
| **UI 描述** | C++ 内嵌链式 DSL：`ui.row("id").size(...).content([&]{...}).build()`，C++ 代码即 UI 声明，**不是宏、不是外部文件** |
| **数据流** | 单向：状态 → 回调改状态 → `composeRequested()` → 重新 `compose()` 整棵 UI 树（= React render） |
| **状态模型** | `ui.state<T>(id)` 实例状态；`loader` 管生命周期（`destroyOnHide()/keepAlive()`）；key 自动带 pageId/loader scope 前缀隔离 |
| **图元** | 布局：Row/Column/Stack/Flow；视觉：Rect（颜色/渐变/圆角/边框/阴影）、Polygon、Text、Image、Svg |
| **布局** | header-only 纯计算（不依赖渲染）；SizeMode(Fixed/WrapContent/Fill) + flexGrow/Shrink + gap/lineGap/margin/padding/min/max + `ignoreLayout()` |
| **事件** | `.onClick/onHover/onScroll/onDrag` 直接 lambda；Runtime 负责 hit-test / capture / 派发 |
| **声明式动画** | `.transition(0.42f, Ease).animate(Prop)`，目标值写属性，Runtime 自动插值 |
| **脏区渲染** | frame/视觉属性变 → 算 dirty rect → 只重绘脏区 |
| **保留层缓存** | 静态子树烘焙成离屏纹理，重绘时画纹理；连续稳定 2 帧才缓存；含动画/交互/滚动/image/svg 的子树不缓存 |

### 对引擎最有价值的吸收

1. **声明式 DSL + compose**：直接解决「retain builder 繁琐」——`ui.row().content().build()` 比手动 `make_shared + attach + setXxx` 清爽一个量级。
2. **`ui.state<T>(id)` + 单向数据流**：这是「数据驱动」在 C++ 里的一个成熟落地形态（比 Vue 的 Proxy 更贴近引擎的 C++ 现实，比 Slate TAttribute 更自动）。
3. **Rect 样式盒 + 链式样式**：样式作为「视觉图元的属性」链式声明，是「样式分离」的一种务实形态（不是分离样式表，而是把样式收敛到图元 API）。
4. **脏区渲染 + 保留层缓存**：引擎目前「整帧 rebuild snapshot」，缺脏区 diff 和静态图层缓存这两层性能优化。
5. **声明式动画（transition/animate）**：引擎目前没有动画系统。

## 3.7 ImGui 蒸馏（项目内 `Engine/ThirdParty/ImGui`）

### 核心设计

| 维度 | ImGui 的做法 |
|---|---|
| **模式** | **Immediate Mode**：每帧直接调 `ImGui::Button()` 画 UI，无持久 widget 对象 |
| **API** | 极简：`if (ImGui::Button("Save")) {...}` 一行 |
| **布局** | 无布局系统：`SameLine()` 手动排列 + 后期加的 `Table` |
| **样式** | 全局 `ImGuiStyle` + `PushStyleColor/PushStyleVar` 压栈 hack |
| **数据** | 无绑定：`InputInt(&value)` 传指针直接读写 |
| **状态** | 无保留状态：widget 状态靠内部 ID 全局表 |
| **绘制** | 每帧全量重建 `ImDrawList` |

### 优缺点

- **优点**：API 极简、零状态同步（数据就是局部变量/指针）、跨平台后端简单、适合工具/调试 UI。
- **缺点**：无布局、无样式系统、无数据驱动、全量重建、鼠标键盘中心、不适合复杂应用 UI 和触摸/手柄。

### 引擎移除 ImGui 的理由

immediate mode 适合工具，但不满足「统一 Editor UI + 游戏内 UI + 数据驱动 + 样式分离」的长期目标。用户已拍板移除。

### ImGui 值得保留的思想（蒸馏精华）

1. **极简控件 API**（一行画一个控件）→ 吸收进声明式 DSL（EUI-NEO 已做到）。
2. **ID 身份**（字符串 ID 区分同名控件）→ 引擎已有 `_name/_typeId`，已对齐。
3. **要抛弃的**：样式压栈、手动定位、指针数据同步——这些 immediate mode 的做法正是换 retain UI 要解决的问题。

## 3.8 SlateIM 蒸馏与架构定案（用户指定的 immediate/retain 分层）

> `https://dev.epicgames.com/documentation/en-us/unreal-engine/API/PluginIndex/SlateIM`（UE 5.8，Experimental）
> 原文定位：「An immediate mode wrapper for Slate. Intended for building debugging tools.」

### SlateIM 事实

- immediate mode **包装层**（wrapper），底层仍是 retain 的 Slate——不是替代，是叠加。
- 官方定位：**调试工具**（debugging tools），Experimental（shipping 需谨慎）。
- 4 个模块分层：`SlateIM`（核心）/ `SlateIMEngine` / `SlateIMInGame` / `SlateIMBlueprint`。
- 实际使用：MetaHuman Crowd、Audio Motor Sim。

### 架构定案（吸收 SlateIM + EUI-NEO + ImGui + React/Vue）

```
Immediate API（便捷层：ImGui 式，一行一个控件，工具/调试/快速原型）
      ↓ 「声明式描述树 → diff 复用」
Retain UI（性能底座：WidgetTree + UIFrameSnapshot + 脏区/保留层缓存）
      ↓
渲染（Render2D）
```

**核心判断**：immediate 和 retain 不是二选一，中间隔一层「声明 → diff → 应用」。这层就是「留的口子」——它一旦存在，immediate API 随时可加，retain 手动写法也继续能用。

### 性能评估管线（设计一等公民）

| 指标 | 含义 | 对应参照 |
|---|---|---|
| update / layout / paint 分三段计时 | 定位瓶颈 | — |
| 每帧 rebuild 的 widget 数 / dirty 区域 | diff 是否生效 | EUI-NEO 脏区 |
| draw item 数 / Render2D flush 次数 | 渲染提交量 | EUI-NEO 图元数 |
| retain 层命中率（命中/重建/绘制） | 静态层缓存 | EUI-NEO `H/M/D/Re` |
| 帧耗时 vs 帧预算 | hover 跟手度（vsync 延迟） | — |

### 参考价值收敛

- **写范式**：已定（immediate API + retain 底层 + 声明 diff），不再参考其它框架范式。
- **能力清单**：仍参考 Slate/Qt/Flutter/Godot 的控件/布局/样式清单补完备度。
- **性能层**：参考 EUI-NEO（脏区/保留层）+ SlateIM（immediate wrapper 模块分层）。

## 4. 结论

- 引擎 GUI 内核（布局对象模型、三阶段事件路由、focus/hover/capture、snapshot）**基础很扎实**，缺的主要是「控件集完备度」「样式/数据驱动」「输入完备（touch/手柄）」三块。
- 对「最小闭环」最关键的三个缺口是 **响应式数据绑定、Styles、TreeView**（P0）；它们直接决定 Editor UI（hierarchy/inspector）能不能用 retain UI 做出来。
- 数据驱动实现参考：**Vue 响应式 + React 单向数据流**提供思想，**EUI-NEO 的 `ui.state<T>(id)` + compose** 提供 C++ 落地形态；样式分离参考 **Vue SFC / CSS-in-JS** + **EUI-NEO 的 Rect 样式盒**。
- **架构定案**：retain UI（性能底座）+ 声明式 diff 复用（口子）+ 未来 immediate API（SlateIM 式便捷层）。第一刀「响应式数据绑定」同时是「声明式 diff 复用」的地基。
- 触摸/手柄/方向键 navigation 是手游/主机闭环的前置，但目前引擎连 touch 事件都没有，属于 P3 补齐。
