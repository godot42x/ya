# RPG Prototype：以 2D 游戏闭环驱动引擎迭代

## 0. 目标与结论

继 `Example/GreedySnake` 之后，用一个 RPG Maker 式的俯视 2D 小游戏驱动引擎补齐 2D 能力：

- 地图可以在编辑器里画（tile 图层），场景文件就是地图。
- 玩家在地图上走动、被墙挡住，面朝 NPC / 告示牌按确认键触发对话。
- 相机跟随玩家，停在地图边界内。
- 走进门切换到另一张地图，跨地图的游戏状态保留。

示例放在 `Example/2DRpgPrototype`。

核心结论（2026-09-29 讨论，用户确认）：

1. **不是「类 UE 架构」导致 2D 麻烦。** 2D 对象 = `Node3D` + `TransformComponent` +
   `Sprite2DComponent`，与 Unity（3D Transform + SpriteRenderer）同构；Game UI 走 yaui 与 Godot
   Control / Unity uGUI 的分工一致。不引入 `Node2D` / `Transform2D` / `Camera2D`。
2. **缺的是 2D 功能层**：Tilemap 与编辑笔刷、tile 通行查询、实体查询与脚本间调用、玩法层切场景、
   帧动画。
3. **两处架构债由本计划负责**：
   - 玩法脚本 API 全靠手写绑定（`LuaScriptingSystem::bindReflectedComponents` 是空 TODO，
     `Sprite2DComponent` 在 Lua 只有 `bVisible/size/tint`）。改为反射驱动、与脚本语言无关的
     绑定层（§4 B1/B2）。
   - 纯 2D 画面走完整 Deferred 管线、每个精灵一个 draw call。R4 用数据定案；2026-10-02 起
     渲染改动也由本计划实现（合并自 `scene-2d-world-and-game-ui`，见 §9）。
4. **Tilemap 是第一个引擎驱动点**，不是最后：Lua 生成的地板在编辑器里看不见、改不了，与
   「可编辑地图」相反。
5. **纪律**：每个引擎改动必须写明是被哪个 checkpoint 的哪段游戏需求逼出来的；写不出来就不做。

## 1. 现状基线（2026-09-29 在代码里核对）

| # | 能力 | 现状 | 锚点 |
| --- | --- | --- | --- |
| G1 | 2D 世界对象 | `Sprite2DComponent`：`image/size/uvRect/flip/tint/layer/sortOrder` 均已反射，编辑器 Sprite 预设可建 | `Scene2D/Sprite2DComponent.h`（`Framework/Scene/Scene2D`），`EditorModule.cpp` Sprite 预设 |
| G2 | 精灵排序 | 不透明（`tint.a >= 1`）alpha 裁剪 + 写深度；半透明按 `layer → sortOrder → 视深` | `Sprite2DWorld.slang` `discard`，`Sprite2DStage.h` |
| G3 | 精灵渲染 | 只在 Deferred 图里（`appendSprite2D`），Forward 没有；逐候选一次 draw；每 View 纹理表 16 张 | `DeferredFrameGraphPasses.cpp`，`Sprite2DStage::drawSprites`，`kTextureTableSize` |
| G4 | 精灵候选 | `WorldSpriteCandidate` 自带世界轴、uv、纹理，不绑死实体 → 非实体来源（tile）可以直接产候选 | `RenderFrameData.h`，`RenderFrameExtractor.cpp extractSprites` |
| G5 | Tilemap | 没有 | — |
| G6 | 玩法 Lua 组件面 | 手写 usertype：Transform、Camera、Sprite（3 个字段）；反射自动绑定为空 | `LuaScriptingSystem.cpp`，`GameplayLua.cpp` |
| G7 | 反射底座 | 字段 `MetaBuilder` 标记（`.color()`、`.instanceEditable()`…）；`YA_REFLECT_METHOD` 登记方法，std::any 调用器 + 应用层扩展表（`MethodJsonInvokers` 按 `Function*` 挂 JSON 调用器） | `Core/Reflection/MetadataSupport.h`，`MethodReflection.h` |
| G8 | 实体查询 / 脚本互调 | Lua 只能拿到 `self.entity`；无按名字 / 按格子查找；S1 的 `call` 只覆盖生命周期回调 | `LuaScriptingSystem.cpp`，`game-ui-script-framework` S1 |
| G9 | 碰撞 / 触发 | Jolt 3D，只有 box/sphere，无 trigger、无 Lua 回调 | `Physics/PhysicsSystem.cpp`，`PhysicsBodyComponent.h` |
| G10 | 切场景 | 只有自动化 JSON 接口 `loadScene`，玩法 Lua 调不到 | `GameRuntime/ScriptApiCore.cpp` |
| G11 | 编辑器 2D | View 菜单正交 XY、平移 gizmo 锁 Z、吸附 0.5；无网格显示、无笔刷 | `EditorLayer::setEditorOrthoXY`，`EditorViewportGizmoController.cpp` |
| G12 | 像素风采样 | R0 核实时 `resolveSlotSampler` 无视 `samplerConfig`、一律线性；已修：`TextureLibrary` 预建（Nearest / Linear）×4 寻址的 sampler 表，按槽位取（Cubic 退回线性） | `Core/Common/TextureSlot.h`，`TextureSlotBinding.cpp`，`TextureLibrary::getSampler` |
| G13 | 素材 | Kenney Tiny Town（CC0）`Engine/Content/TestTextures/tiny_town/tilemap_packed.png`：192×176，16px，12×11 格，地面/树/房屋/栅栏/告示牌；**无角色行走图** | — |

