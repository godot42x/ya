# 2D Gameplay

俯视 2D 玩法（RPG Maker 式）在 YA 引擎里的固定约定。示例事实源：
`Example/2DRpgPrototype/`（内容）与本 skill（规则）。计划历史在
`.agent/plan/rpg-prototype/`。

## 何时读

- 写 2D 玩法脚本（行走、交互、遮挡）或给它们加引擎能力时。
- 判断某个 2D 玩法需求该落在脚本、组件还是渲染层时。

编辑器 play 里，输入要先点一下 viewport，input router 才会切到游戏。

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
  注册）。行走地图实体默认叫 `TilemapGround`：`Player.lua` / `FollowCamera.lua` 的 `mapName` 用这个默认，场景 `propertyOverrides` 可改；Town / House / TownLarge 不写覆盖。
- `isSolid` 出界算阻挡；`entityAt` 返回「格子里第一个 actor」——actor = 同时有
  Sprite2DComponent 与 LuaScriptComponent 的实体。相机、纯变换实体永远不算。
- 玩法脚本（`GridMover.lua`）组合三者判进格：`isSolid(...)`，以及
  `world.scriptOf(occupant)` 上的 `blocksEntry`。无脚本、无该方法、或返回值不是
  false 都阻挡；只有 `s:blocksEntry() == false` 才可穿行。走上新格时用
  `entityAt(x, y, self.entity)` 排除自己，再对 `scriptOf` 得到的表调
  `onPlayerEnter`（门、陷阱都是这个入口；没有该方法就不调用）。引擎不在 isSolid
  里掺实体感知——tilemap 组件看不见场景，查询的注册点在 GameRuntime
  `GameplayScriptFunctions.cpp`（能同时看到 Scene 与组件）。
- 手写场景注意：**cell 值 = tile 序号 + 1**（0 = 空）；TilesetRef 的 JSON 形状是
  `{"__base__": {"AssetRefBase": {"_path": ...}}}`，不是 TextureSlot 那套字段。

## 场景转移（R3 约定）

- 换图只有一条玩法路：`world.loadScene(path, spawnName, actorName)`。`actorName` 可省。
  它只排队，在**帧尾结构变更阶段**执行（转移会停掉全部脚本，不能内联）；转移保持 play
  会话——app 状态、UI host、`Persist` 常驻表都不动。
- 自动化命令 `scene.load` 不是这条路。它保持**调用前的运行模式**：play 里调用会退出
  当前局，再在新场景上重新 `startRuntime` / `startSimulation`（脚本重新 init/tick，
  `Persist` 随退局清空）；编辑态调用只换场景、留在编辑态。编辑器里打开 `.scene.json`
  仍是「退出 play、留在编辑态」，走 `AppSceneServices::loadScene`，不走 `scene.load`。
- 跨场景状态只进 `Persist` 全局表（按实体名等做键）；退出 play（stopRuntime /
  stopSimulation）换新表，转移不碰它。不要依赖「全局变量碰巧没被清」。
- 存档是 `world.save(path)` / `world.load(path)`。文件只记场景路径和 `Persist`。保存前每个已加载的世界脚本若有 `onSave`，就在那里把要留下的状态写进 `Persist`；引擎不扫描实体。`load` 在帧尾换上那张场景，不走出生点，再换上 `Persist`。下一帧 `onInit` 里 `world.saveRestored()` 为真，脚本据此套回自己的状态。场景转移不置这个标志。路径相对项目根，也可以是绝对路径。同一帧里后到的 `loadScene` 或 `load` 覆盖先到的。
- 出生点：场景根上一个命名 Node3D。有 `spawnName` 时，引擎把目的地场景里名为
  `actorName` 的实体放到标记的 x/y 上，z 归角色自己。名字由调用方传入：Door 的
  脚本属性 `actorName` 默认 `"Player"`，场景 `propertyOverrides` 可改。省略或空
  名字不移动任何实体。场景之间主角不搬家，每张图自带自己的角色实体。

## 脚本互调

- 玩法 Lua 直接拿对方的 self：`local s = world.scriptOf(entity[, path])`。
  返回的是那张活表，所以写错方法名当场 `attempt to call nil`；没有已加载脚本
  返回 nil。可选 `path` 按规范化后的脚本路径过滤。返回前会把 `self.entity`
  重新绑到这个实体，和 `call` 一样。
