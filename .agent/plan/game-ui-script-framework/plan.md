# Game UI 脚本驱动框架计划

## 0. 目标与结论

让游戏里大量的 UI 逻辑有归属：**界面自己带脚本、有生命周期、有状态**，玩法脚本只管玩法。
同时把一帧里「谁先跑、谁后跑、暂停时谁停」写成引擎契约，而不是遍历顺序的副作用。

验收用例是 `Example/GreedSnake`：迁移后

- `Snake.lua` 只剩移动、碰撞、计分；不出现任何文档 id / 控件名字符串，没有 `settingsOpen`，
  不自己处理 Esc。
- `HUD.lua` 提供 `setScore`；`GameOver.lua` 处理重开；`Settings.lua` 处理速度按钮、自己的
  文案和关闭。设置面板打开时游戏暂停、世界输入被拦截，由框架负责。
- 场景、文档、脚本三者可在编辑器里编辑；设计器和视口预览不运行脚本。

核心结论（2026-09-28 讨论）：

1. **不给控件配实体，不把 `LuaScriptComponent` 挂到 UI 上。** 理由：GUI 框架不依赖 ECS/Scene/Lua
   （GUIWorkbench、编辑器 chrome 共用控件）；UIDocument 是与场景无关的模板，预览每次重挂；
   会出现两棵要同步的树；世界脚本在暂停时整段被跳过，暂停菜单会跟着冻结。
2. **把「脚本实例」和「宿主」拆开。** 一套与宿主无关的 Lua 脚本运行时（加载、`self`、回调、
   属性覆盖、热重载、错误隔离），两个宿主：实体（`LuaScriptComponent`，保持现状）和控件
   （GameRuntime 里的 `UIBehavior`）。这满足「像 Unity 一样往任何东西上挂脚本」，但不引入
   UUserWidget 式的平行控件类型。
3. **GUI 只提供与 Lua 无关的行为挂载数据。** 文档节点上带不透明的 `behaviors` 描述；
   激活（把描述变成活的行为）由宿主决定。运行时宿主激活，设计器/预览不激活。
4. **帧顺序是契约。** 事件 → 游戏逻辑（受游戏暂停控制）→ UI 逻辑（按各自时钟）→ 结构变更
   统一生效 → 渲染（只布局和快照，不调脚本）。
5. **界面与玩法之间不造事件总线。** 玩法调用界面实例上的方法（`ui.get("HUD"):setScore(n)`）；
   界面通过按钮动作冒泡把意图交给上层（控件脚本 → 条目根脚本 → 世界脚本）。

## 1. 现状基线（硬编码与缺口盘点）

以 2026-09-28 工作区为准，所有条目都在代码里核对过。

### 1.1 引擎侧

| # | 位置 | 现状 | 归属 |
| --- | --- | --- | --- |
| H1 | `GameRuntime/Script/GameplayLua.cpp` `kSpriteTexture` | `world.spawnSprite` 固定用探针图 | S5 |
| H2 | 同上 `ui.setUiActionHandler` → Lua 全局 `onUiAction` | 整个 Lua 状态一个回调，后写覆盖；Snake 的 `onDestroy` 没清它，编辑器 Stop 后仍挂着 | S3 |
| H3 | `InputRouter::setGameKeyHandler` | 全局单槽，第二个脚本覆盖第一个；原始按键码 | S5 |
| H4 | `GameUIHost::setMountedText/Visible` | 只能改文字/显隐；每次按名字 DFS 整棵树，同名取第一个 | S3 |
| H5 | `GameUIHost::bindButtonActions` | 只在 `setMountedRoots` 时绑定一次；之后生成的按钮、或挂载后才设置的回调都不生效 | S3 |
| H6 | `DefaultGameUIController::addToWorld` | 动态挂上的控件不进 `_mountedRoots`，脚本查不到 | S3 |
| H7 | `EditorLayer::createAndMountGameUI` | 写死 `Content:UI/PanelN.yaui.json`、根类型 `engine.panel`、`zOrder = 条目数` | S6 |
| H8 | `SceneWidgetEntry::fromJson` | `rootSlot` 必填，全屏铺满也要写十几个字段 | S5 |
| H9 | `UICanvasLayout::resolveChildRect` | 点锚点轴上 `area` 从锚点出发却取整个父尺寸，锚点 0.5 + 居中会落到右下；内容里只好写「锚点 0 + 居中对齐」绕开 | S5（决策门 D7） |
| H10 | `FButtonStyle` 默认 | 浅灰底 + 白字，内容里每个按钮手动改深色字 | S5 |
| H11 | `App::_bPause` | 只有读取（`GameRuntimeTickOrchestrator` 与 UI 时钟），全仓库没有写入点；暂停不存在 | F0 |
| H12 | `GameRuntimeTickOrchestrator::tick` | 暂停时 `tickLogic` 整段跳过：系统、Lua、文件监视、模块、输入状态更新一起停 | F0 |
| H13 | `LuaScriptingSystem::onUpdate` | 执行顺序 = EnTT 存储顺序；`onInit` 在第一次 `onUpdate` 循环里顺带调用，没有「全部 init 完再 update」 | F0 |
| H14 | `RuntimeRenderContext::buildGameRenderFrame` | UI `update` 在渲染阶段、且只有存在 display root 时才跑；没有渲染器就不更新 | F0 |
| H15 | `WidgetTree::tickSubtree` | 隐藏子树不 tick；子节点按挂载顺序而不是 `zOrder` | 保留：D4 让 UI 脚本 tick 按需开启并沿用此语义 |
| H16 | `GameUIHost::_updateClock` | 整棵树一个时钟，HUD（游戏时间）和暂停菜单（真实时间）没法各选各的 | S4 |
| H17 | `world.destroyEntity` | 在 Lua 视图遍历中立即销毁；删到带脚本的实体会破坏遍历 | F0 |