## 2. 边界（不可越过）

- 不引入 `Node2D` / `Transform2D` / `Camera2D` / 第二棵场景树；Tilemap 是挂在 `Node3D` 上的组件。
- GUI 框架不 include Lua / ECS / Scene；对话框走现有 yaui + 控件脚本（`game-ui-script-framework` S3）。
- 设计器与编辑器预览不运行玩法脚本。
- 不造中心事件总线：交互是「玩家脚本找到目标实体 → 直接调用它脚本上的函数」。
- 引擎与新玩法代码只依赖脚本中立层；只有 Lua 后端 include sol2（B1 起对新代码生效，B2 收口旧代码）。
- 渲染改动遵守 §9 继承的约束：P0 契约冻结前不写新 pass / shader；GUI compose 与 Scene sprite
  路径语义分离；不新增第二套 Scene scheduler / frame loop。
- 不在帧录制中途重建 GPU 资源；tile 编辑后的候选刷新走下一帧提取。

## 3. 已确认的决策（2026-09-29）

| # | 问题 | 结论 |
| --- | --- | --- |
| D1 | 移动 / 碰撞模型 | 两种都可。先做 tile 通行表查询（按格移动足够）；查询面抽成小接口，以后接 AABB、Box2D 或 Jolt 只换实现，玩法脚本不变 |
| D2 | 脚本绑定 | 反射了的字段 / 方法自动注册到脚本（可见性见 D12）。绑定描述与脚本语言无关；Lua 以后可以是插件，换 QuickJS 不应大改 |
| D3 | 地图数据位置 | 场景即地图：tile 图层内联在 `TilemapComponent`；tileset 是独立资产 |
| D4 | 计划位置 | 新开本目录；`scene-2d-world-and-game-ui` 只记渲染侧。2026-10-02 两线合并到本目录，见 §9 |
| D5 | 与活跃线的顺序 | 与 `game-ui-script-framework`（S4–S7）、`ui-behavior-capabilities`（C2/C3）交替推进，按 checkpoint 排；冲突规则见 §5 |
| D6 | 绑定层范围 | 分两步：B1 中立层 + Lua 后端投影（组件字段 / 方法 + 模块函数），新代码只依赖中立层；B2 把 Lua 拆成独立 target、迁移控件句柄与 `world.*` / `ui.*` 手写函数 |
| D7 | 素材 | 地图用仓库里的 Tiny Town；角色行走图先试 grok-4.7 生图，不合格再找 CC0 角色素材。素材随许可说明入 `Example/2DRpgPrototype/Content` |
| D8 | 现有 QuickJS 反射导出（`JSScriptingSystem`，供 `eval_js` / MCP） | B1 起 JS 与 Lua 共用同一个中立导出层，不留第二个反射→脚本导出器 |
| D9 | 可见性 | ~~一律只看标记了的字段 / 方法~~，由 D12 取代 |
| D10 | 中立层的值传递 | 类型化的值，不走 JSON；JS 迁移后 JSON 方法调用器（`MethodJsonInvokers`）删除 |
| D11 | 反射方法怎么被脚本调用 | 走插件自己的 `Function::invoker`，不另建每方法调用器：插件 `Function` 补记参数 / 返回的 `type_index`，`Enum` 能把值装成自身类型的 `std::any`；中立层只有一张按 `type_index` 的值编解码表，字段读写、参数、返回值共用 |
| D12 | 脚本可见性与查找开销（B1 后修订） | 没有「脚本不可访问」的需求，不设门禁：反射了的非静态成员只要类型能过边界就导出，const / 无可写访问器的字段只读；`.script()`、`Meta::ScriptName`、插件 `FieldFlags::Blueprint*` 删除。中立层按名字查一次得到常驻句柄（`findField / findMethod`）；Lua 每个类型一张元表，名字首次命中后把方法闭包 / 字段句柄 `rawset` 进该类型的缓存，之后同类型任何对象都是一次表查找。JS 暂不缓存 |