- 表只在当次使用，不要存过帧。热重载保持同一个 runtimeId，但整张 self 换成新表；
  手里的旧表不再是对方的脚本。
- 直调优先。`entity:call("onInteract")` 只留给协议问询和 JS：目标上第一个定义了
  该名字的已加载脚本作答，未定义返回 nil，目标出错抛到调用方。玩法侧
  （`Player.lua` 的 `interact`、`GridMover.lua` 的 `blocksEntry` /
  `onPlayerEnter`）走 `scriptOf`。可选方法先判空再调，避免把「没有这个函数」
  变成错误。
- `call` 的实现入口仍是 `LuaScriptingSystem::callNamed` 与 `callEntityScript`
  （`Script/Lua/LuaScriptingSystem.cpp`），以无捕获原生方法挂在 Entity 上，
  Lua / JS 共用。`scriptOf` 登记在 `GameplayScriptFunctions.cpp`，Lua 侧把
  `ScriptSelfHandle` 推回那张表。

## 对话框（Game UI 条目）

- 对话框、背包、任务日志都是 autoMount 条目。卡片是 `engine.border` 里的纵向
  `engine.container`。背包贴在右上，日志贴在左上再往下，都按正文长高。对话框是固定的
  760×168，贴在底部居中，不跟某一页的长短变。正文长度上限是 500（18 号字）。超出就按词折行，
  中日韩按字折。文案按这个上限来写。
- 对话框根 panel 挂 `script.lua` behavior 并常驻 Hidden；脚本提供 `say(script, onDone)` 与 `busy()`。
  `script` 是页数组，或带 `start` 的页表。一页是字符串，或 `{ text, next, choices, onShow }`。
  `choices` 的每一项是 `{ text, next }`，`next` 是另一页的键。`onShow` 是函数时，这一页打开时调用一次。数组页省掉 `next` 就顺序往后；
  具名页省掉 `next` 就关上。选项是正文右侧各自一行的 `Choice1`..`Choice4`，这一页打开就显示，不等打字机结束。
  `>` 画在固定宽度的 `Mark` 槽里，标签文字不随高亮改变，所以换项时这一列不左右挪。
  Up / Down 移动高亮，确认选中。确认键在正文还没打完时只把这一页补全。
- 玩法侧（NPC / 玩家 / 箱子 / 告示牌）经 `ui.get(self.dialogueName):say(...)` 说话。
  `dialogueName` 默认 `"Dialogue"`，场景 `propertyOverrides` 可改。跟随相机的目标
  默认 `"Player"`（`FollowCamera.lua` 的 `targetName`）。**移动锁由玩法状态控制**——
  玩家脚本每帧轮询 `dialogue:busy()`，game-ui S4 的 modal 条目落地后才迁移。
  `ui.get` 在条目脚本加载完成前的头几帧回落成控件 `ScriptRef`（读未知字段会报错，
  不是 nil）。轮询侧先确认拿到的是表，再 `dialogue.busy ~= nil` 然后调用。
- 一次按键只做一件事：对话框忽略「开框那一帧」的 `confirm`（`time:getFrameIndex()`），
  玩家侧触发交互后要求确认键完全松开才再武装（re-arm），否则同一次按键会同时
  推进对话又再次触发交互。
- Lua 全局是小写 `time`（`LuaTimeApi` 实例）与 `input` / `log`；大写 `Time` 是
  usertype 表，不能在其上调方法。

## 背包

- 物品名字在 `Content/Scripts/Items.lua` 的 `catalog`。数量在 `Persist.items[id]`。
  `Items.give(id, count)` 只接受目录里的 id。存档和场景转移都跟着 `Persist` 走。
- 面板是 autoMount 条目 `Inventory`（`Content/UI/Inventory.yaui.json`）。`I`（动作 `inventory`）
  开关。面板默认 Hidden，隐藏控件不进 tick，所以脚本用 `every` 听这键。对话框开着时不做切换。
  玩家在面板开着时同样停步。
- 箱子第一次打开调用 `Items.give("brass_key", 1)` 和 `Items.give("coin", 5)`，同时记下 `Persist.opened`。
  `Items.count` / `Items.take` 读写同一张表。花光的一摞会从袋子里拿掉。

## 任务

- 任务文案在 `Content/Scripts/Quests.lua` 的 `catalog`。状态在 `Persist.quests[id]`，取值 `active` 或 `done`。
  `start` 对已经接过或完成的任务不再改写。`complete` 把未完成的记成 `done`，没接过也记。
