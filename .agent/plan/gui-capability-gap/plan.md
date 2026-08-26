# GUI 能力补齐：GameEditor ImGui 替换前置（第二阶段）

> 建立日期：2026-08-18
> 输入工件：`audit.md`（含 2026-08-18 全量 ImGui 依赖调研）
> 状态：调研完成、计划定稿（经 Plan agent 设计，用户拍板范围）
> 前一阶段（数据绑定主线：Reactive + 增量复用 + P0 三件套）已收口，见 feature_matrix.json 旧 track。

## 0. 结论摘要

自研 GUI 框架在 GUI App 线（GUIWorkbench）**先全量补齐 GameEditor 所需 feature**，每个 feature 在 Gallery 页 demo + scenario 断言验证；**最后**才考虑替换 GameEditor 的 ImGui。

调研结论（audit.md §2）：GameEditor 用 32 文件 / 140 API / 1062 次 ImGui 调用。控件大头已有对应，缺口集中在 **DockSpace / 矢量绘制 / Table 布局 / 拖拽重排 / TreeView 编辑 / 输入控件** 六块骨架 + P1 交互补全。

范围（用户 2026-08-18 拍板）：上述全部 + DockSpace。**排除**：ImGuizmo（3D 视口，非 GUI App 线）、字体 CJK/emoji 回退链（渲染资源线）、IME 合成（宿主线）、剪贴板。

## 1. 目标与非目标

### 1.1 目标

1. 补齐 7 期 feature（见 §3），每期在 `Example/GUIWorkbench` Gallery 页（或独立 Dock 页）有可交互 demo。
2. 每个 feature 在 `Example/GUIWorkbench/Scenarios/` 写 jsonl scenario 断言（rect/control/layers 递归子集断言）。
3. 严守架构契约：retain-mode；瞬态状态 VisualFlag；持久字段 changed-only setter；MVC 受控控件不强塞 Reactive 绑定。
4. 全部完成后，自有 GUI 具备替换 GameEditor ImGui 的**能力条件**（替换本身是后续独立决策）。

### 1.2 非目标

- 不替换 GameEditor 的 ImGui（本阶段不做任何 GameEditor 改动）。
- 不做 ImGuizmo 等价物、字体回退链、IME、剪贴板。
- 不做虚拟化列表、保留层缓存、immediate API 便捷层。
- DockSpace 只做最小可用子集（见 §3 P7），不复刻 ImGui 完整 docking。

## 2. 关键设计决策（已定）

| # | 决策 | 内容 |
|---|------|------|
| 1 | 矢量原语渲染链 | 主选「新 draw item kind `Line` + `FLineRender` 增 screen 路径（screen 顶点 + scissor 走 session.clipStack）」。**兜底**：builder 内细四边形细分（零渲染层改动，牺牲粗线/AA）——若 screen pipeline 卡住即切兜底，bezier 砍成分段直线 |
| 2 | DockSpace 形态 | 专用 `UIDockSpace` + `UIDockLayout`（内部节点树：叶子=tab 组，中间=分割），复用 UISplitPane/UITabBar/drag 会话/矢量高亮。最小子集=中央区+左右边缘停靠+tab 合并+分割条拖拽；浮动窗/持久化排除 |
| 3 | DropTarget API | 复用 UIElement 已有 canAcceptDrop/onDrop/setDropHighlight 钩子，`UIDropTarget` = 谓词+回调+高亮 VisualFlag；`UIDragSource` 封装 beginDrag 触发。不新加路由策略 |
| 4 | 模态对话框 | 扩展复用 `UIPopupOverlay`（Modal 角色已覆盖遮罩/焦点/Esc/自 detach），新增 `UIDialog` 薄壳（标题栏+内容槽+OK/Cancel+`_onClosed`）。不新建层 |
| 5 | TreeView 扩展 | 三能力叠加式（重排/右键/过滤），重排走 host 回调 `_onReorder(from,to,mode)` 重建 ReactiveList（控件不拥有数据变异）；右键复用 UIMenu |
| 6 | Table API | `UITableLayout`（布局）+ `UITableGrid`（数据驱动控件，仿 TreeView 扁平 paint 不虚拟化） |
| 7 | tooltip | 复用既有 Tooltip 层 + `UIElement::setTooltip` 存储 + hover 超时挂载 |
| 8 | TextWrapped | 控件层分行（UIText 持 `_bWrap/_maxWidth`，分词断行 + 逐行 addText；computeDesiredSize 返回包裹高度）。builder addText 保持单行 |

