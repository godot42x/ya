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
  只有当第三个以上玩法脚本各自重复这段计算时，才评估下沉成组件字段；那属于
  `scene-2d-world-and-game-ui` 的 authored sprite 能力，不在玩法计划里做。

## 地图即碰撞权威

- 通行、格子的定义全部问 tilemap 组件，不问物理：`worldToCell` / `cellToWorld` /
  `isSolid` / `bounds`（反射方法），加 `entityAt(x, y)`（原生方法，GameRuntime 注册）。
- `isSolid` 出界算阻挡；`entityAt` 返回「格子里第一个 actor」——actor = 同时有
  Sprite2DComponent 与 LuaScriptComponent 的实体。相机、纯变换实体永远不算。
- 玩法脚本（Player.lua）组合两者判进格：`isSolid(...) or entityAt(...) ~= nil`。
  引擎不在 isSolid 里掺实体感知——tilemap 组件看不见场景，查询的注册点在
  GameRuntime `GameplayScriptFunctions.cpp`（能同时看到 Scene 与组件）。

## 脚本互调

- 交互 = 「玩家脚本找到目标实体 → 直接调它脚本上的函数」：
  `target:call("onInteract", ...)`。没有中心事件总线。
- 语义：实体上第一个定义了该名字的已加载脚本作答；未定义返回 nil；目标脚本
  出错抛 ScriptError 到调用方（错误不能伪装成安静地返回 nil）。
- 实现入口：`LuaScriptingSystem::callNamed`（实例面）+ `callEntityScript`
  （实体面，`ECS/Systems/LuaScriptingSystem.cpp`），以无捕获原生方法挂在
  Entity 类型上，Lua / JS 共用。

## 边界

- 不引入 `Node2D` / `Transform2D` / `Camera2D`；2D 对象 = `Node3D` +
  `TransformComponent` + `Sprite2DComponent`。
- 精灵渲染管线（合批、纹理表、纯 2D 图）归 `scene-2d-world-and-game-ui`；玩法
  计划只产出数据与决策（R4）。
- 编辑器预览不运行玩法脚本；对话框等 Game UI 走 yaui（`gui-framework`）。
