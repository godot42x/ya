# 2D Gameplay

俯视 2D 玩法（RPG Maker 式）在 YA 引擎里的固定约定。示例事实源：
`Example/2DRpgPrototype/`（内容）与本 skill（规则）。计划历史在
`.agent/plan/rpg-prototype/`。

## 何时读

- 写 2D 玩法脚本（行走、交互、遮挡）或给它们加引擎能力时。
- 判断某个 2D 玩法需求该落在脚本、组件还是渲染层时。

## 角色前后遮挡（画家顺序 + y-sort）

精灵之间只有画家顺序，比较函数在 `Scene2D/SpriteDrawOrder.h`。键是
`(layer, ySortRank, yKey, order, tiebreak)`，全升序，后画的在上面。
`Transform.z` 不参与这个键：它只拿来和 3D 不透明几何做深度测试（只测不写）。

- `layer`：`Sprite2DComponent.layer`，tile 子层是 `TilemapComponent.layer + TilemapLayer.layerOffset`。
- `ySortRank`：`bYSort` 关为 0、开为 1。同一 layer 里没开 y-sort 的先画。
- `yKey`：开了 y-sort 取 `-sortY`，否则恒为 0。`sortY` 是排序点的世界 y，texel 吸附之后。
  排序点是 pivot：精灵用实体世界位置，tile 用该格中心（pivot 0.5, 0.5）。世界 y 越大
  （屏幕越靠上）越早画，越靠屏幕下方越晚画、盖住上面的。
- `order`：`Sprite2DComponent.sortOrder`；tilemap 子层用 `layerIndex`。
- tiebreak：实体 id，然后 tile 的行优先提取顺序。

示例：Ground / Decor / Overlay 三个子层都在组件 `layer` 0。Overlay 的 `layerOffset = 1`，
所以盖住角色。Player / Npc / Sign / Chest 开 `bYSort`，Door 不开。Door 的 `sortOrder`
是 1：地面子层的 order 是 0，装饰子层是 1，门要画在地面之上；和装饰层 order 相同时
tilemap 的实体 id 更大，装饰仍然盖住门。角色和家具的 `Transform.z` 是常量 0.1，落在
Decor `zOffset` 0.03 和 Overlay 0.2 之间，3D 遮挡语义和从前的角色层一致。脚本不再按 y 写 z。

## 脚与 pivot

`Sprite2DComponent.pivot` 是归一化锚点，(0, 0) 为 quad 左下、(1, 1) 为右上，pivot 落在实体位置上。角色精灵高于一格（示例 1×1.5）时，Player / Npc 用 `(0.5, 1/3)`：实体放在格子中心，quad 底边在中心下方 0.5，正好落在格子底边。画面上的脚由实体位置和 pivot 决定，脚本不再把实体从格子中心抬高。箱子、告示牌、门保持默认 `(0.5, 0.5)`（中心压在实体上）。

## 帧动画