### 1.2 游戏侧（`Snake.lua`）为弥补框架空缺写的代码

- 文档 id / 控件名 / 动作名字符串：`"HUD"/"Score"`、`"GameOver"`、`"Settings"/"SpeedValue"`、
  `restart`、`speed.*`、`settings.close`，JSON 和 Lua 各写一遍。
- `settingsOpen` 手动暂停；Esc 开关写在玩法脚本里。
- 速度文案由玩法脚本回写到设置面板。
- 相机取景每帧在 Lua 里改 `orthoHalfHeight`，墙用 Lua 生成（属于世界编辑性，见 §7 不做项）。

### 1.3 可复用的现有能力

- `UIBehavior`：控件上的生命周期 / tick / 输入钩子（GUI 自带挂载点，对标 MonoBehaviour）。
- `UIScreen` / `ScreenStack`：挂卸、z 顺序、输入拦截策略（`EInputBlocking`），GameRuntime 未使用。
- `Reactive` / `bindText` / `bindEnabled`：C++ 侧响应式绑定。
- `LuaScriptComponent::ScriptInstance`：`self` 表、属性发现与覆盖、`releaseLuaHandles`。
- `UIButton::_action`（已反射、可在设计器编辑）。
- `mountSceneAutoMountEntries`：运行时与预览共用的唯一挂载路径。

## 2. 边界（不可越过）

- GUI 框架（`ya-gui-widgets`）不 include Lua、ECS、Scene；新增数据对 GUI 是不透明的。
- 不引入 Node2D / Transform2D / Camera2D / 第二棵场景树；UI 不进 ECS 层级。
- 不引入 UUserWidget 式平行控件类型；脚本是行为，不是控件子类。
- 不造统管 UI+Scene+Editor 的 UIManager，不造中心事件总线。
- 设计器和视口预览是 authoring 模式：**不激活脚本、不 tick 脚本**（`game-ui-authoring`
  `designer_is_authoring_mode` 保持成立）。
- `ScriptApiRegistry` 仍只服务自动化/编辑器，不作为玩法 Lua 通道；自动化可以新增验证用方法。
- 不做反射自动绑定；Lua 控件句柄逐个显式绑定。
- 不在帧录制中途创建/销毁 GPU 资源；控件/实体的结构变更推迟到 StructuralFlush。
- 日志只用 `YA_CORE_*`；公开头只在 `include/<Module>/`。

## 3. 目标帧顺序（F0 冻结后写入 skill）

```text
Tick
├─ EventPump（同步）
│   ├─ UI 路由：命中测试 → 控件事件 → 按钮动作冒泡（控件脚本 → 条目根脚本 → 世界脚本）
│   ├─ 模态拦截：最上层可见模态条目存在时，世界输入到此为止
│   ├─ 取消动作（Esc/返回）：最上层条目脚本 onCancel → 世界脚本 onCancel → 兜底
│   └─ 世界脚本按键：onKey 按执行顺序派发，返回 true 即消费 → 兜底（退出等）
├─ Logic（保持现有顺序，逐步门控；「暂停」= App::isPaused()，只停标 ▲ 的步骤）
│   ├─ TaskManager、TimerManager、自动化
│   ├─ 系统按注册顺序：引擎维护类（ModelInstantiation、Transform、LinkageFramework）常跑，
│   │   ▲ 玩法模拟类（SkeletonAnimation、Physics）暂停跳过；类别在注册处显式声明
│   ├─ ▲ 世界脚本（Runtime / Simulation；在玩法模拟系统之后：读到本帧物理结果）
│   │   ├─ 本帧新实例：全部按顺序 onInit，再全部按顺序 onStart
│   │   └─ 全部实例按顺序 onUpdate；本帧 onUpdate 中新建的实例排到下一帧
│   │   顺序键 = (executionOrder, 场景树前序, 实体句柄, 实体内脚本下标)
│   └─ 文件监视/热重载、模块 onLogic（编辑器；需要时自己读 isPaused()）
├─ UILogic（不依赖渲染器是否存在；Runtime / Simulation 且有挂载场景时）
│   ├─ presentation 仍在渲染侧 buildSnapshot 前设置：update 不读它，布局在 buildSnapshot 里
│   ├─ 新挂载条目：整条目实例化完成后，按 (zOrder, 条目顺序, 树前序) 调 onInit，再调 onStart
│   ├─ 显隐变化：onShow / onHide
│   ├─ UI 脚本计时器（self:after / self:every）：按 host 时钟推进，与可见性无关（D4）
│   └─ WidgetTree::tick（host 时钟，只走可见子树）：tween 等视觉行为，以及**显式开启 tick**
│       的 UI 脚本 onUpdate(dt)。UI 脚本默认不 tick，走事件驱动（D4）
│       脚本读到的控件几何是上一帧布局结果；需要本帧结果时显式请求立即布局（S3 句柄）
├─ StructuralFlush
│   └─ 本帧排队的实体销毁、控件 spawn/destroy、条目挂卸统一生效（脚本 onDestroy 在此调用）
├─ InputStateUpdate（postUpdate / preUpdate，不受暂停）
└─ Render：setPresentation + layout + buildSnapshot + 录制；不调用任何脚本
```

