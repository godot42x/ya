# GUI 架构收敛计划：单主循环、Widget/Layout/Slot、事件路径与多窗口留口

> 建立日期：2026-08-13
> 状态：正式收敛期。当前主线不是继续堆控件，也不是先做 .yaui / UIDocument，而是先把 GUI 的主链路、布局对象模型、事件路由、control plane 基底与调试闭环做扎实。
> 关联：本计划是 GUI 框架下一阶段的唯一主线参考，替代此前零散的 host / workbench / minimal host / UIContainer 增量路线。

## 0. 结论摘要

本阶段的核心判断：GUI 线现在最缺的不是更多 feature，而是一个足够清晰、可持续扩展的内核。继续往 UIContainer 上补布局字段、继续在 Workbench 上修页面、或者先抽一层 ImGui/自研 GUI 可切换 facade，都会继续放大噪声。

本轮正式决策：

1. 应用主循环以后只保留一个统一内核：AppKernel。GUI app、runtime editor、game/editor host 都是往 AppKernel 注入模块与策略的应用形态，不再各自拥有第二套“真正主循环”。
2. GUI 视角的正式主链路定义为：AppKernel -> GUIApp(装配/策略层) -> GUIWindowHost -> WidgetTree。
3. WidgetTree 的职责定义为“单窗口 live visual tree”，不是整个 GUI app 的唯一全局根。
4. 正式布局模型采用 Widget / Layout / Slot 三层分离，而不是继续让 UIContainer 同时承载 widget 身份、布局参数、布局算法和 child layout data。
5. Slot 是 parent-child 边对象，不是 child 自身字段。方向上参考 UE 的 `Widget -> GetSlot() -> Cast<ConcreteSlot>`，但会收敛成明确的 parent-owned edge object + 类型安全访问，不把布局参数重新塞回 widget 本体。
6. 第一种正式布局实现为 UIBoxLayout + UIBoxSlot，替换当前 UIContainer::_direction/_spacing/_padding/... 体系。
7. authoring v1 只支持 imperative retain UI builder；.yaui / UIDocument 暂不绑定新布局对象模型。未来 XML/DSL/document 都只是这套运行时对象模型的 authoring 前端。
8. Example/GUIWorkbench 的定位是 feature gallery + regression app，不是 app framework 本体，也不是 editor shell 原型；Framework/GUI 只放可复用能力。
9. 多窗口 / docking 当前不实现，但必须现在预留 owner 边界；dragdrop 允许跨窗口，modal 支持 per-window/whole-app 两种 scope。
10. CLI、命令分发、事件注入、frame stepping、截图/回放等调试能力要上收为 AppKernel 之上的共享 control plane，由 GUI / game / runtime editor 共同复用；automation 只是其中一个子能力，不再每条线各写一套 `exit-after-frame` 或私有 smoke 控制。
11. 当前事件系统要从“边命中边执行的 DFS hit walk”逐步收敛到“显式 route path + route phase”的模型，即 pointer path / focus path / preview(target 前) / target / bubble。
12. retain UI 的 visual hierarchy mutation（attach / detach / reparent / reorder）是正式运行时能力，不是只服务 GUI app 固定壳的 authoring 便利接口；game runtime 可以频繁改父子关系，布局与事件模型必须以此为前提。
13. 下一阶段优先级调整为：先把目录、命名、target 依赖线与 owner 主链路收口，再继续扩张 GUI feature。否则后续所有控件、layout 与多窗口工作都会继续挂在历史壳上增长。

## 1. 当前问题到底在哪里

### 1.1 主链路不清晰

目前同时存在多层宿主语义，导致很难回答：谁是真正主循环、谁拥有窗口、谁拥有 WidgetTree、谁负责 control plane、谁负责 present。典型噪声来源包括：

- `Engine/Source/Product/Host/App.h`
- `Engine/Source/Framework/GUI/App/GUIAppHost.h`
- editor 内部的 ImGui panel 承载 WidgetTree
- demo/workbench 自己再带一套控制逻辑

问题不只是“有两个入口”，而是：

- 生命周期所有权难以追踪；
- CLI / scenario / agent / capture 等控制点分散；
- 多窗口无法自然长出来；
- agent 难以判断问题到底在 app kernel、window host、widget tree、layout、compose 还是 render。

### 1.2 布局模型职责混杂

当前布局主链路大致是：

- `WidgetTree::layout()`
- `UIElement::layout / layoutAssigned()`
- `UIContainer::arrangeChildren()`

其中 UIContainer 同时承担：

1. 容器 widget 身份；
2. box 布局参数存储；
3. box 布局算法；
4. child layout data 的隐式承载。

这条路线短期能跑，但继续扩张会卡死后续需求：

- 同一 widget 不能自然切换不同 layout；
- child 的布局意图没有独立 slot 模型；
- specialized layout（split / scroll / menu popup / inspector row）没有统一抽象基线；
- 为 multi-window、popup tree、workspace/docking 留口会越来越别扭。