## 4. Checkpoints

每个 checkpoint 一个提交（计划文件同提交）。「逼出来的需求」写明引擎改动的来源。

### B1 — 脚本中立绑定层 + Lua / JS 投影

逼出来的需求：R0 行走动画要写 `uvRect`、R1 要调 tilemap 查询方法、R2 要按名字找实体；每个都手写
一遍绑定，就是 G6 的放大器。开工时发现 `JSScriptingSystem` 已经把全部反射按 JSON 导给 `eval_js`（D8–D10）。

- **可见性（D12）**：反射了的非静态、非指针成员，类型能过边界就导出，不需要标记；过不了的静默不导出。
  脚本名是反射名去掉前导 `_`（`_fov` → `fov`，`bPrimary` 不变）。方法的元数据经
  `Register::function(name, fn, meta)` 交给插件（与 `property` 同式）。
- **中立层**放 `Core/Scripting/`（`ya-foundation-core`，与 `ScriptApiRegistry` 同处，只依赖反射），不新建 target：
  - 值：nil / bool / 整数 / 浮点 / 字符串 / vec2-4 / 对象引用；枚举按整数传，写入也接受枚举名。
  - 对象引用 = (类型, 引用种类, 两个 64 位载荷)，每次访问经种类的解析函数找回对象，不持有指针；
    找不回来时报脚本错误。
  - 方法（D11）：参数按 `Function::argTypeIndices` 装箱成 `std::any`，调插件 `Function::invoker`，返回值按
    `returnTypeIndex` 拆箱；JSON 调用器扩展表与 `InstanceRef` 删除。签名里有不支持的类型时不导出。
  - 每类型的导出（`Property*` / `Function*` + 继承路径，原生与解析器答出的方法同表）在 `.cpp` 内首次使用时建，
    只增不删；公开面是常驻句柄 `findField / findMethod` 加句柄版 `readField / writeField / callMethod`，
    按名字的版本只是「查 + 用」。
  - 类型原生方法：不来自反射、由提供方登记在某类型上（实体的组件存取）。
  - 组件字段写入后调用 `onPostSerialize`（与原 JS 路径一致；Transform 借此置脏）。
- **引用种类**由 `ya-scene-core` 提供：场景（`Scene::_instanceId`）、实体（场景 id + 带版本的 entt 句柄）、
  组件（实体 + 类型）。`Scene` 维护 instanceId → 活场景的查找表（不依赖 lifecycle host，测试里也成立）。
- **实体脚本面**（两种语言相同）：`getId / getName / setName`（反射方法）；原生 `get / has / add / remove(类型名)`；
  每个组件类型的 `get<短名>() / has<短名>()`，短名 = 类名去掉 `Component`（`getTransform`、`getCamera`、
  `getSprite2D`）。`Entity::componentByName` 等只为 JS 存在的反射方法与 `components()` 删除；C++ 调用方
  用到的 `addComponentByName / removeComponentByName` 保留为普通方法。
- **Lua 后端**（仍在 `ya-ecs-systems`）：承载引用的 full userdata，元表按类型懒建（D12），`__index / __newindex`
  先查该类型的缓存表，未命中才进中立层并回填；方法闭包带类型 upvalue，拿别的类型当 self 报错；元表对脚本隐藏；
  sol 经 `sol_lua_push/get/check` 定制点认它，不再是 usertype；
  `self.entity`、`world.spawnSprite` 返回实体引用。删除 Transform / Camera / Sprite / Entity 的手写 usertype 与
  GameplayLua 里的 `hasSprite / getSprite`；Vec4 值类型随 Vec2 / Vec3 归到 Lua 后端。