## 3. 分期（7 期，基础设施先行 → DockSpace 收尾）

### P1 矢量绘制原语（基石）
- `UIFrameDrawItem::EKind` 增 `Line`；`UIFrameBuilder` 增 `addLine/addRectOutline/addBezierCubic`（bezier 客户端细分折线）。
- 消费链：`Render2DComposePass.cpp` 增 Line 分支 → `FLineRender::addScreenLine`（screen pipeline + clipStack scissor）。
- Gallery section「4. Vector primitives」静态线/框/贝塞尔。
- scenario：`gallery_vector.jsonl`（断言需扩展 WidgetTreeDump 或借 capture 像素——实施时定）。

### P2 Table/Grid 布局
- `UITableLayout` + `UITableSlot`（row/col/span/alignment），仿 UILayout/UIBoxSlot；`UITableGrid` 数据驱动控件（bindData(ReactiveList<FTableRow>) + bindSelection）。
- Gallery section「5. Table」4 列数据表 + 选中高亮。
- scenario：`gallery_table.jsonl`（WidgetTreeDump 增 table layout/control 块）。

### P3 输入控件补全
- 新建 `UIDragFloat`/`UISpinBox`/`UIRadioButton`（组）/`UIColorEdit`/`UISearchComboBox`，仿 CheckBox/Slider 骨架。
- Gallery section「6. Input controls」。
- scenario：`gallery_inputs.jsonl`（control.type + value/checked 断言，WidgetTreeDump 增块）。

### P4 拖拽重排 + DropTarget/DragSource
- `UIDropTarget`（`_accept` 谓词 + `_onDrop` + VisualFlag `_bDropHighlight`，paint 用 P1 高亮边框）；`UIDragSource` 辅助封装。
- Gallery section「7. Drag reorder」可拖拽重排列表。
- scenario：`gallery_drop.jsonl`（dragSession 路由 + 高亮/顺序断言）。

### G-A 框架护栏一（P5 之前插入）：paint self-clip + StyleSet set 语义

> 2026-08-18 用户定调：每开发一个 feature 就出现体验 bug，护栏补强须插入后续计划（P5-P7）之前。

- **G1 `UIElement::paint` 默认 self-clip**：paint 模板自动 `pushClip(_layoutRect)`（opt-out `_bSelfClip=false`），「widget 永不画出自已 rect」从控件自觉变为框架保证——消灭溢出绘制整类 bug（TreeView/Modal field/Gallery 盖 status bar 同根因）。
- **G4 `UIStyleSet::define` 同名 set 语义**：同名再 define 复用已有 handle 并 `set()` 新值（绑定者自动收到 notify），不再替换 handle 孤立绑定。
- 验收：移除 TreeView 手写 clip 后全部 scenario 回归通过；define 同名后已绑定控件重绘（scenario 断言）。

### G-B 框架护栏二：Event 时间戳 + debug 校验帧

- **G3 Event 加 timestamp**：Event 基类增时间戳字段，SDL 桥接填充；场景驱动以步进帧近似；双击检测改时间戳（去掉位置近似）。
- **G2 debug 校验帧**：debug 构建下每 N 帧（默认 60）强制全量重画并与增量缓存结果 diff，不一致时告警指明 widget——漏标脏在开发期被当场抓住，而不是上线后靠肉眼。
- 验收：双击判定走时间戳且 scenario 通过；人为制造一处漏标脏（临时 patch）触发校验帧告警，修复后告警消失。