### 1.3 visual hierarchy mutation 还没有被当成正式能力建模

当前虽然已经有 WidgetTree::attach / reparent / detach / reparentBefore / reparentAfter，但在心智模型上仍然容易被误解成“主要给 GUI app 静态拼页面用”。这对 game/editor 是不够的。

retain UI 正式需要支持的不是一次性 attach，而是：

- runtime 中频繁地把 widget 从一个父节点移到另一个父节点；
- 在 overlay / popup / drag ghost / inspector row / runtime HUD 之间切换挂载点；
- 在不销毁 widget identity 的情况下改变 visual parent；
- 让 slot 跟随 parent-child edge 重建，而不是把布局数据残留在 child 本体；
- 让焦点、capture、hover、drag session 在 detach/reparent 后按 tree 规则正确失效或迁移。

所以当前真正要巩固的是：Widget identity、visual parent、slot edge、layout invalidation、route state 在 runtime mutation 下的合同，而不是把“动态父子关系”当作传统 GUI app 的例外场景。

### 1.4 事件路由不够清晰

当前 `WidgetTree::dispatchEvent()` 更接近“从 root 做整树 DFS hit walk，子先于父处理；部分状态由 tree 特判（focus/capture/drag）”的混合模型。它能处理简单按钮，但在以下场景会越来越难维护：

- menubar 点击一个 entry 后，鼠标横移到另一个 entry，菜单是否切换；
- popup / modal / capture / drag 同时存在时，谁重写 route policy；
- keyboard 是否只发给 focused widget，还是应该经过一条 focus path；
- 事件回放、golden diff、调试 overlay 需要稳定的 event path 证据；
- 跨窗口 dragdrop 时，source/target window 的路径语义怎么表达。

所以当前问题不是“leaf -> parent bubble 没做完整”这么简单，而是整个 route 模型还没有被正式对象化。

### 1.5 GUI 缺陷不可观测

当前很多问题本质上不是“不会修”，而是缺少稳定观察面：

- menu 宽度不够、hover 切换不对、button 状态不回落；
- split/scroll 覆盖页头；
- render2d 与坐标/clip/flush 问题会表现成纯体验异常；
- 字体、布局、hit test、popup 层级问题不容易被 agent 精确描述。

如果没有 tree/layout/slot dump、debug overlay、事件回放、golden diff，后续继续做控件只会不断堆“看起来怪”的人工反馈循环。

## 2. 正式架构终局

### 2.1 顶层主链路

    AppKernel
      ├─ event pump
      ├─ frame loop / timing / exit policy
      ├─ control plane / CLI / stepping / capture
      ├─ shared services registry
      └─ AppModules[]
           ├─ GUIApp
           │    ├─ NativeWindowManager
           │    └─ GUIWindowHost[]
           │         ├─ native window
           │         ├─ input state / pointer state / modal scope
           │         ├─ presenter / compose target
           │         └─ WidgetTree
           │              ├─ content layer
           │              ├─ popup layer
           │              ├─ tooltip layer
           │              └─ drag overlay layer
           └─ Game / Editor / other modules

职责约束：

- AppKernel
  - 是唯一主循环；
  - 管事件源、frame timing、退出策略、control plane 基底、共享服务装配；
  - 不直接知道 GUI widget、布局或 RHI 细节；
  - 不再允许 GUI 线、game 线、editor 线各自再长一套平行 run loop。

- GUIApp
  - 不是第二个主循环，而是 GUI 应用装配/策略层；
  - 管 GUI 相关窗口集合、共享 GUI 服务、window host 创建策略；
  - 不直接知道具体 widget 布局细节；
  - 不直接拥有某个窗口内的 transient pointer/focus 状态。

- GUIWindowHost
  - 一窗口一树；
  - 持有 native window、presenter、输入上下文、该窗口的 transient UI state；
  - 是多窗口扩展的第一责任边界。

- WidgetTree
  - 只代表单窗口 live visual tree；
  - 管 focus path / hover path / capture / popup stack 等树级交互状态；
  - 不承担整个 app 工作区模型的概念。

### 2.2 control plane 与共享服务归位

control plane 的正式归位：

- AppKernel 之上提供共享 control plane：CLI、command dispatch、事件注入、frame stepping、等待条件、截图、golden diff、scenario 编排、remote/agent control；
- automation 退回成 control plane 下的一个子能力，而不是顶层总名字；
- GUI / game / runtime editor 都通过模块注册把自己的 surface、window、语义动作挂进去；
- GUI 不再自己 hardcode `exit-after-frame`、smoke switch 或专用 CLI；game/editor 也不再绕开同一基底另做一套控制服务；
- 这部分应归共享 App 层，不能继续留在单一 Product 语义里。

### 2.3 正式布局对象模型

    UIElement          内容与状态对象
    UILayout           父节点持有的布局算法对象
    UISlot             parent-child 边上的布局数据对象

正式职责定义：

