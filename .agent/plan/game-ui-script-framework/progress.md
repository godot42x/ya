# Progress

## 2026-09-28 — 立项

完成：

- 盘点硬编码与缺口，结果写入 `plan.md` §1（H1–H17 与游戏侧弥补代码）。
- 核对的关键事实：
  - `App::_bPause` 全仓库无写入点，暂停不存在；暂停时 `tickLogic` 整段跳过（含输入状态、文件监视、模块）。
  - `LuaScriptingSystem::onUpdate` 按 EnTT 存储顺序遍历，`onInit` 在同一循环里懒调用。
  - UI `update` 位于 `RuntimeRenderContext::buildGameRenderFrame`，依赖 display root 存在。
  - 按钮动作只在 `GameUIHost::setMountedRoots` 时绑定一次。
  - `UICanvasLayout::resolveChildRect` 点锚点轴取整个父尺寸作为对齐区域。
  - `game-ui-authoring` 已约定：不引入 UUserWidget 平行类型、不造中心事件总线、设计器预览不 tick。
- 确定方向：脚本实例与宿主解耦；GUI 只存不透明行为描述；预览不激活；帧顺序成为契约。

未开始：F0–S7 全部。

待确认：`plan.md` §6 决策门 D1–D9。F0 依赖 D1、D2、D5；S3 依赖 D4、D8；S4 依赖 D3；S5 依赖 D6、D7、D9。

下一步：用户确认决策门后开始 F0。

## 2026-09-28 — 决策门 D1 / D2 / D5 确认

用户确认：

- D1：UI 更新移到 GameLogic 之后的 UILogic；接受 UI 脚本读到上一帧布局，句柄提供显式立即布局；
  `setPresentation` 在 UILogic 之前确定。
- D2：修正原建议「系统整组受暂停」——`_systems` 里同时有玩法模拟（Physics、SkeletonAnimation）
  与引擎维护（ModelInstantiation、Transform、LinkageFramework）。改为系统显式声明类别，只有玩法
  模拟受暂停；模块 onLogic、TaskManager、自动化、文件监视、输入状态更新不受暂停；`TimerManager`
  在 F0 审计后决定是否拆时钟；暂停 API 单一写入口、计数语义。
- D5：顺序键 `(executionOrder, 场景树前序, 实体内脚本下标)`；脚本声明默认值、实例可覆盖；
  增加 `onStart`，每帧 onInit → onStart → onUpdate 三遍，onUpdate 中新建的实例下一帧进入。

计划同步：`plan.md` §3 帧顺序与生命周期表、§4 F0 条目与测试、S3 句柄、§6、§8；
`feature_matrix.json` decisions 与 F0 features。

仍待确认：D3、D4、D6、D7、D8、D9（不阻塞 F0）。

下一步：开始 F0。

## 2026-09-28 — F0 帧顺序、暂停与结构变更时机

完成：

- 暂停：`App::_bPause`（无写入点）→ `_gamePauseDepth` + `pushGamePause` / `popGamePause`，
  多弹一次 WARN 且不下溢；`isPaused()` = 深度 > 0。`iterate` 不再整段跳过 `tickLogic`。
- 系统类别：`ESystemTickGroup{Engine, Simulation}`，`App::_systems` 存 `FRegisteredSystem`，
  `AppLifecycle` 注册时显式给出；暂停只跳过 Simulation（SkeletonAnimation、Physics）。
- 世界脚本：先快照句柄再加载，排序键 (executionOrder, 树前序, 句柄, 下标)，
  onInit 全部 → onStart 全部 → onUpdate 全部，每步按句柄/下标重新解析；`ScriptInstance`
  增加 `onStart`、脚本默认 `executionOrder`、可选实例覆盖（仅显式设置时序列化）。
  `LuaRuntimeServices::readScript` 可注入脚本源（测试用，默认走 VFS）。
- UILogic：`tickUILogic` 在模块 onLogic 之后推进 `GameUIHost`；`buildGameRenderFrame`
  只留 `setPresentation` + `buildSnapshot`。
- StructuralFlush：`Scene::queueDestroyNode` / `flushQueuedDestroys(beforeDestroy)`；
  `world.destroyEntity` 入队，flush 时对整棵子树调 `LuaScriptingSystem::onEntityDestroying`
  （onDestroy + 释放 Lua 句柄）再销毁。
- `TimerManager` 审计：唯一调用方是编辑器 `delayCall`，常跑，不拆时钟。
- `Class.lua` 根类 `__index` 自引用死循环修复；`ScriptBase` 补默认 `onStart`。

偏离计划：见 `plan.md` F0「实施偏离」（类别在注册处声明、保持现有步骤顺序、presentation
留渲染侧、不缓存排序、只有玩法 Lua 销毁入队、热重载跳过未加载实例）。

验证：

- `ya-testing` 全量 1345 通过；`TickOrderTest.*` / `TickOrderAppTest.*` 10 个通过，
  `ScriptBaseInstance…` 在回退 `Class.lua` 时复现 `'__index' chain too long`。