### P5 TreeView 编辑能力
- 叠加式：`_bReorderable`（行 press 起 drag；canAcceptDrop 判定行间/行内；`_onReorder` 回调）；`_onContextMenu(nodeId)`（右击 UIMenu）；`bindFilter(Reactive<string>)`（flattenVisible 剪枝）。
- Gallery section「8. TreeView editing」。
- scenario：`gallery_tree_edit.jsonl`。

### G-C 验收基础设施：scenario 渲染帧开关（P6 之前插入）

> 2026-08-18 用户定调：人肉前验收须覆盖渲染级检查，scenario 模式不渲染帧是结构缺口。

- **`--scenario-render`**：scenario 模式下 `frame` 步骤真实渲染（buildSnapshot + compose 提交），使 G2 校验帧在行为场景内生效。
- **校验告警断言**：WidgetTree 累计校验 mismatch 计数（`getValidationMismatchCount()`）；场景新增 `{"assert_validation_clean":true}` 步骤断言「渲染帧内零校验告警」（scene dump 或专用断言通道）。
- 验收：gallery_acceptance 加 `--scenario-render` + 长帧运行零告警；人为注入漏标脏后 `assert_validation_clean` 失败。

### P6 P1 交互补全
- tooltip（决策 7）；TextWrapped（决策 8）；`UIElement::_bEnabled` 子树禁用（setEnabled changed-only + paint 灰度 + 输入入口拦截）；`UIDialog` 模态对话框（决策 4）。
- Gallery section「9. Tooltip/Wrap/Disabled/Dialog」。
- scenario：`gallery_p1.jsonl`。

### P7 DockSpace + 窗口管理（收尾，聚合前序）
- `UIDockSpace`/`UIDockLayout`（决策 2）+ 独立 `Dock` 页（全视口）。
- Gallery 加指针段指向 Dock 页。
- scenario：`dock.jsonl`（初始中央区 + 拖拽后 tab 合并/分割结构断言）。

## 4. 验收标准

1. 7 期全部落地：Gallery（或 Dock 页）demo 可交互。
2. 全部新增 scenario 通过：`xmake run GUIWorkbench --start-page <X> --scenario <abs>`。
3. 既有 8 页（Render/Widgets/Layout/Menus/DragDrop/Modal/ScrollSplit/Editor）无回归；Gallery 原 3 section 不破坏。
4. GUIWorkbench smoke（runDemoAutomation）不破坏——**新增 Dock 页会移位 Editor tab（case 20 点 tabs[7]），每加页同步更新 tab 索引与 FDemoState**。
5. 架构契约：VisualFlag/标脏纪律（漏标脏已复发 4 次，新控件零容忍）、changed-only setter、MVC/MVVM 分治。

## 5. 风险与停止线

| 风险 | 停止线/兜底 |
|------|------------|
| P1 渲染层改动（FLineRender screen pipeline）卡住 | 切细四边形兜底；bezier 砍分段直线 |
| P7 DockSpace 复杂度失控 | 砍到「中央区+左右停靠+tab 合并」，浮动窗/多级嵌套/持久化不做 |
| smoke 自动化 tab 索引硬编码 | 每加页立即更新 runDemoAutomation case 与索引 |
| 漏标脏复发 | 新控件瞬态一律 VisualFlag；code review 检查点 |
| Gallery 页过长 | section 化；Dock 已独立成页 |

## 6. 执行顺序

P1 → P2 → P3 → P4 → **G-A → G-B** → P5 → **G-C** → P6 → P7。每期 = 1 个自洽 commit（代码 + 工件更新 + scenario 同 commit）。

## 6.1 P8 UI 分层目标（P7 后，先定边界再扩展）

P1-P7 收口后，进入统一 GUI / Game UI / Editor 的 UI 入口建设。整体分为三层，依赖方向必须保持自上而下：

```text
Dynamic UI / Script UI
  Component / Props / State / Binding / Reconcile
                ↓
Static UI Builder / Static DSL
  Slate / EUI-NEO 风格强类型组合
                ↓
Retained UI Runtime
  UIElement / WidgetTree / Layout / Event / Paint
```

第一步只稳定 retained runtime 与静态 DSL，不把动态 component、脚本字段或 JSON bucket 渗透进底层：