- UIElement
  - 内容、状态、事件、绘制；
  - 最终 `_layoutRect`、desired size cache、invalidation state；
  - 不再直接内置复杂容器布局算法。

- UILayout
  - 负责 measure + arrange；
  - 输入为 host、children、slot 数据、约束尺寸；
  - 输出 desired size 与每个 child 的 assigned rect。

- UISlot
  - 表达 child 相对 parent 的布局意图；
  - 如 padding、alignment、stretch、weight、min/max 等；
  - 是反射、序列化、authoring 将来真正应该挂的地方。

正式所有权规则：

- 一个 child 在同一时刻只有一个 visual parent，因此只有一个 active slot；
- slot 属于 parent-child 边，由 parent/container 持有；
- layout 属于 parent/container；一个 container 同时只激活一个 layout；
- child 可通过 `getSlot()` 取回当前边对象；需要时再 cast 到具体 slot 类型；
- reparent 时旧 parent 销毁/解绑旧 slot，新 parent 创建新 slot；
- slot 属性改动至少触发布局 invalidation；影响 desired size 的改动触发 measure + arrange，只影响分配位置的改动可只触发 arrange。

### 2.3.1 runtime mutation 合同

为了让 game runtime 与 GUI app 共用同一套 retain UI 内核，visual hierarchy mutation 明确遵循以下合同：

1. attach、detach、reparent、reorder(before/after) 都是 runtime-safe 的一等操作；它们不是 document/import 专用流程。
2. widget identity 独立于 visual parent；reparent 默认保留 widget 自身状态，但 tree 级 hover / capture / drag target / focus path 必须按附着性重新验证。
3. slot 始终属于 parent-child edge：reparent 时销毁旧 edge slot，由新 parent/layout 创建新 slot；child 本体不缓存旧 slot 布局语义。
4. mutation 至少触发 arrange invalidation；若新旧 layout 或 slot 数据影响 desired size，则升级为 measure + arrange。
5. route 执行期间允许 detach/reparent，但 path trace 只保留稳定名字/快照，不保留可能悬空的裸指针。
6. popup、drag ghost、tooltip、modal overlay 也只是不同 layer/owner 下的 subtree mutation，不额外发明第二套 attach 语义。

### 2.4 第一种正式布局：Box

第一版正式布局不是内部 helper，而是长期对象模型的起点：

- `UIBoxLayout`
- `UIBoxSlot`

UIBoxLayout 负责：

- horizontal / vertical 主轴；
- spacing / padding；
- main-axis / cross-axis alignment；
- 按 slot 决定 auto / stretch / weight 分配；
- 统一 desired size 聚合策略。

UIBoxSlot 负责：

- margin / inset；
- alignment；
- auto / fill / stretch / weight；
- min / max / preferred；
- 是否参与布局、是否保留占位。

### 2.5 specialized layout 的归位

不是所有控件都该继续自己藏一套局部布局规则。下一步应逐步收敛为：

- `UISplitPane` 的几何排布下沉到 `UISplitLayout`；
- `UIScrollViewport` 的内容排列与裁剪窗口关系下沉到 `UIScrollLayout`；
- `UIButton` / `UILabel` / 单内容控件采用 `UISingleChildContentLayout` 一类的轻量内容布局；
- `MenuBar` / `PopupMenu` / `TabStrip` / `TreeView` / `InspectorRow` 在 box layout 站稳后，再决定是组合实现还是独立 layout。

原则：

- 普通容器布局靠通用 UILayout；
- 真正需要交互几何协议的控件才有 specialized layout；
- specialized layout 也是 layout，不再把 geometry 写回 widget 自己的若干字段。

### 2.6 事件路由正式模型

当前实现更接近“从 root 开始做整树 DFS hit walk，子先于父处理”，而不是显式的事件路径模型。它在简单按钮/面板上能工作，但对 menu、modal、focus path、跨窗口 dragdrop、自动化回放都不够清晰。

正式目标模型：

1. 命中测试只负责求出 target 与 route path，不在求路径时直接执行业务逻辑；
2. pointer 事件以 `pointer path` 为核心：root -> ... -> target；
3. keyboard 事件以 `focus path` 为核心，而不是只把事件塞给单个 focused widget；
4. 路由阶段分为：preview/tunnel（父到子，可拦截） -> target -> bubble（子到父）；
5. capture、modal、popup stack、drag session 都是 route policy，对求路径结果做改写，而不是散落在各控件局部；
6. WidgetTree / GUIWindowHost 持续维护当前 pointer state（位置、按钮、capture owner、hover path），而不是把 logicalPoint 作为上层应用接口一路下传；
7. route 结果除 handled/pass 之外，还应可观测地输出 target、path、capture owner、modal owner、popup owner，供调试和自动化使用。

这意味着后续会逐步把当前 `dispatchSubtree()` 风格迁到“显式 route path + route phase”模型。

### 2.7 多窗口与交互默认语义

需要明确的未来边界：