- 面板是 autoMount 条目 `Journal`（`Content/UI/Journal.yaui.json`），贴在左上。`J`（动作 `journal`）
  开关。面板默认 Hidden，隐藏控件不进 tick，所以脚本用 `every` 听这键。对话框开着时不做切换。
  玩家在面板开着时同样停步。正文 `_bWrap` 且 `_maxWrapWidth` 落在卡片内宽里，长目标折行，卡片跟着长高。
- 村民的「What's in the house?」打开 `errand` 页，页上的 `onShow` 调用 `Quests.start("pot")`。
  罐子第一次打开调用 `Quests.complete("pot")`。状态真的变了才弹出 `QuestNotice`：
  「Quest accepted」或「Quest complete」，加上任务名。几秒后自己关掉，不锁移动。

## 商店和结局

- 货和价格在 `Content/Scripts/Shop.lua`。`well_charm` 卖 3 个 `coin`。已经带着的货不再卖。
  钱不够返回 `"poor"`，买成返回 `"ok"`。
- 广场上的 `Merchant` 用对话框卖。打开柜台就 `Quests.start("well")`。买到的是护符。还可以问钥匙的用途。
- `Well` 在广场北面。身上有护符，或有柜子里的井之玻璃，才把 `well` 记成完成。两条结局句子不同。都没有就只说井还是静的。
- 房子左侧的 `Cabinet` 用罐子里的黄铜钥匙打开一次，钥匙花掉，换成 `well_glass`。这是不花钱唤醒井的另一条路。
- 一条可玩通的流程：城镇摊位（没钱）→ 房子里的罐子（钥匙和硬币）→ 门口走回城镇（`Persist` 还在）→ 买护符 → 井。
  柜子是可选的。房子是这一条里的室内。不另做迷宫图。

## 设置和语言

- Esc 是运行时的取消键，不进 InputMap。玩家脚本的 `onCancel` 打开 `Settings`。编辑器 Play 里，Esc 即使视口控件没有键盘焦点也会先交给游戏；游戏没接住，松键才停止 Play。文本框正聚焦时 Esc 仍留给文本框。视口里按下鼠标会抓住指针，这层捕获不再吞掉 Esc；游戏接住 Esc 时松开指针。卡片开着时它是最上面的可见条目，自己的 `onCancel` 把它关上，玩家脚本不会再被叫到，所以一次 Esc 只开关一次。卡片固定在画面正中。空格在卡片里切换语言。
- 英文是原文。`Persist.locale` 缺省或 `"en"` 时 `Locale.tr` 原样返回，玩法测试继续对英文。`"zh"` 才查中文表。`Locale.refresh` 让对话、背包、日志、任务提示和设置各自 `applyLocale`。对话开着时切换会换掉当前页和右侧选项。
- 设置开着时玩家停步。对话、背包、日志在这段时间里不吃确认和自己的开关键。任务提示仍然不锁移动。

## 像素完美相机

俯视像素画的取景在 `CameraComponent` 上，脚本不再自己算 zoom：

- `_pixelPerfect` 默认关。打开后正交半高由 `resolveCameraViewFraming` 按该 View 的**设备像素**高度（`outputRect.extent`，等于逻辑点乘 `SceneViewDesc::pixelDensity`，不是窗口逻辑尺寸）计算：`zoom = max(1, floor(viewHeight / _referenceHeightPx))`，半高 = `viewHeight / (2 * _pixelsPerUnit * zoom)`，半宽跟真实宽高比。zoom 是整数且不低于 1：视口比参考矮时少看一些世界，一个 texel 至少占一个设备像素。2x 密度下 zoom 翻倍。
- 只按高度取 zoom。像素是正方形，半宽跟真实宽高比走，横向的 texel→像素比和纵向相同，不另设参考宽度。pixel perfect 不用 `_fixedAspectRatio`，否则横向会被拉开。
- 相机眼在 `buildCameraRenderMatrices` 里吸附到设备像素（不写 Transform）。宽或高为奇数时该轴偏半个设备像素，texel 边落在像素边上。偶数尺寸相位为 0。
- 精灵 / tile 的 `worldCenter` 在 `RenderFrameExtractor::extractSceneSnapshot` 吸附到 `1 / _pixelsPerUnit`。候选被所有 View 共享；整数 zoom 的设备像素是 texel 的细分，再叠加每 View 的眼吸附，精灵不会落在半个设备像素上。
- `world.viewSize()`：主相机是 pixel-perfect 正交时返回 `(halfWidth, halfHeight)` 世界单位，否则仍是像素尺寸。`world.viewAspect()` 仍是宽/高，pixel perfect 时等于 `halfWidth / halfHeight`。跟随脚本用它钳制地图。
- 编辑器正交 XY 视口是自由相机，不读这个开关。