- 保持每种控件自己的 builder（column/row/panel/text/button/textField），不引入一个拥有所有事件成员的通用 UIBuilder。
- `child(UIDescription)` / `children(UIDescription...)` 是静态 DSL 的基础契约；child 必须是已经构造好的强类型节点。
- `compose/composeChildren/when` 属于上层函数式扩展，不是静态 builder 的基础依赖。
- 组合语法必须保持 key identity、reconcile 复用、focus/capture 生命周期不变。
- 静态 builder 必须可以脱离 `UIRenderController` 和动态 component 独立构建基础 UI；动态层不得成为 standalone GUI、game runtime、editor 的硬依赖。
- 第一阶段只做 C++ 内嵌静态 DSL，不做 XML/脚本编译器、不做 immediate API、不做 theme DSL、不做 JSON 属性 bucket。
    - 已落地：静态 `children`、函数式 `compose/composeChildren/when`、`UIComponent` / `UIComponentFactory`、setEnabled / setFocusPolicy / panel.setColor / text.setFontSize / button.setText / textField.setText / textField.setFontSize。
    - 容器属性 contract 已落地：spacing / padding / clipChildren / stretchLastChild。
    - 已落地 callback authored 语义：未声明 callback 时保留运行态回调，显式 `onClick` 时才覆盖。
    - 下一刀：继续审计其它 authored-vs-default 字段，并把静态 builder 的创建/应用路径从动态 reconcile 中独立出来。
    - 结构前置：继续把可扩展的节点映射留在 `UIDeclarativeNodeAdapter`，让 `UIReconciler` 只服务动态描述，保持轻壳，只管生命周期/identity/顺序。

### 三层职责边界

1. **Retained UI Runtime**：只持有 widget/tree/layout/event/paint 运行态；不依赖 JSON、反射、component 或脚本。
2. **Static Builder / DSL**：每个控件强类型 builder 与 typed description；不使用字段字典；可直接创建 retained widget，也可生成静态 description。
3. **Dynamic UI / Script**：负责 component、props、state、binding、条件/循环和 keyed reconcile；脚本/JSON/反射只能在这一层转换为 typed description。

JSON bucket、反射属性查找、脚本 schema 都是边界适配机制，不得进入 retained runtime 的热路径。

### 6.2 P8 typed description 架构重构（新增设计，先于继续扩展属性）

当前单一 `UIDescription` 已出现 god struct 趋势：所有控件的字段和 `_bHasXxx` authored 标志集中在一个平面结构中，后续每增加控件都会同时膨胀 description、builder 和 adapter 分支。下一阶段必须先完成 typed description 重构，再继续扩展属性。

#### 设计来源与取舍

- **React**：保留统一节点外壳的 `type/key/children` identity 语义；reconcile 继续按 key + kind 复用 retained widget。
- **Slate**：每种控件拥有自己的 typed arguments/builder/slot，不把所有事件和属性塞进通用 builder。
- **Flutter**：description 是不可变配置，Element/WidgetTree 承载长期 identity，focus/capture/编辑缓冲等 runtime state 不进入 description。
- **QML**：属性归属于具体 type，后续可在具体属性上增加 binding，而不是使用无类型属性字典。

#### 目标数据模型

```text
Common description
  identity: key / displayName
  layout: position / size / enabled / focusPolicy
  children

Typed description payload
  Column/Row: spacing / padding / clipChildren / stretchLastChild
  Panel: color
  Text: text / fontSize / color
  Button: text / onClick
  TextField: text / fontSize
```

推荐实现为“公共部分 + typed payload”：

```cpp
struct UIElementDescription {
    UIIdentityDescription identity;
    UILayoutDescription layout;
    UIInteractionDescription interaction;
    std::vector<UIDescription> children;
};

struct UIButtonDescription : UIElementDescription {
    std::string text;
    std::optional<std::function<void()>> onClick;
};

using UIDescription = std::variant<
    UIColumnDescription, UIRowDescription, UIPanelDescription,
    UITextDescription, UIButtonDescription, UITextFieldDescription>;
```