- **JS 后端**：字段 / 方法导出改走中立层，值直接在中立值与 JS 值之间转换（向量仍是数组）；
  `ya.entity.create/get/list`、`ya.scene.active` 返回中立引用。
- **新增反射**：Transform 的 get/set 方法、`CameraComponent::setAspectRatio`、Entity / Scene 的名字与计数方法。
- **不在 B1**：`world.*` / `ui.*` 既有函数与控件句柄（§5）；模块函数登记推迟到第一个使用者 R0；
  脚本函数句柄推迟到 B2（B1 没有使用者）；反射枚举的通用导出（`CameraProjection` 表仍手写）。
  Transform 手写的 `getForward/getRight/getUp` 没有脚本在用，删除不补。
- 测试：`ScriptBindingTest.ReflectedMembersThatCrossAreVisible`、`FieldsReadAndWriteThroughReflection`、
  `EnumsTravelAsIntegersAndAcceptNames`、`MethodsCallThePluginInvoker`、`GoneObjectsRaiseInsteadOfDangling`、
  `EntityRefsReachComponentsAndGoStale`、`SceneRefsDieWithTheirScene`；
  `LuaScriptBindingTest.ReflectedMembersReachTheComponents`、`MembersAreCachedPerType`、
  `MissingComponentIsNilAndRefsCompareByIdentity`、`MistakesRaiseLuaErrors`、`DestroyedEntityRaisesInsteadOfDangling`；
  既有 `ScriptApiTest` / `ScriptApiLibraryTest` / `AppAutomationControlJsTest` 按新名字改写后全绿。
- 验收：GreedySnake 只把 `getSprite` 机械改为 `getSprite2D`，GreedySnake / HelloMaterial 的 `camera.primary`
  改为 `camera.bPrimary`（D12 无改名）；运行冒烟与 `ya-testing` 全绿；
  `rg -n "new_usertype<(TransformComponent|CameraComponent|Sprite2DComponent|Entity|LuaScriptObject)>" Engine/Source`
  与 `rg -n "MethodJsonInvokers|findJsonInvoker|InstanceRef|ScriptInvoker|Blueprint(ReadOnly|ReadWrite|Callable|Pure)|ScriptName|findMember" Engine Script` 无结果。

### R0 — 示例壳 + 走动 + 相机跟随

- 游戏验收：编辑器里摆一个玩家精灵、在 Inspector 选贴图；Play 后方向键按格走动（格间补间），
  四方向朝向 + 行走帧动画；相机平滑跟随。
- 内容：`Example/2DRpgPrototype`（照 GreedySnake 的 yaproject / xmake / module 结构）、
  `Content/Scenes/Town.scene.json`、`Content/Scripts/Player.lua`、`Content/Scripts/FollowCamera.lua`；
  Tiny Town 图集与角色行走图（D7）入 `Content/Textures/`，附许可说明。
- 逼出来的引擎改动：
  - `world.find(name)`（B1 中立层登记）：相机脚本要找到玩家。
  - `world.viewSize()` 返回像素尺寸：像素对齐取景 `orthoHalfHeight = 视口高 / (2 × 每单位像素 × 缩放)`。
  - 核实 G12：像素图选 Nearest 后精灵 pass 确实用最近邻；不是则修到 sampler 走 `TextureSlot`。
- 输入用现有 `input:isKeyDown` 轮询，不依赖 S5 的 `onKey`。
- 模块函数（落地）：中立层 `registerModuleFunction(module, name, fn)`，类型化值进出；后端建状态时投影
  （Lua 全局表 `world.*`、JS `ya.world.*`），所以登记在 Lua / JS init 之前（`registerGameplayScriptFunctions`）。
  与 `ScriptApiRegistry` 分工：后者是编辑 / 自动化的 JSON 命令表（`component.get`、`scene.save`），
  玩法每帧对活对象的调用走中立层；JS 上同名时类型化函数优先。
- 途中发现：精灵 pass 把图像顶行画在四边形底边（共享 quad 是 GL 约定 `texCoord.y` 朝 +Y，`uvRect` 是
  图像空间）。在 `Sprite2DWorld.slang` 里翻转 quad 的 v，quad 网格不动（光照全屏 / billboard / Quad 预设共用）。