F0 已落地（2026-09-28）：上图除 UI 脚本回调与 StructuralFlush 中的控件/条目部分（S3/S4）外均由
`GameRuntimeTickOrchestrator::tickLogic` 实现，`TickOrderTest.*` / `TickOrderAppTest.*` 钉住。

生命周期约定（世界与 UI 共用名字；UI 默认不 tick，见 D4）：

| 回调 | 时机 |
| --- | --- |
| `onInit` | 实例创建后；同一批新实例全部创建完成后再逐个调用，因此可以互相查找，但对方状态未必已初始化 |
| `onStart` | 同一批新实例的 `onInit` 全部执行完后、首次 `onUpdate` 之前；此时可以依赖其他实例在 `onInit` 里准备的状态 |
| `onUpdate(dt)` | 世界：GameLogic 每帧，按顺序键。UI：默认不调用；`self:setTickEnabled(true)` 后由 `WidgetTree::tick` 在控件可见时调用（树顺序），`false` 关闭 |
| 计时器回调 | 仅 UI：`self:after(sec, fn)` / `self:every(sec, fn)` 返回可 `cancel()` 的句柄；UILogic 中按 host 时钟触发，隐藏不停；实例销毁时自动取消 |
| `onShow` / `onHide` | 仅 UI：条目或控件可见性变化后，下一个 UILogic 调用 |
| `onAction(name, widget)` | 仅 UI 与世界：按钮动作冒泡，返回 true 停止 |
| `onCancel()` | 取消动作，返回 true 停止 |
| `onKey(key, pressed, repeat)` | 仅世界：按键，返回 true 消费 |
| `onDestroy` | StructuralFlush；Stop / 场景切换 / 卸载 / 热重载替换时 |

## 4. 分阶段

每个 phase 只有一个可验收目标；跨 phase 的改动先改计划。

### F0 — 帧顺序、暂停与结构变更时机

目标：§3 的顺序在代码里成立，并有测试钉住。

- `GameRuntimeTickOrchestrator` 拆成 AlwaysLogic / GameLogic / UILogic / StructuralFlush /
  InputStateUpdate；`_bPause` 只门控 GameLogic。
- 系统暂停类别（D2）：系统基类声明「玩法模拟 / 引擎维护」，Physics、SkeletonAnimation 为
  玩法模拟，ModelInstantiation、Transform、LinkageFramework 为引擎维护；编排器按类别分两组调用。
- `TimerManager` 调用方审计（D2）：只有玩法调用 → 归入 GameLogic；玩法与引擎混用 → 拆成
  游戏时钟与真实时钟两个计时器，不共用。审计结果写进 progress。
- App 增加游戏暂停 API（写入点唯一，计数语义：`pushPause` / `popPause`），`isPaused()` 语义 =
  游戏时间是否停止；模块 onLogic 不受门控。
- `LuaScriptingSystem` 三遍：本帧新实例按顺序键 onInit → 同一批按顺序键 onStart → 全部实例
  按顺序键 onUpdate；onUpdate 期间新建的实例下一帧才进入（D5）。
- 执行顺序值（D5）：脚本返回表可声明默认 `executionOrder`（加载时读取）；`ScriptInstance`
  增加可选覆盖字段，仅在显式设置时序列化；有效值 = 覆盖值 ?? 脚本默认 ?? 0。排序结果缓存，
  场景树结构变化、脚本增删、顺序值修改、热重载时失效。
- UI 更新从 `buildGameRenderFrame` 移到 UILogic；`buildSnapshot` 留在渲染侧；
  `setPresentation` 在 UILogic 之前确定。这推翻 `game-ui-authoring` P0「放渲染侧因为暂停 gate
  了逻辑」的理由（暂停不再跳过 UI），实施时在该计划 progress 里记一笔（D1）。
- 实体销毁排队：`world.destroyEntity` 与 `Scene::destroyNode` 在 GameLogic 期间入队，
  StructuralFlush 执行并调用 `onDestroy`。