实现时需要处理 C++ 递归 variant 的间接层；可以使用 `UIDescriptionRef` 或内部 node indirection 保持 description 值语义，避免直接递归类型导致编译和 ownership 复杂化。最终选择以最小 ownership 复杂度为准，不引入 premature XML/脚本 AST。

#### 必须保持的边界

1. `UIReconciler` 只处理 identity、生命周期、children order、stale subtree cleanup，不知道具体控件属性。
2. `UIDeclarativeNodeAdapter` 改为按 typed payload 分发（优先 `std::visit`/typed handlers），不再继续堆叠 `dynamic_cast + _bHasXxx`。
3. 每种控件保留自己的 builder；builder 只暴露该控件合法的成员和 slot。
4. authored 属性使用 `std::optional<T>` 或等价明确标志表达；未 authored 时不得覆盖 retained widget 的运行态。
5. description 不持有 runtime state：focus、pointer capture、编辑态、caret、drag session、Reactive dependency、WidgetTree pointer 均留在 retained widget/tree。
6. 静态 DSL 与函数式 composition 分层：`child(UIDescription)` / `children(UIDescription...)` 是基础契约；`compose/composeChildren/when` 是建立在静态 DSL 之上的扩展。
7. `UIComponent` 只负责生成 `UIDescription`，不直接持有或修改 WidgetTree，也不把 factory 存入 description。

#### 分步迁移

1. 已开始 typed payload 兼容迁移：`UIPanelDescription`、`UITextDescription`、`UIButtonDescription`、`UITextFieldDescription` 已落地；公共 description `UICommonDescription` 已抽出并 helper 化，静态 `children()` 与函数式 `compose()` 已分层，下一步继续收缩 flat fields。
2. 建立 `UIColumnDescription`、`UIRowDescription`、`UIPanelDescription`、`UITextDescription`、`UIButtonDescription`、`UITextFieldDescription`。
3. 保留当前静态 builder 调用语法，内部改写为 typed description；先做兼容转换，不一次性改调用方。
4. 将 `UIDeclarativeNodeAdapter` 改为 typed handler/`std::visit` 分发。
5. 将 authored/default contract 迁移到 `std::optional` 语义，删除旧 `_bHasXxx` 平面字段。
6. 删除旧 god `UIDescription` 字段，并保留 stable key、focus/capture、callback、stale cleanup contract tests。
7. 在静态 DSL 稳定后，增加独立 `UIComponent` 扩展：先无参 description factory，再设计 `ComposeContext/Props`，最后才讨论 state/binding/lifecycle。
8. 最后再扩展新控件和更复杂属性。

#### 验收门槛

- 现有 DSL 调用语法不变或仅有机械迁移。
- `DeclarativeContractTest` 覆盖 identity reuse、focus preservation、runtime callback preservation、authored override、children/when、stale cleanup。
- typed description 不引入通用 fat `UIBuilder`，不把 theme DSL、binding DSL 和 runtime state 混入本阶段。
- `Declarative.cpp` 不重新承载控件属性分支；新增控件的扩展面限于 description、builder、typed adapter handler 和 contract tests。
- 静态 DSL 不依赖 component 扩展；component 扩展必须可被禁用/替换，不影响 standalone GUI、game runtime、editor 的基础描述构建。

## 7. 修订记录

- **2026-08-18 首版**：ImGui 调研 → 范围拍板（上述全部 + DockSpace）→ Plan agent 设计 → 定稿。
- **2026-08-25**：根据 Slate / React / Flutter / QML 对比，新增 P8 typed description 架构重构：公共 description + 控件专属 payload，先解决 `UIDescription` god struct，再继续扩展 DSL。
- **2026-08-25**：明确静态 Slate 风格 DSL 是基础层，React 风格 `UIComponent`/`compose` 是上层扩展；移除 `content()` 兼容入口，禁止两种语义继续混用。
- **2026-08-25**：进一步定案为三层架构：retained runtime → 强类型静态 builder/DSL → dynamic component/script；JSON bucket、反射与脚本 schema 只允许存在于动态边界，不进入底层热路径。