- 验收：`python3 Script/ya.py run --project Example/2DRpgPrototype/2DRpgPrototype.yaproject -- --exit-after-frame=120`
  exit 0；行走 / 跟随手测；自动化见 `Script/automation/2d-rpg/`（`input.inject_key`，评审步骤 2）。

### R1 — Tilemap（数据 + 渲染 + 编辑笔刷 + 通行查询）

一个 checkpoint 一个提交：R1a / R1b / R1c 共享 `TilemapComponent`、`Town.scene.json` 与 tileset 文档，
R1b / R1c 改的正是 R1a 引入的文件，所以按一个可验收目标一次落地。

#### R1a — Tileset 资产 + TilemapComponent + 渲染

- 游戏验收：手写一份带 tilemap 的场景 JSON，Play 看到三层地图（地面 / 装饰 / 遮挡），玩家在地面与
  遮挡层之间。
- `Tileset` 资产（`.yatileset.json`）：图集路径、tile 像素尺寸、边距 / 间距、每个 tile 的 `solid`。
  按资源系统规则加载（`resource-system` skill）。
- `TilemapComponent`（挂 `Node3D`，与 `Sprite2DComponent` 同模块）：tileset 引用、每格世界尺寸、
  宽高、图层数组 `{name, zOffset, cells}`；格子 0 = 空，其余 = tile 序号 + 1。序列化格式见 D-T1。
- 渲染：extractor 按 View 可见范围把非空格展开成 `WorldSpriteCandidate`（`entityId` = tilemap 实体），
  复用 `Sprite2DStage`；不新增 pass。图集无内边距时 uv 内缩半像素防串色。
- 测试：`TilemapComponentTest.RoundTripsLayers`、`TilemapExtractionTest.OnlyVisibleCellsBecomeCandidates`、
  `TilemapExtractionTest.EmptyCellsAreSkipped`。

#### R1b — 编辑器 Tile 笔刷

- 游戏验收：编辑器里选中 tilemap，在正交 XY 视口画一面墙、擦掉一块草、矩形填充一片路，保存重开仍在；
  每一笔可以撤销。
- Tile Palette tab：显示 tileset 网格，单选或框选图章；图层选择。
- 视口笔刷模式（选中 tilemap 且正交 XY 时可进入）：画 / 擦 / 矩形填充 / 吸管；网格线走编辑器
  overlay 的世界绘制列表。
- 一笔（按下到松开）= 一个撤销步，推到关卡根会话的 `UndoStack`（与 `EditorTransformUndo`、Inspector
  属性编辑同一个栈），撤销记录存图层前后内容，不另造撤销栈。
- Inspector：宽高、每格尺寸、tileset 可编辑；改宽高保留已有格子。
- 测试：`TilemapEditUndoTest.StrokeIsOneUndoStep`、`TilemapEditTest.RectFillWritesOnlyTheActiveLayer`、
  `TilemapEditTest.ResizeKeepsExistingCells`；编辑器冒烟 exit 0。

#### R1c — 通行查询 + 被墙挡住 + 相机限界

- 游戏验收：编辑器画墙 → Play 被挡住；相机停在地图边界内。
- `TilemapComponent` 反射可调用方法：`worldToCell`、`cellToWorld`、`isSolid(x, y)`、`bounds()`。
- 查询面按 D1 抽成小接口（先只有 tile 实现），玩家与以后的 NPC 共用；AABB 查询等自由移动需求出现再加。
- 测试：`TilemapQueryTest.SolidTileBlocks`、`OutOfBoundsIsSolid`。

### R2a — 事件实体与交互

- 游戏验收：编辑器里摆 NPC 和告示牌（精灵 + 各自脚本），玩家面朝它按确认键，它的 `onInteract` 被调用；
  NPC 占住的格子不能走进去。
- 逼出来的引擎改动：
  - 按格子查实体（先在 tilemap 所在场景里按 Transform 换算，量大了再加索引）。
  - 脚本互调：`entity:call("onInteract", ...)` 调目标实体脚本的具名函数，带返回值；扩展 S1 的
    `call`，不走事件总线。
  - 角色前后遮挡：约定「角色 z = 图层基准 − y × ε」（不透明精灵靠深度排序，G2），写进示例与
    skill；两个以上脚本重复这段时再下沉成组件字段。
- 测试：`LuaScriptHostTest.NamedCallReturnsValue`、`NamedCallOnMissingFunctionIsNil`。

### R2b — 对话框

