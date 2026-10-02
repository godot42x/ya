# 2D Gameplay

俯视 2D 玩法（RPG Maker 式）在 YA 引擎里的固定约定。示例事实源：
`Example/2DRpgPrototype/`（内容）与本 skill（规则）。计划历史在
`.agent/plan/rpg-prototype/`。

## 何时读

- 写 2D 玩法脚本（行走、交互、遮挡）或给它们加引擎能力时。
- 判断某个 2D 玩法需求该落在脚本、组件还是渲染层时。

## 角色前后遮挡（z 约定）

不透明精灵 alpha 裁剪并写深度（`Sprite2DWorld.slang`），所以前后遮挡用深度表达：

- **角色 z = 图层基准 − y × ε**。世界 y 越小（屏幕越靠下）z 越大，越靠近相机，
  遮住后面的角色。基准取 tilemap 的 Decor 与 Overlay 层之间；ε 取小值保证
  `地图高 × ε` 仍留在两层之间（示例：基准 0.1、ε 0.005、地图 20 格）。
- 约定收在示例 `Content/Scripts/Actor.lua`（`Actor.zFor`），Player/Npc/Sign 共用。
  这是在补"没有 y-sort"的洞：`rpg-prototype` 评审步骤 5 把画家顺序（layer → y → order）
  做成 2D 合批的排序键后删除，届时脚本不再算 z。

## 地图即碰撞权威

- 通行、格子的定义全部问 tilemap 组件，不问物理：`worldToCell` / `cellToWorld` /
  `isSolid` / `bounds`（反射方法），加 `entityAt(x, y[, except])`（原生方法，GameRuntime
  注册）。**每个场景的行走地图实体都叫 `TilemapGround`**——Player.lua 按这个名字找地图。
- `isSolid` 出界算阻挡；`entityAt` 返回「格子里第一个 actor」——actor = 同时有
  Sprite2DComponent 与 LuaScriptComponent 的实体。相机、纯变换实体永远不算。
- 玩法脚本（Player.lua）组合三者判进格：`isSolid(...)`，以及
  `occupant:call("blocksEntry") ~= false`（门作答 false 可穿行；无作答 = 阻挡）。
  走上新格时用 `entityAt(x, y, self.entity)` 排除自己再触发其 `onPlayerEnter`（门、
  陷阱都是这个入口）。引擎不在 isSolid 里掺实体感知——tilemap 组件看不见场景，查询的
  注册点在 GameRuntime `GameplayScriptFunctions.cpp`（能同时看到 Scene 与组件）。
- 手写场景注意：**cell 值 = tile 序号 + 1**（0 = 空）；TilesetRef 的 JSON 形状是
  `{"__base__": {"AssetRefBase": {"_path": ...}}}`，不是 TextureSlot 那套字段。

## 场景转移（R3 约定）

- 换图只有一条玩法路：`world.loadScene(path, spawnName)`。它只排队，在**帧尾结构变更
  阶段**执行（转移会停掉全部脚本，不能内联）；转移保持 play 会话——app 状态、UI host、
  `Persist` 常驻表都不动。
- 跨场景状态只进 `Persist` 全局表（按实体名等做键）；退出 play（stopRuntime /
  stopSimulation）换新表，转移不碰它。不要依赖「全局变量碰巧没被清」。
- 出生点：场景根上一个命名 Node3D；引擎把名为 `Player` 的实体放到标记的 x/y 上，
  z 归角色自己。场景之间主角不搬家，每张图自带 Player 实体。

## 脚本互调

- 交互 = 「玩家脚本找到目标实体 → 直接调它脚本上的函数」：
  `target:call("onInteract", ...)`。没有中心事件总线。
- 语义：实体上第一个定义了该名字的已加载脚本作答；未定义返回 nil；目标脚本
  出错抛 ScriptError 到调用方（错误不能伪装成安静地返回 nil）。
- 实现入口：`LuaScriptingSystem::callNamed`（实例面）+ `callEntityScript`
  （实体面，`ECS/Systems/LuaScriptingSystem.cpp`），以无捕获原生方法挂在
  Entity 类型上，Lua / JS 共用。

## 对话框（Game UI 条目）

- 对话框是一个 autoMount 的 Game UI 条目（`widgetEntries`），根 panel 挂
  `script.lua` behavior 并常驻 Hidden；脚本提供 `say(lines, onDone)` 与 `busy()`。
- 玩法侧（NPC / 玩家脚本）经 `ui.get("Dialogue"):say(...)` 说话；**移动锁由玩法
  状态控制**——玩家脚本每帧轮询 `dialogue:busy()`，game-ui S4 的 modal 条目落地后
  才迁移。`ui.get` 在条目脚本加载完成前的头几帧回落成控件句柄，轮询侧必须
  容错（`dialogue.busy ~= nil` 再调用）。
- 一次按键只做一件事：对话框忽略「开框那一帧」的确认键（`time:getFrameIndex()`），
  玩家侧触发交互后要求确认键完全松开才再武装（re-arm），否则同一次按键会同时
  推进对话又再次触发交互。
- Lua 全局是小写 `time`（`LuaTimeApi` 实例）与 `input` / `log`；大写 `Time` 是
  usertype 表，不能在其上调方法。

## 边界

- 不引入 `Node2D` / `Transform2D` / `Camera2D`；2D 对象 = `Node3D` +
  `TransformComponent` + `Sprite2DComponent`。
- 精灵渲染管线（合批、纹理表、纯 2D 图）归 `rpg-prototype`（2026-10-02 合并了
  `scene-2d-world-and-game-ui`）；动渲染前先过 P0 契约（`P0-contract-matrix.md`）。
- 编辑器预览不运行玩法脚本；对话框等 Game UI 走 yaui（`gui-framework`）。