- 测试（`ya-testing`）：
  - `TickOrderTest.WorldScriptsRunByExecutionOrderThenTreeOrder`
  - `TickOrderTest.InstanceOrderOverridesScriptDefault`
  - `TickOrderTest.NewInstancesInitAllThenStartAllBeforeAnyUpdate`（合并原 AllNewInstancesInitBeforeAnyUpdate / AllInitsRunBeforeAnyStart）
  - `TickOrderTest.ScriptBaseInstanceResolvesUndefinedCallbacksThroughItsClass`
  - `TickOrderTest.InstanceCreatedDuringUpdateStartsNextFrame`
  - `TickOrderTest.DestroyDuringUpdateIsDeferredToFlush`
  - `TickOrderAppTest.PauseStopsGameLogicButNotUILogicOrInputState`
  - `TickOrderAppTest.PauseKeepsEngineMaintenanceSystemsRunning`
  - `TickOrderAppTest.NestedPauseRequiresMatchingResume`
  - `TickOrderAppTest.UILogicRunsWithoutRenderer`
- 实施偏离（已接受）：
  - 类别在注册处（`AppLifecycle`，`FRegisteredSystem`）声明，不放 `ISystem`：类别是宿主策略，
    同一系统在别的宿主里可以归不同组；注册必须给出类别，没有默认值。
  - 暂停 API 名为 `App::pushGamePause` / `popGamePause`。
  - 保持现有逻辑步骤顺序、逐步门控，不把模块 onLogic 挪到最前（挪动会改变编辑器时序，无收益）。
  - `setPresentation` 留在 `buildSnapshot` 旁：`GameUIHost::update` 不读 presentation。
  - 顺序不缓存：没有场景结构版本号可作失效依据；每帧 O(脚本数 × 树深) 求树路径，到有
    性能证据再加缓存。
  - 只有玩法 Lua 的 `world.destroyEntity` 入队；`Scene::destroyNode` 仍立即执行（编辑器、
    自动化、场景切换依赖立即语义）。
  - 热重载跳过尚未加载的实例（它首次加载就会读新源，避免 onInit 两次）；重载的实例按单实例
    批次 onInit → onStart。
  - `TimerManager` 审计：唯一调用方是编辑器 `EditorLayer.ViewportAuthoring.cpp` 的 `delayCall`，
    归 AlwaysLogic，不拆时钟。
  - 顺带修 `Engine/Content/Lua/Class.lua`：根类 `__index` 指向自身，查任何缺失字段死循环；
    C++ 开始读 `onStart` / `executionOrder` 后必然触发（GreedSnake 冒烟暴露）。`ScriptBase`
    补默认 `onStart`。
- 验收：上述测试绿；HelloMaterial / GreedSnake runtime smoke、editor smoke exit 0；
  GreedSnake 行为不变（这一步不改游戏脚本）。

### S1 — 与宿主无关的 Lua 脚本运行时

目标：一个脚本实例可以挂在实体上，也可以挂在任意宿主上，加载/回调/热重载/属性走同一份代码。

- `LuaScriptingSystem`（全局唯一，持有唯一的 `sol::state`）提供与宿主无关的实例接口：
  `load(instance, host)`、`call(instance, callback, args...)`、`reloadScript(path)`、`destroy(instance)`。
- 宿主接口 `ILuaScriptHost`：给 `self` 注入宿主字段（实体宿主注入 `self.entity`，控件宿主注入
  `self.widget`），回答「是否仍存活」。
- 活实例登记表：热重载遍历登记表，不再只扫当前场景的 `LuaScriptComponent` 视图。
- 错误隔离：单个实例回调报错只记日志、不影响同帧其他实例（沿用 `invokeLuaCallback`）。
- 实体宿主行为与现在完全一致（`LuaScriptComponent` 序列化不变）。
- 测试：现有 `LuaScriptComponentLifetimeTest` 全绿；新增
  `LuaScriptHostTest.SameScriptOnTwoHostsHasIndependentSelf`、
  `LuaScriptHostTest.HotReloadReachesNonEntityHosts`、
  `LuaScriptHostTest.CallbackErrorDoesNotStopOtherInstances`。
- 验收：GreedSnake / HelloMaterial runtime smoke 行为不变；编辑器 Lua 预览（`EditorLuaPreview`）不回退。
- 已落地（2026-09-28），实施取舍：
  - 不单独抽 runtime 类：`LuaScriptingSystem` 本就是 App 里唯一的实例，一对一的拆分只会多出
    转发层。系统自己持有 `sol::state` 与活实例登记表，实体只是它直接驱动的一种宿主；
    `_lua` 改为私有，经 `lua()` 暴露。
  - 实例类型从组件里抽出：`LuaScriptInstance` / `LuaScriptProperty`（`ECS/Systems/LuaScriptInstance.h`），
    `LuaScriptComponent::scripts` 持有它，序列化不变；调用点直接改名，不留别名。
  - 登记表以 `runtimeId`（单调递增、不复用）为键，宿主 `resolve(id)` 找回实例：组件存储与
    vector 移动都不影响；顺带消除 F0 记录的「同帧删除脚本导致下标错位」。
  - 实体宿主不保存 `Scene*`，每次向 active scene 服务取：播放中卸载场景（未经 `onStop`）时
    宿主只会解析不到，登记项被剔除，不会悬空。
  - `destroy` 先摘除登记再调 onDestroy：回调里再次销毁同一实例是空操作而非递归。
  - `call` 只覆盖现有生命周期回调（枚举 `ELuaScriptCallback`）；带返回值的具名回调
    （onAction / onCancel / onKey）留到 S3–S5 需要时扩展。
  - `EditorLuaPreview` 保持独立状态（IS_EDITOR、不跑回调），只随类型改名。
  - 追加测试：`LuaScriptHostTest.DestroyFromOwnOnDestroyRunsOnce`、`GoneHostLeavesTheRegistry`、
    `TickOrderTest.ScriptRemovedDuringUpdateDoesNotSkipItsSiblings`、`SceneGoneWithoutStopLeavesNoLiveHost`。