- 冒烟 `--exit-after-frame=60`：GreedSnake runtime、HelloMaterial runtime、GreedSnake editor
  均 exit 0；GreedSnake 无 Error。
- HelloMaterial 两条 sol2 panic（`PlayerCamera.lua` 的 `8.0` / `45.0` 被 `ScriptBase:properties`
  用 `% 1 == 0` 推断为 int，`as<int>` 失败被捕获）为既有问题，与 F0 无关，未修。

已知边界：同一帧内删除实体上的脚本会使后续下标错位（解析时按下标，最坏跳过/错调一个实例一帧）；
目前无调用方，S1 统一脚本宿主时一并处理。

下一步：S1（与宿主无关的 Lua 脚本运行时）。

## 2026-09-28 — S1 与宿主无关的 Lua 脚本运行时

完成：

- `LuaScriptingSystem` 增加与宿主无关的实例接口与活实例登记表：
  `load(instance, host)` / `call(instance, callback, dt)` / `destroy` / `destroyAll` / `reloadScript(path)` /
  `liveCount()`；不另设 runtime 类（系统全局唯一，拆分只多一层转发）。
- `ILuaScriptHost`：`resolve(id)` 找回所承载实例（找不到即视为已消失并剔除）、`bindSelf(self)`
  注入宿主字段；每次回调前重绑。
- `LuaScriptInstance` / `LuaScriptProperty` 从 `LuaScriptComponent` 抽出（`LuaScriptComponent.cpp`
  更名为 `LuaScriptInstance.cpp`），新增 `runtimeId`；组件序列化与克隆不变。
- `LuaScriptingSystem`：实体宿主 `FEntityScriptHost`（按 active scene + entt 句柄 + id 解析）；
  world 帧按 id 重新解析；`onStop` = `destroyAll` + 重置行状态；热重载遍历登记表，覆盖所有宿主。
- 调用点改名：`EditorLuaPreview`、`EditorLuaScriptSection`、`GameplayLua`（`scripting.lua()`）、测试。

偏离与取舍：见 `plan.md` S1「已落地」。F0 的「同帧删除脚本下标错位」边界已消除。

验证：

- `ya-testing` 全量 1355 通过；`LuaScriptHostTest.*` 5 个、`TickOrderTest.*` / `TickOrderAppTest.*`
  12 个、`LuaScriptComponentLifetimeTest.*` 3 个。
- 冒烟 `--exit-after-frame=60`：GreedSnake runtime、HelloMaterial runtime、GreedSnake editor exit 0；
  无关闭时实例残留警告；HelloMaterial 仍只有既有的两条属性类型 panic。

未做：`onEnable` / `onDisable` 仍只绑定不调用（既有状态，非 S1 范围）。

下一步：S2（GUI 通用行为描述，与 Lua 无关）。

## 2026-09-28 — 决策门 D4 确认

用户确认（替换原建议「隐藏仍每帧 onUpdate」）：UI 脚本参照 UMG / Godot，默认不 tick、走事件驱动；
需要每帧时 `self:setTickEnabled(true)`，沿用 `UIBehavior::wantsTick` 协议（只在可见时、树顺序）；
定时逻辑用 `self:after` / `self:every`，计时器队列归 `GameUIHost`，按 host 时钟推进、隐藏不停、
随实例销毁取消。

计划同步：`plan.md` §3 UILogic、生命周期表、S3 驱动方式与测试、§6 D4、H15；`feature_matrix.json`
decisions 与 S3 features（`widget_script_tick_opt_in`、`widget_script_timers`）。

仍待确认：D3（S4）、D6 / D7 / D9（S5）、D8（S3）。

## 2026-09-28 — S2 GUI 通用行为描述

完成：

- `UIDocument` 节点新增可选 `behaviors: [{type, data}]`，`fromWidget` / `instantiate` / `toJson` /
  `fromJson` 往返；`UIElement::_behaviorSpecs` 承载（非反射、不参与绘制与布局）。
- `GUI/Widgets/UIBehaviorSpec.h`：`FUIBehaviorSpec`、`FUIBehaviorActivation`、`IUIBehaviorActivator`
  （GUI 定义、不实现）、`activateBehaviorSpecs`。
- `mountSceneAutoMountEntries` 增加 activator 参数：`DefaultGameUIController` 传
  `GameUIHost::getBehaviorActivator()`，`EditorGameUIPreview` 传 null。

偏离与取舍：见 `plan.md` S2「已落地」。

验证：

- `ya-testing` 全量 1360 通过；`ya-gui-closure-test` 602 通过、`ya-gui-widgets-test` 的
  `UIDocumentTest.*` 22 个通过（两者都不链 Scene / ECS / Lua）。
- 依赖审计：未改任何 `xmake.lua`；新文件只引 `Core/Common/Types.h`、nlohmann、`UIElement.h`。
- GUIWorkbench `--smoke-actions` PASS、exit 0；GreedSnake runtime / editor `--exit-after-frame=60` exit 0、无 Error。