- 游戏验收：交互弹出对话框，逐字显示，确认键翻页 / 关闭；对话期间玩家不能移动。
- 内容：`UI/Dialogue.yaui.json` + `UI/Dialogue.lua`（控件脚本，`self:every` 做逐字），玩法经
  `ui.get("Dialogue"):say(lines, onDone)` 调用。
- 「对话期间不能移动」：game-ui S4 已落地则用 `modal` 条目；否则先由玩法状态控制，S4 落地后迁移。
- 验收：手测 + 冒烟；若 S7 自动化已有，补一条注入按键的端到端脚本。

### R3 — 切换地图与跨场景状态

- 游戏验收：走进门切到 `House.scene.json`，站在对应出生点；出来回到门口；开过的宝箱保持打开。
- 逼出来的引擎改动：
  - 玩法层 `world.loadScene(path, spawnName)`，在安全时机（帧尾结构变更阶段）切换；与自动化
    `loadScene` 共用底层实现，不合并两套 API。
  - 跨场景常驻的脚本状态：定义一个明确的常驻表（不是依赖「全局变量碰巧没被清」），切场景保留、
    退出 Play 清空。
- 测试：`SceneTransferTest.SpawnPointPlacesPlayer`、`PersistentStateSurvivesTransfer`、
  `PlayStopClearsPersistentState`。

### R4 — 规模与定案（2026-09-30 完成，数据与决策见 `r4-measurements.md`）

- 游戏验收：64×64、3 层地图 + 20 个 NPC（`Content/Scenes/TownLarge.scene.json`，由
  `Tools/make_scale_scene.py` 确定性生成），量 profile 构建下的帧时间与 draw 数。
- 结论（一句话版；依据与复现命令在 `r4-measurements.md`）：
  - 逐候选约 **1.2µs** CPU 录制，5134 候选取 ~6.3ms，占 10.6ms 帧的 60%，且随候选数线性增长
    → **精灵合批 / 实例化是当前最高价值的渲染改动**（纹理只有 2 张，形态高度重复）。
  - 窗口缩到 1/4 像素只省 0.8ms → **填充/全屏 Deferred 链不是瓶颈**；「纯 2D View 绕开
    Deferred」按正确性与图层面清晰度推进，不要拿性能当理由（图层面仍值得拆）。
  - 纹理表 2/16 → 上限当前不是约束；但每帧重建 + 逐候选线性查找要随合批一起改成直接映射。
  - 整帧提取只要 0.10-0.15ms → **tile 候选区块缓存现在不需要**（4× 面积时再评估）。
- 本 checkpoint 只产出数据与决策记录，不改渲染。
- 未决：`Render/Frame` 的 self 时间（4.3/5.0/2.9ms，疑似帧栅栏等待）在 CPU trace 里无法归因，
  需要 pass 级 scope 或 GPU timing（本机无 RenderDoc）——已列进交接项。

### B2 — Lua 插件化与旧绑定收口（评审步骤 7；归属见 §5）

- Lua 后端从 `ya-ecs-systems` 拆成独立 target；引擎、GameRuntime 只依赖中立层。
- `world.*` / `ui.*` 手写函数、控件句柄（`LuaWidgetHandle` / `LuaWidgetScripts`）迁到中立层登记。
- `world.viewAspect`（手写 sol2）与 `world.viewSize`（中立层）去重；新脚本函数不经 `App::get()` 取单例。
- 前置（评审步骤 2，已完成）：具名调用只剩 `callNamed(instance, name, args, ENamedCallError)`；
  脚本非 const 方法调用在 `ScriptRefKind::afterCall` 进编辑漏斗。
- 验收：`rg -n "sol/|sol::" Engine/Source --glob '!**/Script/Lua/**'` 只剩 Lua 后端；
  GreedySnake、2DRpgPrototype 冒烟与脚本测试全绿。

## 5. 与活跃线的关系与冲突规则

2026-10-02 起三项归属转入本计划：B2 脚本对外接口收口、`input.inject_key`、`viewAspect` / `viewSize` 去重。

- `game-ui-script-framework`：S5 / S7 仍决定 `onKey`、`world.spawnSprite`、`ui.*` 的**语义**；
  这些函数的新形态直接登记在 B1 中立层，不再新增手写 sol2。B2 只迁移 S5 / S7 没有动到的手写函数
  和控件句柄，并删除残留；动到同一函数时 S5 / S7 先落地。`input.inject_key {key, action: down|up|hold, frames?}`
  与 `ui.query {entry, widget?}` 已在评审步骤 2 提供，S7 的端到端脚本直接复用；按键走 `App::dispatchEvent`，
  S5 的 `onKey` 会收到。该计划 §7 的「反射自动绑定 Lua」「场景可编辑性（墙、取景）」由本计划接手。