- app 有多个 window；
- 每个 window 一棵 tree；
- popup / tooltip 默认是 per-window；
- drag overlay 默认跟随所属窗口，但 dragdrop v1 允许跨窗口；
- docking/workspace model 位于 WidgetTree 之上，而不是普通布局系统的一部分；
- tab / docking 是 workspace 语义，不等价于 box child。

补充交互语义：

- dragdrop：允许跨窗口，但必须通过 GUIApp 级 drag session 协调 source window、hovered target window、drop commit；
- modal：支持 per-window 与 whole-app 两种 scope，默认由 GUIApp 统一管理 active modal stack；
- focus：采用 focus path 模型。基础焦点归属是 per-window，但 active window 由 GUIApp/AppKernel 维护；跨窗口切换时路径整体迁移或失效，而不是只存一个裸 focused widget；
- control plane：所有事件注入默认都要指定目标 window；单窗口 app 允许省略并走默认窗口。

## 3. 目录与分层收口

### 3.1 目标目录形状：共享主链 + 可拆分叉

上一版把未来目录写成了 Foundation / Framework / Product，这对职责讨论有帮助，但对真实物理目录不够好。现在正式改成面向未来拆仓的目标树：

    Core
      -> App
          -> Kernel
          -> Control
      -> GUI
          -> Host
      -> Game
          -> Editor
    Example/*

判断标准：

- 共同主链只能有一条：Core -> App/Kernel；
- App 不是一整团；它只保留无窗口内核与共享 control plane，不再承载窗口层；
- window/bootstrap/native event source 属于 GUI/Host；windowed GUI app、windowed game、runtime editor 都走 GUI bootstrap；
- Game 与 Editor 仍然是独立分支；headless game / DS 可以停在 App 之上而不被 GUI 强绑；
- 不能再让 Foundation / Framework / Product 这种抽象词继续作为未来物理目录目标；
- GUI-only 形态以后必须能看着目录直接知道：至少带走 Core + App/Kernel + App/Control + GUI；
- CLI / DS / server / render-service 形态以后必须能看着目录直接知道：至少停在 Core + App/Kernel（按需再加 App/Control）；
- windowed game/editor 形态以后必须能看着目录直接知道：至少带走 Core + App/Kernel + App/Control + GUI + Game + Editor；
- headless game / DS 形态以后也必须能看着目录直接知道：至少带走 Core + App/Kernel + Game；
- 不能再让 Product/Host/App、Framework/GUI/App、Foundation/Core/Application 都像真正主入口。

### 3.2 目标归位：当前目录到目标目录的映射

| 当前目录 / 类型 | 当前问题 | 目标归位 |
|---|---|---|
| Foundation/Core/*（除 Application） | 底层能力与 Application 主链混放 | 收到 Core/* |
| Foundation/RHI/* | 共享图形底座，但被 Foundation 这个抽象桶包住 | 收到 Core/RHI/* |
| Foundation/Core/Application/* | 已经是单 loop / control plane base，但物理位置还像 Core 杂项 | 按职责拆到 App/Kernel/* 与 App/Control/* |
| Framework/AppRuntime/* | 主要是窗口/bootstrap/native event source；本质上是 GUI bootstrap 的一部分 | 收到 GUI/Host/* |
| Framework/GUI/App/* | App 命名与唯一 App 主链冲突；读者难以判断它是 GUI 装配还是第二主循环 | 收口为 GUI/Host/*；只表达 GUI window/tree/presenter owner |
| Framework/AppServices/* | 名字像共享 App 层，实际是 game/render runtime contract | 回收到 Game/*（候选：Game/RuntimeServices/*） |
| Product/Host/* | Host 过于宽泛，混合 game runtime、control、window、GUI bridge、utility | 顶层 Product 取消；该目录回收到 Game/* 的 branch-local shell |
| Product/Host/GUI/GameUI/* | 名字容易让人误读成 generic GUI framework | 保留 game 语义，但归到 Game 分支内部，而不是挂在顶层 Product |
| Product/Editor/* | editor product 语义相对清楚，但放在 Product 下仍模糊 | 直接升格为 Editor/* |
| Example/GUIWorkbench/* | 当前定位大体正确 | 保留在 Example，不再让 GUI runtime 反向承担 demo/page 内容 |

### 3.3 GUI 与 Example 的边界

GUI 分支只放可复用 GUI 库，不放产品化 demo 内容。

保留与调整建议：

- Engine/Source/GUI/
  - Runtime/Widgets
  - Runtime/Layout
  - Runtime/Compose
  - Runtime/Draw2D
  - Runtime/Resource
  - Host（只放 GUI 相关 window host / presenter / tree glue；不再命名为 App）
  - Tooling（只保留真正可复用的 tooling 基座）
- Example/GUIWorkbench/
  - 作为 retain UI feature gallery / regression app；
  - 展示 menu、popup、dragdrop、modal、split、scroll、tree、property inspector 等；
  - 不再把 demo 页定义为 framework 的一部分。

### 3.4 命名收口原则

后续命名和职责应尽量收口为：

- AppKernel：统一帧循环与 control plane 基座；
- App/Kernel：唯一主循环、module lifecycle、service registry；
- App/Control：共享控制协议、CLI/command surface、scenario/frame stepping、capture/diff、remote/agent control 抽象；
- GUI/Host：native window、native window manager、event source、presenter、windowed bootstrap；
- GUIApp：GUI 应用装配层；
- GUIWindowHost：单窗口宿主；
- Presenter：窗口/离屏呈现边界；
- WidgetTree：单窗口 live tree。

避免继续出现 App / Host / GUIAppHost / WorkbenchSurface / Panel host 交叉都像入口的情况。过渡类型允许暂存，但方向必须是收口而不是再加一层新名字。

补充硬约束：

- App 只允许出现在真正应用级 owner / 装配层命名中；
- 顶层以后只保留一个 App/ 根；但它内部只保留 Kernel / Control；不再新增 GUI/App、Game/App、Editor/App；
- Host 只允许表达某个被上层 owner 持有的宿主对象，例如 GUIWindowHost；
- Surface 只表达渲染/呈现表面，不再表示页面壳或应用壳；
- Panel 只表达 editor/ImGui embedding，不再承担 framework owner 语义。

### 3.5 第一轮目录迁移顺序（不改行为）

第一轮迁移只解决目录、命名、target 依赖线，不顺手改对象模型或交互逻辑。顺序固定为：

1. 先产出目录 charter：逐层写清 allowed / forbidden responsibilities；
2. 先核对 xmake target 闭包：确认哪些是真边界，哪些只是历史目录名导致的假边界；
3. 先拆清 App 子树：Foundation/Core/Application/* -> App/Kernel + App/Control；
4. 再移动 GUI 宿主层：Framework/AppRuntime/* + Framework/GUI/App/* 一起收口到 GUI/Host/*；
5. 最后取消顶层 Product：Product/Editor/* -> Editor/*，Product/Host/* 回收到 Game/* 分支内；
6. 每一步都只允许做文件移动、include 修正、target/name 收口；不混入 owner 重写、layout 重构、route 重写；
7. 每做完一层立即验证：构建目标、GUIWorkbench 链接闭包、owner-model 与目录表达是否一致。

目录收口的正式 allowed/forbidden charter、命名禁用词、唯一去向决策与迁移 batch 定义，统一收敛在 directory-charter.md；后续真实 move/rename 必须以该工件为准，不再靠 review 评论或临时口头约定。

停止线：如果某一步需要引入新的过渡 facade、额外 host 或第二套 app owner 才能完成迁移，说明这一步不是收口，而是在继续制造噪声，应当回退到上一层重新定归属。

## 4. 设计原则与方法论

### 4.1 先定对象模型，再补控件

顺序必须是：

1. 先把对象模型与 owner 边界定清楚；
2. 再把 Workbench 迁到新模型上；
3. 最后才继续扩展 feature gallery。

不能再走“先把某个页面修到能看，再把通用逻辑抄出来”的路线。

### 4.2 每一步都必须有可观测输出

每次布局或交互框架调整，都至少要有两类证据：

- 结构证据：tree dump / layout dump / slot dump / route dump；
- 视觉证据：golden screenshot / 差异图 / debug overlay。

没有观测就不要继续加 feature。

### 4.3 先服务 imperative retain UI

.yaui / UIDocument 不是当前主线。短期目标不是文档格式，而是：

- retain UI API 能稳定构建 widget tree；
- layout 与 event 模型足够清晰；
- control plane 能模拟真实 GUI 交互；
- 后续无论 XML、DSL、document，最终都只是这套运行时对象模型的 authoring 前端。

### 4.4 多窗口不现在做，但现在就要留口

未来要支持多窗口、workspace、dock、detached tab，当前就必须把 owner 边界和事件/drag/modal/focus 语义定清，不允许再默认“全局永远只有一棵树”。

### 4.5 先收目录，再继续 feature

后续实现顺序调整为：

1. 先把目录、命名、target 依赖线与 owner 链条收口；
2. 再继续 layout/route/runtime mutation 的内核深化；
3. 最后才恢复 Workbench feature 扩张与 editor 迁移。

理由不是目录优先于功能的形式主义，而是当前目录结构已经在制造错误心智模型：一旦目录仍然让人误判谁是 loop、谁是 GUI host、谁是 product glue，后续 feature 每增加一页，噪声都会继续累积。

## 5. 需要的调试与自动化基建

### 5.1 树与布局 dump

最少需要：

- widget tree dump；
- layout tree dump；
- slot data dump；
- desired size / arranged rect / clip rect；
- focus path / hover path / capture / modal / popup stack 状态；
- route dump（target、path、phase、handled 结果）。

建议输出字段：type、name、layoutType、slotType、desiredSize、assignedRect、clipRect、visibility、enabled、focused、hovered、pressed、captured。

### 5.2 可视化 overlay

最少要有三种 overlay 开关：

- layout bounds；
- clip/scissor bounds；
- hit-test / hover / focus / route chain。

这样才能快速定位“字超界、内容被挡、hover 不切换、popup 错层”等纯体验问题。

### 5.3 自动化事件驱动

control plane 要上收成通用 GUI 事件基座，而不是每个 app 自己写 exit-after-frame 或 smoke switch。统一支持：

- mouse move / press / release；
- wheel；
- key press / release / text input；
- drag sequence；
- popup open / dismiss；
- resize；
- frame stepping；
- 指定 window 的事件注入。

同一套输入既能驱动 GUI app，也能驱动 future runtime editor shell。

### 5.4 golden diff

至少为以下页面建立可重复基线：

- menu / popup；
- split / scroll；
- button state；
- tree / inspector；
- modal / overlay；
- cross-window dragdrop（在支持后）。

golden 不只是截图，还要配结构断言，避免把错误像素结果当成功能语义验证。

### 5.5 长期 agent 执行 harness

GUI 这种长线架构重构，不允许只写一个 plan 然后靠记忆推进。每条正式重构线都必须附带执行 harness，保证多轮、长时间、多人/多 agent 接力时上下文不丢。

最少工件：

- `plan.md`：目标、边界、phase、验收；
- `todo.md`：当前阶段按优先级展开的可执行事项清单；
- `progress.md`：每轮完成内容、验证结果、剩余问题；
- `feature_matrix.json`：场景/能力/当前状态（planned/in-progress/pass/fail）；
- `session_checklist.md`：每轮开工与收尾固定步骤。

每轮开始默认步骤：

1. 读 plan / todo / progress / checklist；
2. 看 git status / 近期 git log；
3. 跑最小 smoke 或核心 scenario，确认基线未坏；
4. 只选一个最小垂直切片推进。

每轮结束默认步骤：

1. 保证代码可编译；
2. 相关 scenario / smoke / dump / screenshot 已验证；
3. 更新 todo / progress / feature matrix；
4. 记录决策、证据、剩余风险；
5. 工作区保持清楚，不留来源不明的中间状态。

## 6. 分阶段推进方案

### Phase 0 — Rendering correctness gate

目标：先把渲染正确性压到稳定基线，再谈 layout / interaction 收敛。否则很难区分到底是布局问题、事件问题还是 Render2D/compose/presenter 问题。

工作：

1. 确认 GUI 逻辑坐标始终是左上角原点，底层 Vulkan/MoltenVK 的 reverse viewport、present layout、render target 切换都被 presenter / compose 层完全掩盖；
2. 修正 resize 资源重建、clip/scissor、text baseline/方向、snapshot -> compose -> present 一致性；
3. 建立 windowed / headless / offscreen 三条路径的最小同帧一致性验证；
4. 验证 Render2D 多批次 flush、render target 切换、纹理槽位复用不会覆盖后续内容；
5. 保证 Vulkan / MoltenVK validation 零错误；
6. 给 Workbench 增加最小渲染调试开关：坐标轴、clip bounds、batch 边界、render target 标记、focus/hover overlay；
7. 给自动化补一个“单帧渲染烟雾测试”入口，确保 agent 可以在不手操 GUI 的情况下验证最基础的画面稳定性。

验收：

- resize 无崩溃；
- 文字方向、坐标方向、clip/scissor 正确；
- 同一 snapshot 在不同呈现路径上结果一致；
- Render2D 多批次 flush 不污染后续内容；
- validation 干净。

最小切片（建议先做这一刀）：

1. 固定一个最小 workbench 页面，只保留静态文本 + 单个 button + 一个 image 占位；
2. 开启 debug overlay，先把坐标、clip、batch、RT 切换看清；
3. 把 resize 崩溃与画面倒置先压掉，再看布局/交互。

### Phase A — 主链路命名与 owner 收口

目标：先让“谁拥有谁、谁是主循环、谁注入 control plane”清楚下来。

工作：

1. 文档与代码命名收口到 `AppKernel -> GUIApp -> GUIWindowHost -> WidgetTree`；
2. 明确 `GUIAppHost` 与 `Product/Host/App` 的边界：统一主循环是 AppKernel；GUI 线自己的类型只能表达装配层或单窗口宿主，不能再制造一个含混总入口；现有 `GUIAppHost` 作为过渡类型处理，最终拆解或重命名到上述结构；
3. 盘清哪些状态属于 app、哪些属于 window、哪些属于 tree；
4. control plane 基底从多层零散控制服务中上收，定义 kernel/base 与各模块扩展点；
5. 明确 popup/tooltip/drag overlay/modal/focus 的 app/window/tree 归属；
6. 给每个 owner 边界补一页“职责清单”：谁创建、谁销毁、谁持有、谁做 restore、谁负责 debug dump；
7. 把现有入口、host、app、panel、surface 的同名/近名类型做一次去重清点，列出保留、改名、过渡、删除四类结果。
8. 产出目录归位表：把 Foundation/Core/Application、Framework/AppRuntime、Framework/GUI/App、Product/Host、Product/Editor、Example/GUIWorkbench 映射到目标结构。
9. 先做不改行为的目录/命名收口：让共同依赖线能从目录上直接读出来，再继续内核 feature。

验收：

- 新开发者能从目录与命名上看懂 GUI 主链路；
- 文档能明确回答“哪个是真正主循环、哪个拥有窗口、哪个拥有 tree”；
- control 入口不再散落在多个 app/host 私有实现里。
- GUI example 链接闭包不再被 Product/Host 语义污染。

最小切片（建议先做这一刀）：

1. 先把 `AppKernel / GUIApp / GUIWindowHost` 的职责图画清并落文；
2. 再把现存 `GUIAppHost`、`Product/Host/App` 的关系整理成过渡表；
3. 再补目录归位表与命名禁用词规则；
4. 最后才动真实目录/target 迁移，并保持行为不变。

### Phase B — Layout/Slot 内核落地

目标：停止继续增强旧 UIContainer 布局字段路线。

工作：

1. 引入 `UILayout`、`UISlot` 基类；
2. 引入 `UIBoxLayout`、`UIBoxSlot`；
3. UIContainer 退化为 layout host：持有 children、持有当前 layout，不再自己存 box layout 参数与实现算法；
4. 引入 parent-owned slot 生命周期、reparent 规则、layout invalidation 规则；
5. 把现有 `UIContainer::_direction/_spacing/_padding/...` 迁移到 box layout / slot；
6. 先不做 document 序列化，只把 imperative builder 跑顺；
7. 给 layout / slot 的最小 public API 定义稳定访问面：child 取 slot、parent 取 layout、slot 改动触发 invalidation、layout 只负责 measure/arrange；
8. 为后续 specialized layout 预留基类或注册点，但先不把 split/scroll/dock 直接硬编码进去。

验收：

- Workbench 基础页面能在新 box layout 上运行；
- 测试不再直接依赖 UIContainer 内部布局字段；
- 后续新增布局特性不需要再改 UIContainer 类本体。

最小切片（建议先做这一刀）：

1. 先把一个最简单的 vertical box 页面迁到新 layout/slot；
2. 再把横向 nested box 加进去；
3. 最后验证 reparent / detach 时 slot 不悬空。

### Phase C — 事件路径与状态模型收口

目标：把事件系统从“DFS hit walk + 特判”迁到“显式 route path + route phase”。

工作：

1. 建立 pointer state / pointer path / focus path 抽象；
2. 抽出 hit test 求 target/path 与 route phase 执行；
3. 统一 preview/tunnel -> target -> bubble 三阶段；
4. 把 capture、modal、popup stack、drag session 归为 route policy；
5. 补 route dump、focus/hover overlay、自动化回放断言；
6. 把“当前鼠标位置”收敛成 tree/window 级持续状态，而不是放进每个 onEvent 接口参数里反复传递；
7. 为菜单、button、scroll、split、dragdrop 定义最小事件语义表，避免每个控件各自解释同一事件。

验收：

- 可以清楚描述一次 pointer/key 事件经过的 path 与 phase；
- menubar 横向 hover 切换、button 状态回落、popup/modal/capture 关系有稳定结构断言；
- route 调试证据可直接服务 agent 和回归测试。

最小切片（建议先做这一刀）：

1. 先把 mouse move / press / release 的 path 证据打出来；
2. 再让 menubar 的 hover 切换走 path，而不是局部状态硬改；
3. 最后补 focus path 与 Tab 切换。

### Phase D — Workbench 与测试迁移

目标：让现有 demo 成为新架构的真实练兵场，而不是旧 API 展览馆。

工作：

1. 把 Workbench demo 页面迁到新 retain UI + layout/slot builder；
2. 把 widgets test / tool controls test / layout test 一并迁移；
3. 优先修掉当前高频问题：menu 宽度与文字裁切、menu 激活后横向 hover 切换、split/scroll 覆盖页头、button hover/pressed/release 状态回落；
4. 将 Workbench 定位为“所有基础 GUI 行为的聚合 demo”，不是产品页，也不是实验页；
5. 给每个 feature page 配最少一条自动化 scenario 和一条截图基线。

验收：

- Workbench 能作为 feature gallery 跑通核心页面；
- 上述高频问题都有结构断言与视觉回归；
- demo 内容与 Framework 代码边界更清楚。

最小切片（建议先做这一刀）：

1. 先迁 menu / popup / button 三类页面；
2. 再迁 scroll / split 页面；
3. 最后迁 tree / inspector 页面。

### Phase E — Specialized layout 收敛

目标：把几何协议型控件纳入正式 layout 体系，而不是各写各的。

工作：

1. `UISplitPane -> UISplitLayout`；
2. `UIScrollViewport -> UIScrollLayout`；
3. 单 child 内容布局抽出轻量 layout；
4. 明确 popup/menu/tree row/inspector row 是组合控件还是专用 layout；
5. 对 menu/popup/tree row 先给出“组合控件优先、专用 layout 兜底”的默认策略，避免过早把每个控件都做成独立布局类型。

验收：

- specialized widget 的几何逻辑不再散落在 widget 字段里；
- layout debug 能统一看到 split / scroll / content layout 的结果；
- 后续新增树控件、表单控件不再需要复制局部布局套路。

最小切片（建议先做这一刀）：

1. 先收 `split`；
2. 再收 `scroll`；
3. 最后再决定 `menu/tree/inspector` 是否需要独立 layout。

### Phase F — 多窗口与 docking 留口验证

目标：不实现完整 docking，但验证当前对象模型不会把未来堵死。

工作：

1. 用类型设计证明一个 app 可拥有多个 `GUIWindowHost`；
2. 明确 cross-window dragdrop、modal scope、focus path 的默认语义；
3. 明确 per-window popup/tooltip/drag overlay 生命周期；
4. 明确 workspace/docking model 放在 tree 之上；
5. 为 future tabbed editor / detached inspector / floating panel 预留最小 host 接口；
6. 明确哪个对象负责 `active window` 与 `window activation order`；
7. 明确一个 drag session 在跨窗口时如何选择最终 drop target，以及取消时如何恢复 hover/focus 状态。

验收：

- 当前文档与类型边界不再默认“全局只会有一棵树”；
- 不需要改 layout 内核就能解释未来多窗口与 docking 的 owner 关系；
- dragdrop / modal / focus 的默认语义已经事先写清。

最小切片（建议先做这一刀）：

1. 先只证明多窗口 owner 关系；
2. 再补跨窗口 dragdrop 的语义；
3. 最后补 whole-app modal / focus path 的切换规则。

## 7. 这轮不做什么

以下内容不是否定其价值，而是明确不是当前主线：

- 不先做 `.yaui / UIDocument` 作为主 authoring 入口；
- 不先做完整 docking 系统；
- 不先做 editor shell 全量替换；
- 不先做大而全主题系统；
- 不先做 RHI 全面重写。

但以下边界必须在设计上被照顾到：

- future document / DSL 终将落到 retain UI 运行时对象模型；
- future editor shell 会复用 `AppKernel / GUIApp / GUIWindowHost / WidgetTree` 的层级；
- future docking/workspace 不应该逼迫普通布局模型返工。

## 8. 第一步一步该先做什么

建议执行顺序如下：

1. 先做 Phase 0：把渲染正确性压稳，否则后续 layout/interaction 回归不可信；
2. 再做 Phase A：主链路命名、职责图、owner、control plane 对齐；
3. 再做 Phase B：`UILayout / UISlot / UIBoxLayout / UIBoxSlot` 内核；
4. 再做 Phase C：事件路径与状态模型收口；
5. 然后做 Phase D：迁 Workbench 与测试，并只修新模型下暴露出的真实框架问题；
6. 再做 Phase E：把 split / scroll / 单 child 内容布局收编进正式 layout 体系；
7. 最后做 Phase F：验证多窗口 / docking 留口，而不是现在就实现 docking。

判断是否可以进入下一阶段的标准不是“demo 看起来差不多能用”，而是：

- 主链路是否更清晰；
- 模型是否更统一；
- 测试与观测是否更强；
- 下一类 GUI 能力是否能在当前架构上自然长出来。

每一轮实际推进时，只允许选“一个 phase 里的一个最小切片”，并满足以下完成条件才算收工：

- 代码不被半套新旧模型并存；
- 最少一条结构证据；
- 最少一条视觉或 scenario 证据；
- todo / progress / feature matrix 至少更新其一；
- 工作区保持可继续接力。

## 9. 成功标准

本计划完成时，应当能够满足：

1. 可以清楚讲明 GUI app 的主链路与所有权关系；
2. AppKernel 成为唯一主循环，control plane 基底被 GUI / game / runtime editor 共用；
3. UIContainer 不再承载大部分布局算法与布局状态；
4. retain UI builder 能通过正式 Layout / Slot 模型构建常见工具型 GUI；
5. 事件系统可以用 pointer path / focus path / route phase 来解释，而不是靠局部特判；
6. Workbench 成为 feature gallery + 回归 app，而不是历史布局试验场；
7. menu / popup / split / scroll / button 等基础交互具备稳定自动化回归；
8. 未来要接 document、DSL、多窗口、docking 时，不需要推翻当前内核。

## 10. 当前落盘后的行动指令

从本计划开始，后续 GUI 相关实现默认遵循以下优先级：

1. 先收 AppKernel / GUIApp / GUIWindowHost / WidgetTree 主链路与 owner；
2. 再立正式 Widget / Layout / Slot；
3. 再收事件路径与 route phase；
4. 再迁 Workbench / 测试；
5. 再补 specialized layout；
6. 最后再扩 feature gallery。

凡是继续往 UIContainer 上补布局字段、补局部规则、补临时状态的改动，默认视为与本计划冲突，除非它本身就是迁移到正式 layout 模型的一部分。