下一步：S3 需先确认 D8。

## 2026-09-28 — S3a 控件脚本运行时与句柄

决策：D8 按建议采纳（S3 只新增，旧 API 在 S7 与迁移同一提交删除）。S3 拆成 S3a / S3b。

完成：

- `IGameUIBehaviorRuntime`（激活 + `update`）取代 S2 的激活器入口；`LuaWidgetScripts` 为
  `script.lua` 创建 `LuaWidgetScriptBehavior`，UILogic 中按批 onInit → onStart，轮询 onShow / onHide，
  `self:setTickEnabled` 按需 tick，`self:after` / `self:every` 走 `GameUIHost` 计时器。
- 类型化弱句柄（Widget / Text / Button / Image / Border），条目内名字索引与 `find`，`ui.get(entryId)`。
- `LuaScriptingSystem::invoke`：带参数、取布尔返回值的回调调用（onShow / onHide，S3b 的 onAction 复用）。
- 编辑器 Stop 后按文档重挂场景 UI。

偏离与取舍：见 `plan.md` S3「S3a 已落地」；测试放新套件 `GameUIScriptTest`。

验证：

- `ya-testing` 全量 1370 通过（`GameUIScriptTest.*` 9 个、`TickOrderAppTest.StopRemountsSceneUIFromItsDocuments`）。
- GUIWorkbench `--smoke-actions` PASS；GreedSnake runtime / editor `--exit-after-frame=60` exit 0、无 Error。

未做：旧 API 仍在（D8）；按钮冒泡、`destroy` / `spawn`、`addToWorld` 激活属 S3b。

下一步：S3b。

## 2026-09-28 — S3b 按钮动作冒泡与结构变更

完成：

- GUI 动作出口：`UIBehavior::onAction`、`WidgetTree::emitAction` / `setActionSink`；`UIButton`
  激活后发出 `_action`。`bindButtonActions` 与 `setUiActionHandler` 删除，改为
  `GameUIHost::setWorldActionHandler`。
- 路由：源控件 → 祖先上的 UI 脚本 `onAction` → 世界脚本 `Script:onUiAction`
  （`LuaScriptingSystem::invokeWorld`，按执行顺序）→ 旧全局 `onUiAction`（S7 删）。
- `Widget:destroy()`、`self:spawn(documentPath, parent)` 在 StructuralFlush 生效；spawn 的行为按
  父节点所在条目激活；`addToWorld` 控件自成条目并激活行为。

偏离与取舍：见 `plan.md` S3「S3b 已落地」（pending 句柄可用、`spawn` 暂不带 slot）。

验证：

- `ya-testing` 全量 1375 通过；`ya-gui-closure-test` 603 通过。
- GUIWorkbench `--smoke-actions` PASS；GreedSnake runtime / editor、HelloMaterial runtime
  `--exit-after-frame=60` exit 0、无 Error。

下一步：S4 需先确认 D3（模态条目不做成 `UIScreen`，只复用 `EInputBlocking` 语义）。

## 2026-09-28 — 性能检查点（F0 / S3 引入的三处每帧开销）

用户反馈实现性能低，决定只修本计划引入的三处，`UIBehavior` 拆分与 tick 注册表另起计划。

完成：

- 世界脚本顺序：`Node` 树修订号 + `LuaScriptingSystem` 前序排名缓存，取代每帧逐实体求树路径（补上 D5 的缓存）。
- onShow / onHide：`WidgetTree` 可见性修订号，未变化时不比较。
- UI 计时器：最小堆，取代每帧全量扫描。
- 新计划 `ui-behavior-capabilities/`（未开始，待确认）。

偏离与取舍：见 `plan.md`「性能检查点」（同一次 update 内计时器改按到期先后触发）。

验证：

- 临时基准（debug）：世界 400 脚本 2468 → 604 µs/帧；UI 300 脚本 + 300 计时器 24.7 → 10.3 µs/帧。
- `ya-testing` 全量 1379 通过；`ya-gui-closure-test` 604 通过。
- GUIWorkbench `--smoke-actions` PASS；GreedSnake runtime / editor、HelloMaterial runtime
  `--exit-after-frame=60` exit 0、无 Error。

下一步：S4（D3 已确认：不做 `UIScreen`）。

## 2026-09-28 — S3b 按钮动作冒泡被 `ui-behavior-capabilities` C1c 取代

- 删除 `_action` / `emitAction` / `setActionSink` / `IUIActionHandler` / 世界 sink / `invokeWorld` /
  `onUiAction` / `Button.action`；按钮广播 `onClicked`，脚本直接监听（C1d 起为 `btn.onClicked:add(self, fn)`）。
- H2（全局 `onUiAction` 覆盖、Stop 后残留）随之消失；GreedSnake 已改为直接监听。
- plan.md §0 原则 5、§1.3、§3 帧顺序与回调表、S3 / S6 / S7 相应改写；S3 落地记录保留并标注。

下一步不变：S4（排在 `ui-behavior-capabilities` C2 / C3 之后）。