### S2 — GUI 通用行为描述（与 Lua 无关）

目标：任何控件节点都能在文档里带行为描述，往返不丢；GUI 不知道它是什么。

- `UIDocument` 每个节点新增 `behaviors: [{ "type": string, "data": object }]`，
  `fromWidget` / `instantiate` / `toJson` / `fromJson` 往返。
- `UIElement` 持有惰性描述 `_behaviorSpecs`（非反射字段、不参与绘制和布局）；
  `UIDocument::fromWidget` 从这里读回。
- 激活接口 `IUIBehaviorActivator`（GUI 定义、GUI 不实现）：`activate(UIElement&, spec, context)`。
  `mountSceneAutoMountEntries` 增加 activator 参数；运行时 `GameUIHost` 传入，
  `EditorGameUIPreview` 和设计器传 null → 预览不激活。
- 测试：`UIDocumentTest.BehaviorSpecsRoundtripOnEveryNode`、
  `UIDocumentTest.UnknownBehaviorTypeIsKeptOpaque`、
  `EditorGameUIPreviewTest.PreviewDoesNotActivateBehaviors`。
- 验收：GUI closure 依赖审计（`ya-gui-widgets` 不新增依赖）；GUIWorkbench smoke exit 0。
- 已落地（2026-09-28），实施取舍：
  - 描述、激活上下文与激活接口同在 `GUI/Widgets/UIBehaviorSpec.h`：`FUIBehaviorSpec{type, data}`、
    `FUIBehaviorActivation{entryId, entryRoot}`、`IUIBehaviorActivator`，以及遍历函数
    `activateBehaviorSpecs`（子树前序、节点内按书写顺序，每条描述调用一次）。
  - `fromJson` 只校验外壳（数组、`type` 为非空字符串、`data` 缺省为 `{}`、存在时必须是对象），
    外壳不合法整份文档拒绝，与现有子节点错误一致；`type` / `data` 内容原样保留。
  - `toJson` 只在节点有描述时写 `behaviors`，已有文档不变；格式版本不升（新增可选字段）。
  - 激活器归 `GameUIHost` 所有（`setBehaviorActivator`，默认空 = 描述惰性挂载），声明在 `_tree`
    之前以便它创建的行为先于它销毁；替换激活器时重挂当前场景，旧激活器活到重挂结束。
  - 激活时机：条目挂进树之后（行为可见到树）。`addToWorld` 动态挂载的激活留到 S3 与条目
    句柄一起做；设计器会话本就只 `instantiate` / `fromWidget`，不经过挂载函数。
  - 追加测试：`UIDocumentTest.MalformedBehaviorEnvelopeIsRejected`、
    `GameUIHostTest.MountActivatesBehaviorSpecsInPreorderWithEntryContext`。

### S3 — GameRuntime 控件脚本、句柄与按钮动作冒泡

目标：`script.lua` 行为可以挂在任意控件上，脚本拿到控件句柄，按钮先交给所在界面。

- `LuaWidgetScriptBehavior`（GameRuntime）：`IUIBehaviorActivator` 为 `type == "script.lua"`
  创建它，内含一个 S1 脚本实例，宿主 = 该控件。
- 句柄提供显式立即布局入口（UILogic 中读取本帧几何时使用，D1）。
- 驱动方式（D4）：UI 脚本默认只收生命周期与事件（onInit / onStart / onShow / onHide / onAction /
  onCancel / onDestroy），不每帧调用。
  - 按需 tick：`self:setTickEnabled(bool)` 切换 `LuaWidgetScriptBehavior::wantsTick()`，沿用
    `UIBehavior` 的 tick 协议——`WidgetTree` 每帧轮询、只走可见子树、按树顺序，不另建调度表。
  - 计时器：`self:after` / `self:every`，队列归 `GameUIHost`，在 `update` 里先于树 tick 按同一
    host 时钟推进；与可见性无关（弹窗自动关闭、倒计时在隐藏时也要走）；句柄 `cancel()`；
    实例销毁时连带取消。GUI 框架不引入计时器（与 Lua 无关的 tween 已有 `UIAnimation`）。
- `self.widget`：所在控件句柄；`self.root`：所在条目根句柄；`self:find(name)`：在**所属条目
  实例**内按名字查找（挂载时建索引，重名报 WARN，结果弱引用）。