Tiny Town（Town / House / TownLarge）：`_pixelsPerUnit = 16`，`_referenceHeightPx = 192`（12 格）。1280×720 时 zoom = 3，半高 7.5。

## 输入动作（InputMap）

按键表在项目数据文件里，不在脚本里。`.yaproject` 的 `inputMap` 是项目相对路径
（`FProjectDescriptor::resolvePath`：先项目根，再工作区根）。文件是「动作名 → 按键名
数组」，例如 `move_up: ["Up", "W"]`、`confirm: ["Space", "Enter", "E"]`。一个动作里
任一键按下即成立。

脚本：`input:isActionDown(name)` 是按住，`input:isActionPressed(name)` 是本帧按下沿
（和 `isKeyDown` / `isKeyPressed` 同一对）。2D RPG 原型的文件是
`Example/2DRpgPrototype/InputMap.json`；方向谁优先仍由 `Player.lua` 的检查顺序决定。
缺文件或没有这个动作：`YA_CORE_WARN`，查询返回 false，不崩。没写 `inputMap` 的项目
沿用 `.yaproject` 里的 `inputActions`，都没有则用引擎内置键位。

## Box2D 与 tile 棋盘的空间边界约定

**权威判据**（P1 已落地，2026-10-14 文档化）：

- **棋盘判定用 tilemap 查询**（权威）：`isSolid(x, y)` / `entityAt(x, y)` / `worldToCell` / `cellToWorld` / `bounds` 反射方法。
- **Box2D 只负责非格子的可交互物**：物理系统（`Physics2DSystem`）与 tilemap 组件**互不感知**。
  - 棋盘格子的通行性由 tile 的 solid 属性与 `entityAt` 决定，物理碰撞体不参与。
  - 角色/NPC 的移动走脚本 `GridMover.lua`，调用 tilemap 查询，**不走物理推动**。
  - 推箱子/压力板/移动平台等机关用 `RigidBody2DComponent` + `Collider2DComponent`，但**不与 tilemap 碰撞**（Box2D 里没有 tile 静态体）。
- **机关战斗的物理交互**（C1 内容将用到）：
  - 推箱子：玩家碰撞体 `onContact2D`，对箱子施加 `impulse`，箱子落到新格后触发压力板。
  - 压力板：sensor collider（`bSensor = true`），检测 `bBegan` 接触事件。
  - 移动平台：kinematic body（`type = kinematic`），脚本驱动位置，角色站在上面跟随（velocity 继承或位置约束）。
- **两套系统的边界**：
  - tilemap `isSolid` **不问**物理世界（组件看不见场景，查询注册在 `GameplayScriptFunctions.cpp`）。
  - Box2D `step` **不感知** tilemap（物理系统只看 `RigidBody2D` / `Collider2D` 组件）。
  - 脚本负责桥接：玩家脚本读 tilemap 决定能否进格，物理事件回调通知脚本触发机关。

**反面约束**（禁止的做法）：
- ❌ 不要尝试把 tilemap 的 solid tile 导出为 Box2D 静态碰撞体（重复且低效）。
- ❌ 不要让角色移动同时依赖 tilemap 与物理（会出现"tilemap 说能走、物理说撞墙"的矛盾）。
- ❌ 不要用物理查询（raycast/overlap）判断格子通行性（权威是 tilemap）。

## 边界

- 不引入 `Node2D` / `Transform2D` / `Camera2D`；2D 对象 = `Node3D` +
  `TransformComponent` + `Sprite2DComponent`。
- 精灵渲染管线（合批、纹理表、纯 2D 图）归 `rpg-prototype`（2026-10-02 合并了
  `scene-2d-world-and-game-ui`）；动渲染前先过 P0 契约（`P0-contract-matrix.md`）。
- 编辑器预览不运行玩法脚本；对话框等 Game UI 走 yaui（`gui-framework`）。