- 精灵换帧用 `SpriteAnimationComponent`（`Scene2D/SpriteAnimationComponent.h`，与 `Sprite2DComponent` 同实体），脚本不再自己算 `uvRect`。
- 切格和片段在共享的 `.yaanim.json`（`SpriteAnimationSet`）里，不在组件上。文件字段：`atlas`、`columns`、`rows`、`clips`（`name`、`frames`、`fps`、`bLoop`）。图像按 `columns × rows` 均匀切格，帧下标 = `row * columns + column`，行主序、从左上开始。`frames` 可重复，如走路 `{0,1,2,1}`。`fps` 缺省 8、`bLoop` 缺省 true；写了但类型不对仍然非法。`atlas` 是这套动画拥有的贴图路径，可空（空则不写进文件）。示例：`Content/Animations/Hero.yaanim.json`（3×4，走+站）、`Npc.yaanim.json`（只站）、`Chest.yaanim.json`（12×11，closed/open）。
- 组件只存 `animation`（`SpriteAnimationSetRef`，序列化形状与 `textureRef` 相同）和 `clip`（游戏开始时自动播的片段名）。多个实体引用同一文件、共享一个槽；改这一份资产，所有引用一起生效。
- 脚本：`local anim = self.entity:getSpriteAnimation()`，`anim:play("walk_left")`（每帧都调没关系：正在播的同名片段不重启；未知名字返回 false 并告警）、`anim:stop()`、`anim:setFrame(i)`（停下并显示某一帧）、`anim:isPlaying()`、`anim:currentClip()`。资产没加载时 `advance` 不动、`play` 返回 false，告警只打一次。
- `atlas` 非空时，显示帧会把同实体 `Sprite2DComponent.image` 的路径写成 atlas，并写 `uvRect`。只改路径，保留 `samplerConfig`。路径和贴图槽 generation 都没变时不重新 rebind。`atlas` 为空是有意的：只写 `uvRect`，贴图继续用 Sprite2D 自己的，同一套切格可以套不同皮肤。
- `play` / `setFrame` 立即写，脚本同一帧就看到。时间由 `SpriteAnimationSystem` 推进（Simulation 组，游戏暂停时停；只在 runtime / simulation 模式推进播放头）。加载完成、Inspector 改 `animation` / `clip`、以及动画集槽 generation 变化时，会套用当前帧；没在播就套 `clip` 的第一帧，所以视口不播放时也是对的图和对的帧。
- 带 atlas 的动画组件驱动的 `Sprite2D.image` 路径和 `uvRect` 仍写进场景文件，存的是派生值。
- 正在播的片段以名字为准。槽 `generation` 变了（资产重载）就按名字重新找下标，不沿用旧下标。Inspector 改 `animation` 会清掉运行态，并立刻按新的 `clip` 显示初始帧。
- 一次性片段（`bLoop = false`）停在最后一帧，`isPlaying()` 变 false；再 `play` 同一个片段会重来。

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

## 像素完美相机

俯视像素画的取景在 `CameraComponent` 上，脚本不再自己算 zoom：

- `_pixelPerfect` 默认关。打开后正交半高由 `resolveCameraViewFraming` 按该 View 的**设备像素**高度（`outputRect.extent`，等于逻辑点乘 `SceneViewDesc::pixelDensity`，不是窗口逻辑尺寸）计算：`zoom = max(1, floor(viewHeight / _referenceHeightPx))`，半高 = `viewHeight / (2 * _pixelsPerUnit * zoom)`，半宽跟真实宽高比。zoom 是整数且不低于 1：视口比参考矮时少看一些世界，一个 texel 至少占一个设备像素。2x 密度下 zoom 翻倍。
- 只按高度取 zoom。像素是正方形，半宽跟真实宽高比走，横向的 texel→像素比和纵向相同，不另设参考宽度。pixel perfect 不用 `_fixedAspectRatio`，否则横向会被拉开。
- 相机眼在 `buildCameraRenderMatrices` 里吸附到设备像素（不写 Transform）。宽或高为奇数时该轴偏半个设备像素，texel 边落在像素边上。偶数尺寸相位为 0。
- 精灵 / tile 的 `worldCenter` 在 `RenderFrameExtractor::extractSceneSnapshot` 吸附到 `1 / _pixelsPerUnit`。候选被所有 View 共享；整数 zoom 的设备像素是 texel 的细分，再叠加每 View 的眼吸附，精灵不会落在半个设备像素上。
- `world.viewSize()`：主相机是 pixel-perfect 正交时返回 `(halfWidth, halfHeight)` 世界单位，否则仍是像素尺寸。`world.viewAspect()` 仍是宽/高，pixel perfect 时等于 `halfWidth / halfHeight`。跟随脚本用它钳制地图。
- 编辑器正交 XY 视口是自由相机，不读这个开关。

Tiny Town（Town / House / TownLarge）：`_pixelsPerUnit = 16`，`_referenceHeightPx = 192`（12 格）。1280×720 时 zoom = 3，半高 7.5。

## 边界

- 不引入 `Node2D` / `Transform2D` / `Camera2D`；2D 对象 = `Node3D` +
  `TransformComponent` + `Sprite2DComponent`。
- 精灵渲染管线（合批、纹理表、纯 2D 图）归 `rpg-prototype`（2026-10-02 合并了
  `scene-2d-world-and-game-ui`）；动渲染前先过 P0 契约（`P0-contract-matrix.md`）。
- 编辑器预览不运行玩法脚本；对话框等 Game UI 走 yaui（`gui-framework`）。