- 显式句柄类型（弱引用，控件失效后操作 no-op 并记一次 WARN）：
  - `Widget`：`name`、`visible`、`find`、`destroy()`（入队到 StructuralFlush）
  - `Text`：`text`、`color`、`fontSize`
  - `Button`：`enabled`、`action`
  - `Image`：`texture`（路径，异步加载沿用 `gameUITextureSource`）
  - `Border`：`fill`
- 按钮动作不再在挂载时绑定：GUI 层 `WidgetTree` 提供与 Lua 无关的动作出口
  （`UIButton` 有 `_action` 时点击调用 `tree.emitAction(source, action)`），GameRuntime 接住后从
  源控件向上找最近的脚本实例调 `onAction`，未消费继续向上，到条目根后交给世界脚本
  `onUiAction`（按执行顺序）。动态生成的按钮天然生效。
- 删除：Lua 全局 `onUiAction`、`GameUIHost::setUiActionHandler`、`bindButtonActions`、
  `ui.setText` / `ui.setVisible`、`setMountedText/Visible`、`findMountedWidget` 的字符串三元组查询。
- `ui.get(entryId)`：返回条目根脚本实例的 `self`（没有脚本时返回根句柄），玩法用它调用界面方法。
- 动态实例化：`self:spawn(documentPath, parentHandle[, slot])` 入队，StructuralFlush 挂上并
  激活其中的行为，返回根句柄（生效前句柄为 pending，访问时 WARN）。
- `addToWorld` 挂上的控件同样参与名字索引与动作路由。
- 测试（新套件 `GameUIScriptTest`，需要 Lua 与 GameUIHost 同时在场，不塞进 `GameUIHostTest`）：
  - `WidgetScriptReceivesLifecycleInOrder`（onInit → onStart → onDestroy，期间无 onUpdate）
  - `WidgetScriptTicksOnlyAfterEnablingAndWhileVisible`
  - `WidgetTimerFiresWhileHiddenAndStopsWithItsScript`
  - `WidgetTimerFollowsHostClock`（GameTime host 暂停时计时器不走）
  - `ButtonActionBubblesToNearestScriptThenWorld`
  - `SpawnedButtonRoutesActionWithoutRemount`
  - `FindIsScopedToOwningEntry`（两个条目同名控件互不干扰）
  - `DanglingHandleIsNoOp`
- 验收：GreedSnake 暂不迁移也能编译运行；旧 API 在 S7 与迁移同一提交删除（D8）。
- 分两批交付：
  - **S3a**：控件脚本运行时（生命周期、按需 tick、计时器）、句柄、条目内查找、`ui.get`。
  - **S3b**：按钮动作冒泡、`destroy()` / `spawn`、`addToWorld` 参与索引与激活。
    旧 API（`setUiActionHandler`、`bindButtonActions`、`ui.setText/setVisible`、
    `setMountedText/Visible`）S3 只保留不扩展，删除随 S7。
- S3a 已落地（2026-09-28），实施取舍：
  - `IGameUIBehaviorRuntime : IUIBehaviorActivator` 多一个 `update()`（GameRuntime 定义）；
    `GameUIHost::setBehaviorRuntime` 取代 S2 的 `setBehaviorActivator`，`update` 顺序为
    runtime `update` → 计时器 → 树 tick。实现 `LuaWidgetScripts` 在 `bindGameplayLua` 时装上；
    App 析构先 `setBehaviorRuntime(nullptr)` 再关 Lua。
  - 激活只创建行为、不跑脚本；下一次 UILogic 把新批按（条目 zOrder、激活序）加载，全部 onInit
    后再全部 onStart。onShow / onHide 在 `update` 轮询 `isVisibleInTree` 变化得到。
  - 控件离开树（`onDetached`）即 onDestroy 并取消计时器；树内换父不通知。
  - 计时器：`addTimer(owner, delay, interval, fire)`，重复计时器每次 `update` 最多触发一次；
    `fire` 通过 `self` 身份判断实例是否已被热重载替换，替换后自动失效。
  - 名字索引按条目建（前序、首个同名胜出、重名首次查询 WARN 一次）；句柄的 `find` 找所在条目。
  - 句柄额外提供 `layout()`（立即布局）与 `rect()`；控件失效或已离树时操作 no-op、WARN 一次，
    getter 返回 nil。`Widget:destroy()` 属 S3b。
  - 编辑器 Stop：`stopRuntime` / `stopSimulation` 在 Lua `onStop` 之后按文档重挂当前场景 UI。
    否则被 `onStop` 销毁的脚本不会在下一次 Play 重启，Play 期间改过的文字、显隐也会留到编辑态。
  - 追加测试：`WidgetScriptSeesShowAndHide`、`UiGetReturnsTheEntryScriptOrItsRoot`、
    `RemovingTheRuntimeDestroysRunningScripts`、`TickOrderAppTest.StopRemountsSceneUIFromItsDocuments`。

### S4 — 条目模态、暂停与取消

目标：设置面板这类界面声明「我是模态、我暂停游戏」，框架负责拦截输入、暂停和 Esc。