- `ui-behavior-capabilities`：C3 只管生命周期回调路径 `LuaScriptingSystem::call(ELuaScriptCallback)`；
  具名调用（脚本互调、`onShow` / `onHide`）已合并为 `callNamed` + `ENamedCallError`，归本计划。两者不并存第二套调用路径。
- `scene-2d-world-and-game-ui`：已合并进本计划并归档，见 §9。
- 关卡撤销：R1 复用关卡根会话的 `UndoStack`（`EditorTransformUndo` 同款），不另造撤销栈。

```text
B1 ──► R0 ──► R1 ──► R2a ──► R2b ──► R3 ──► R4 ──► 评审步骤 1–7（review-2026-09-30.md 文末）
game-ui S5 / S7 ──► B2（只在动到同一函数时等待）
```

## 6. 待定决策门（到对应 checkpoint 前确认）

| # | 问题 | 建议 |
| --- | --- | --- |
| D-T1 | tile 图层在场景 JSON 里的格式 | 每层一个整数数组，全空行可压缩；可读性优先，R4 数据显示文件过大再换 RLE / base64 |
| D-T2 | 帧动画 | R0 在 Lua 里改 `uvRect`；玩家与 NPC 两处重复后评估 `SpriteAnimationComponent`（在 R2a 之后决定） |
| D-T3 | Tileset 通行之外的属性 | 先只有 `solid`；需要伤害地形 / 遇敌区时再加标签 |
| D-T4 | 按格查实体是否需要空间索引 | 先线性；R4 数据决定 |

## 7. 不在本计划

- 物理化的碰撞与触发（Box2D / Jolt 2D）；D1 的查询接口为它留位置。
- 预制体（`game-ui-script-framework` D6 已延后）。
- 战斗、背包、存档到磁盘。
- 世界空间 UI（名字牌 / 血条）、UI 动画轨道。
## 8. 风险

- 绑定层与 game-ui S5/S7 同时改脚本面 → §5 冲突规则；B1 对 `GameplayLua.cpp` 只改实体的传递方式（返回 / 接收实体引用）与删除 `hasSprite/getSprite`，不动函数语义。
- `eval_js` 可见面收窄（D9）→ 自动化 agent 改用 `component.get/set`；MCP 桥说明同步。
- 生成的角色图像素网格不齐、调色板与 Tiny Town 不搭 → D7 回退到 CC0 素材，R0 验收看像素对齐。
- 逐格一个候选、逐候选一次 draw，在大地图上可能吃紧 → R1 先做可见范围裁剪，R4 定合批。
- tile 编辑触发候选重建的频率 → 候选每帧从组件提取，编辑只改组件数据，不碰 GPU 资源。

## 9. 合并自 scene-2d-world-and-game-ui（2026-10-02）

原计划归档在 `../archive/scene-2d-world-and-game-ui/`（已完成项的记录与验证都在那里）。
`P0-contract-matrix.md` 随 P0 移到本目录。

继续有效的架构约束（原 §0 / §2.4，开工渲染项前读原文）：

- GUI compose 与 Scene sprite 是两条 graphics path：WidgetTree / UIFrameSnapshot 只进 GUI compose，
  authored sprite 是 SceneSnapshot candidate；Render3D 不依赖 GUI Compose（已落地，P0 只做回归守卫）。
- draw list 类型固定坐标系（`ScreenDrawList` 像素 / `WorldDrawList` 世界）；共享 pipeline 归持有
  `IRender` 的一方，录制器归画这个目标的一方。
- 纯 2D View 不支付 3D attachment / stage；不新增第二套 Scene scheduler / frame loop。
- 不新增 Transform2D / Camera2D / Node2D；正交只是 CameraComponent 的 projection mode。

未完成项并入评审步骤：