- `SceneWidgetEntry` 新增挂载意图：`modal`（bool）、`pausesGame`（bool）、`clock`（`game` / `real`），
  替代 `GameUIHost` 整树一个时钟（H16）。
- GameUIHost 维护「可见的模态条目」集合（按 zOrder）：存在时世界输入在 EventPump 被拦截；
  任一可见 `pausesGame` 条目 → 游戏暂停计数 +1，隐藏即 -1（暂停 API 来自 F0）。
- 取消动作：Esc 在 EventPump 先交给最上层可见条目脚本 `onCancel`，再世界脚本 `onCancel`，
  都没消费才走兜底。
- 复用 `ya::ui::EInputBlocking` 语义；是否让条目直接成为 `UIScreen` 由 D3 决定。
- 测试：`GameUIHostTest.ModalEntryBlocksWorldInput`、`PausingEntryStopsGameLogicWhileVisible`、
  `CancelGoesToTopmostVisibleEntryFirst`、`EntryClockGameFreezesWhilePaused`。
- 验收：GreedSnake 设置面板改为 `modal + pausesGame + real`，`Snake.lua` 删除 `settingsOpen`
  与 Esc 逻辑后行为不变（放在 S7 一起验收）。

### S5 — 世界脚本输入与硬编码清理

目标：§1.1 中剩余硬编码项有正确的归属。

- `onKey(key, pressed, repeat)` 作为世界脚本回调，按执行顺序在 EventPump 派发；删除
  `InputRouter::setGameKeyHandler` 与 `input:setKeyHandler`（H3）。命名输入动作（InputMap）
  不在本计划，只保留「取消」一个内置动作。
- `world.spawnSprite(name, { texture, size, tint })`；无 texture 时不设图（不再回落探针图，H1）。
  预制体实例化是否在本计划由 D6 决定。
- `SceneWidgetEntry.rootSlot` 缺省 = 全屏铺满；序列化时等于缺省值则省略（H8）。
- 点锚点布局（H9）：按 D7 结论修正 `UICanvasLayout::resolveChildRect`，把 GreedSnake 文档改回
  「锚点 0.5 + pivot 0.5」的直观写法；与 `gui-anchor-to-slot` 计划对齐，补 `WidgetLayoutTest`。
- 按钮默认文字颜色与底色有足够对比（H10），在默认主题里改，不在内容里补。
- 测试：`SceneWidgetEntryTest.MissingRootSlotDefaultsToFill`、`WidgetLayoutTest.PointAnchorCentersOnAnchor`、
  `GameplayLuaTest.OnKeyConsumedStopsFallback`。

### S6 — 编辑器 authoring

目标：脚本、条目模态/时钟、按钮动作都能在编辑器里编辑，不手改 JSON。

- UI 设计器检查面板：选中控件显示行为列表，增删 `script.lua`、选择脚本路径、属性行
  （复用 `EditorLuaPreview` 的属性发现；设计器仍不运行 `onInit/onUpdate`）。
- 层级面板 Game UI 条目：检查面板编辑 `modal` / `pausesGame` / `clock` / `zOrder`。
- 新建 Game UI（H7）：在内容浏览器当前目录创建、询问名称，`zOrder` 取现有最大值 + 10。
- 内容浏览器：新建 UI 脚本模板（`onInit/onUpdate/onAction/onCancel` 骨架）。
- 验收：编辑器 smoke exit 0；`EditorUIDesignerSessionTest` 扩展「给按钮挂脚本 → 保存 → 重开仍在」；
  预览不运行脚本的既有测试保持绿。

### S7 — GreedSnake 迁移与端到端验收

目标：§0 的验收用例成立，并可无人值守验证。

- 新增 `Content/UI/HUD.lua`、`GameOver.lua`、`Settings.lua`，挂在各自文档根上；
  `Settings` 条目 `modal + pausesGame + clock=real`。
- `Snake.lua` 删除全部 UI 字符串、`settingsOpen`、`setKeyHandler`、`onUiAction` 全局；
  通过 `ui.get("HUD"):setScore(n)`、`ui.get("GameOver"):show(score)` 驱动界面，
  通过 `Script:onUiAction("restart")` 接收重开，通过 `Script:onCancel` 打开设置。
- 自动化（`ScriptApiRegistry`，仅验证用）：`input.inject_key`、`ui.click({entry, widget})`，
  并提供对应 CLI 驱动脚本放 `Script/automation/greedy-snake/`。
- 端到端脚本：开局 → 注入方向键 → 撞墙 → 截图确认 Game Over → 点击 Restart → 确认分数归零 →
  注入 Esc → 确认设置可见且蛇不动 → 点击 Fast → 关闭 → 确认恢复移动。
- 验收：端到端脚本 exit 0；`rg -n '"HUD"|"Score"|"SpeedValue"|settingsOpen|setKeyHandler' Example/GreedSnake/Content/Scripts/Snake.lua` 无结果。

## 5. 依赖与顺序

```text
F0 ──► S1 ──► S2 ──► S3 ──► S4 ──► S7
          │                   ▲
          └──────► S5 ────────┘
S6 依赖 S2/S3/S4 的数据形态，可与 S7 并行
```