| 原条目 | 内容 | 并入 |
| --- | --- | --- |
| `pipeline_contract_audit` | Render2D 调用方与 owner 表 | 步骤 3 P0，已完成（`P0-contract-matrix.md` 开头的表即结论） |
| `mixed_world2d_visual_contract` | 混合 / 纯 2D / 只有 UI 三种输出的遮挡、顺序、混合、bloom、tone-map 预期，并可见验证 | 步骤 3 P0，契约已冻结（C1–C5，2026-10-03）；像素验收移到步骤 5 |
| `render2d_upload_submit_lifetime` | 同一提交内不覆写上传；被引用的 buffer / 资源活到 submit 与所需 fence | 步骤 5 合批核心的验收条件 |
| `legacy_pipeline_removed` | 删除 screen / world 混用的旧 shader 接口、`textureRef` 高位 mode 编码与陈旧调用方 | 步骤 5 收尾 |

## 10. Pixel-perfect camera（用户报告 2026-10-02）

不在评审 7 步里。小视口（约 596×419、500×350）下 16 texel/单位的世界发糊、闪烁：正交相机若按固定世界高度取景，最近邻且无 mip 的贴图会被非整数倍缩小，texel 被不均匀丢掉。用户选了 Unity 式 Pixel Perfect Camera，不要固定内部分辨率，也不要开 mipmap。

范围：挂在现有 `CameraComponent` 上的可选开关，默认关，3D 和旧场景不变。不新增 pass / shader / 渲染路径（§2、§9：P0 冻结前禁止；步骤 5 会重写精灵 pass）。投影用的输出尺寸是该 View 的 `outputRect`（`RuntimeGameViewProducer` 的 `renderResolution`、编辑器预览的 preview extent），调用点上已经有，不是窗口尺寸。

唯一函数 `resolveCameraViewFraming(camera, outputAspect, outputExtentPx)`，`buildCameraRenderMatrices` 用它生成 view / projection。渲染提取、`world.viewSize` / `world.viewAspect`、预览线框都走它，不复制公式。

- 字段：`_pixelPerfect`（默认 false）、`_pixelsPerUnit`（默认 16）、`_referenceHeightPx`（默认 192）。Town / House / TownLarge 显式打开，数值保持今天 1280×720 的取景。
- 只按高度取整数 zoom：`zoom = max(1, floor(viewHeightPx / referenceHeightPx))`。半高 = `viewHeightPx / (2 * pixelsPerUnit * zoom)`，半宽跟**真实**宽高比（pixel perfect 时不用 `_fixedAspectRatio`，否则横向会被拉开，texel 不再是正方形）。像素是正方形，所以横向的「设备像素 / texel」和纵向相同，不需要参考宽度；参考宽度只会改取景（letterbox 或多裁一刀），这一轮不引入。
- 视口比参考矮时 zoom 停在 1，半高变小，少看一些世界，绝不低于 1 设备像素 / texel。变大时 zoom 按整数上台阶。
- 今天脚本在 720p 的取景是 zoom 3、半高 7.5（`FollowCamera.lua` 的 16×12 格 = 192），不是场景里写死的 8。`_referenceHeightPx = 192`、`_pixelsPerUnit = 16` 与之相同。
- 相机眼位置在 `buildCameraRenderMatrices` 里吸附，不写回 Transform。步长是一个设备像素 `1 / (pixelsPerUnit * zoom)`。视口宽或高为奇数时，该轴再偏半个设备像素，让 texel 边界落在像素边上而不是像素中心（否则 419 这种奇数高会在边界上闪）。偶数尺寸相位为 0，与今天脚本的吸附一致。
- 精灵 / tile 的 `worldCenter` 在 `RenderFrameExtractor::extractSceneSnapshot` 里吸附到 texel 网格 `1 / pixelsPerUnit`。候选被所有 View 共享，不能按某一个 View 的 zoom 改坐标；整数 zoom 的设备像素网格是 texel 网格的细分，相机又吸附在设备像素上，所以精灵不会落在半个设备像素上。这一步在候选上，步骤 5 重写 pass 之后还在。
- `world.viewSize()`：主相机打开 pixel perfect 且是正交时，返回 `(halfWidth, halfHeight)` 世界单位；否则仍是呈现视图像素（旧契约）。`world.viewAspect()` 仍是宽/高；pixel perfect 时等于 `halfWidth / halfHeight`。跟随脚本用它钳制地图，不再自己写 `orthoHalfHeight`、不再自己算 zoom。
- 编辑器正交 XY 视口是 `FreeCamera`，不读这个开关。对话框不读相机。`_orthoHalfHeight` 在开关关闭时仍是半高（自动化回归场景不打开开关）。