- F0 是所有脚本时序的前提，先做。
- 旧字符串 API 的删除随 S7 迁移同一提交（D8），否则中间态 GreedSnake 不能运行。

## 6. 决策门（开工前需确认）

状态：D1、D2、D5、D4 已于 2026-09-28 确认；D8 按建议采纳（用户可推翻）；其余仍为建议（S4 差 D3）。

| # | 问题 | 结论 / 建议 |
| --- | --- | --- |
| D1 | UI 更新从渲染侧移到逻辑 tick 之后，推翻 `game-ui-authoring` P0 的放置理由 | **已确认**：移动；暂停不再跳过 UILogic，原理由不再成立。接受 UI 脚本读到上一帧布局（需要时显式请求立即布局）；`setPresentation` 在 UILogic 之前确定 |
| D2 | 暂停门控范围 | **已确认**：系统按「玩法模拟 / 引擎维护」分组，由系统自己声明：物理、骨骼动画受暂停控制；模型实例化、Transform、LinkageFramework、TaskManager、自动化、文件监视、输入状态更新不受控。模块 onLogic 不受控，需要时自己读 `isPaused()`。`TimerManager` 先审计调用方，玩法与引擎共用则拆成两个时钟。暂停 API 单一写入口、计数语义 |
| D3 | 模态条目是否直接做成 `UIScreen` 进 `ScreenStack` | 不做；条目是持久挂载 + 显隐切换，只复用 `EInputBlocking` 语义。若以后要 push/pop 临时界面，再接 `ScreenStack` |
| D4 | UI 脚本是否每帧 onUpdate、隐藏时是否继续 | **已确认**（参照 UMG / Godot）：默认不 tick，事件驱动；需要时 `self:setTickEnabled(true)` 开启，走现有 `UIBehavior::wantsTick` 协议，只在可见时、按树顺序调用；定时逻辑用 `self:after` / `self:every` 计时器，按 host 时钟推进、隐藏不停、随实例销毁取消 |
| D5 | 世界脚本顺序键与初始化 | **已确认**：`(executionOrder, 场景树前序, 实体内脚本下标)`；脚本声明默认 `executionOrder`，实例可覆盖（仅显式设置时序列化）；排序缓存、结构变化时重排。增加 `onStart`（同批全部 onInit 之后、首次 onUpdate 之前），世界与 UI 一致 |
| D6 | 精灵是否在本计划支持预制体实例化 | 不做；只做参数化 `spawnSprite`，预制体另起计划 |
| D7 | 点锚点语义 | 点锚点轴：以锚点为基准点，`pos = 锚点 + offset − pivot × size`，alignment 只在拉伸轴生效；需与 `gui-anchor-to-slot` 确认后再改 |
| D8 | 旧 API 删除与 GreedSnake 迁移同批提交 | **按建议采纳**：S3 只新增，旧 API 在 S7 与 GreedSnake 迁移同一提交删除 |
| D9 | 独立运行时 Esc 兜底是否仍退出程序 | 保留为最后兜底；游戏脚本或模态条目消费后不触发 |

## 7. 不在本计划

- 命名输入动作系统（InputMap）；本计划只内置「取消」。
- 文档内的 Reactive 数据绑定（ViewModel 写法）；等 S3 句柄稳定后另起。
- 世界空间 UI、UI 动画轨道（clip player）。
- 场景可编辑性：墙、相机取景作为场景数据 / 组件（固定场地取景相机模式）另起计划；
  本计划不改 `Snake.lua` 里的取景与墙生成。
- 预制体实例化（D6）。
- `GameUIHost` 拆分（`game-ui-authoring` P2 `scene_ui_composition_split`）。
- 反射自动绑定 Lua。

## 8. 风险

- **单一 `sol::state` 共享全局**：脚本仍可能写全局变量互相污染。S1 不改为独立环境
  （`require` 与共享工具库依赖全局），只在文档里约定「脚本返回 local 表」，并在热重载后检查
  新增全局写入，发现时 WARN。
- **弱句柄的生命周期**：控件在 StructuralFlush 前被父级销毁时，pending spawn 需要随之取消。
- **编辑器 PIE 与 Stop**：Stop 时 UI 脚本实例必须在 Lua 状态销毁前 `releaseLuaHandles`，
  沿用 `LuaScriptComponentLifetimeTest` 的约束。S3a：Stop 后按文档重挂场景 UI，下一次 Play
  从作者态开始（`TickOrderAppTest.StopRemountsSceneUIFromItsDocuments`、
  `GameUIScriptTest.RemovingTheRuntimeDestroysRunningScripts`）。
- **性能**：按名字索引在挂载时建一次；每帧排序世界脚本可缓存，结构变化时失效。
- **UI 读上一帧布局**：UILogic 先于布局，依赖本帧几何的脚本若忘记请求立即布局会差一帧；
  S3 句柄文档与模板里写明。
- **系统类别漏标**：新系统默认类别要选「引擎维护」还是「玩法模拟」。F0 取「必须显式声明」
  （纯虚或构造参数），避免新系统静默落入错误分组。
