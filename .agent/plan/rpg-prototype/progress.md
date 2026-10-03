# Progress

## 2026-09-29 — 计划建立

- 评估结论：2D 对象沿用 `Node3D` + `Sprite2DComponent`，Game UI 沿用 yaui；缺的是 Tilemap、通行查询、
  实体查询与脚本互调、玩法切场景、帧动画，以及手写脚本绑定这笔架构债（见 plan §0、§1）。
- 用户决策 D1–D7 见 plan §3：tile 通行表先行、UE 式反射标记绑定且与脚本语言无关、场景即地图、
  新开本目录、与 game-ui / ui-behavior 线交替推进、绑定层分 B1/B2、地图用 Tiny Town + 角色图试生成。
- 从 `game-ui-script-framework` §7 接手「反射自动绑定 Lua」与「场景可编辑性」。
- 下一步：B1。开工前按 `session_checklist.md` 确认 game-ui S5/S7 当前是否在改 `GameplayLua.cpp`。

## 2026-09-29 — B1 完成

- 保留：标记（`.script()` / `.scriptReadOnly()`）、中立值与引用（`Core/Scripting/ScriptValue.h`）、
  按名字的导出面（`ScriptBindings.h`）、scene-core 的场景 / 实体 / 组件引用种类与实体原生方法、
  Lua（`LuaScriptBinding`）与 JS（`JSScriptingSystem`）两个后端都走中立层。
- 偏离：原计划「每个标记方法生成类型化调用器挂 `Function*` 扩展表」是与插件平行的调用路径；经用户确认改为
  D11——插件 `Function` 补记参数 / 返回 `type_index`、`Enum` 可装箱，脚本调用直接走 `Function::invoker`。
- 附带：`Scene` 删除移动构造（实体与实例表都持有场景地址）；`Entity` 只留 `add/removeComponentByName`
  （返回 `void*` / `bool`，未知类型名一律抛错），删除无人再用的 `has/componentByName` 与 `components()`；`ya_mcp_bridge.py` 的 `eval_js` 说明写明只暴露标记成员。
- 验证：`ya-testing` 1394 通过，唯一失败 `GUIWindowManagerTest.DragOverlaySessionIsExemptFromFocusAndInput`
  是既有红灯；`ya-gui-closure-test` 614、`ya-render-3d-test` 188、`ya-resource-core-test` 11、
  `ya-ecs-core-test` 1 全过；GreedySnake / HelloMaterial 运行与编辑器冒烟退出码 0，无 `[Script]` 跳过告警。
- 未完成：无。下一步 R0。

## 2026-09-29 — B1 修订：去掉可见性门禁 + Lua 按类型缓存（D12）

- 用户决策：没有「脚本不可访问」的需求，门禁整套删除——`.script()` / `.scriptReadOnly()`、`Meta::ScriptName`、
  插件 `FieldFlags::BlueprintReadOnly/ReadWrite/Callable/Pure`。反射了的成员类型能过边界即导出；
  const / 无可写访问器的字段只读。没有改名覆盖，`camera.primary` 在两个示例脚本里改为 `camera.bPrimary`。
- 中立层：`findMember` 换成常驻句柄 `findField / findMethod`；原生方法、解析器答出的方法与反射方法同表，只增不删。
- Lua：`LuaScriptObject` 不再是 sol usertype，改为 full userdata + 每类型一张懒建元表，首次命中把方法闭包 /
  字段句柄 `rawset` 进该类型缓存；方法闭包校验 self 类型；元表 `__metatable` 隐藏。sol 走 `sol_lua_push/get/check`。
- JS：只改用句柄 API，不缓存（用户决定）。

## 2026-09-29 — B1 后续：Lua 绑定层读性（用户反馈）

- 对象路径去 sol：建表 / 值转换全 raw C API，`sol::` 从 41 处降到 14 处，剩下的都在 Vec usertype
  注册区和 `sol_lua_push/get/check` 三个桥函数（其他模块经 sol 移动引擎对象的唯一入口）。
- `LuaScriptBinding.cpp` 文件头放机制图：推入（userdata + 按类型元表）、三个入口（__index/__newindex/方法闭包）、
  懒缓存；`pushObjectMetatable` 等栈操作收进按意图命名的工具（`setField*` / `setMarker`，内部先转绝对索引）。
- 途中教训：sol v3.5 的 usertype 元表懒定稿，注册时打的识别标记会在首次实例化后丢失；Vec 识别退回 sol 自己的检查。
- 验证：脚本相关 80 测试全过；GreedySnake / HelloMaterial 冒烟退出码 0，无脚本错误。

## 2026-09-29 — B1 后续二：脚本生命周期读性（B/C/D）

- B：受保护调用收敛为 `protectedCall`（push→protected_function→调用），`invokeLuaCallback` 只剩
  判空 + 记日志；`LuaScriptingSystem::invoke` 删掉手写舞步改调它。
- C：`reloadScript` 的单实例重载体（属性快照、destroy、重绑、回填、单批 init/start）提取为
  `reloadInstance(host, id, source)`；`_live.contains` 的 belt-and-braces 改为直接 `host->resolve`。
- D：`LuaScriptInstance::applyPropertyOverridesTo` 的 40 行 typeid if-chain 改为类型列表 fold
  （`makeLuaObject<T...>`）。`solToAny` 保持 if-chain——每类一行、本身就是表的直写形式，包一层反而变长。
- 用户同时确认不动 `init()`（A 项不做）。
- 验证：`ya-testing` 1395 通过（唯一失败仍是既有红灯）；GreedySnake / HelloMaterial 冒烟退出码 0。

## 2026-09-29 — R0：示例壳 + 按格走动 + 相机跟随

- 目标与边界：`Example/2DRpgPrototype` 跑起来，方向键 / WASD 按格走、四方向 + 三帧行走、相机平滑跟随且像素对齐；
  新脚本函数只经中立层，不新增 pass，不动 `world.spawnSprite` / `ui.*` 语义。
- 中立层模块函数：`registerModuleFunction / forEachModuleFunction`（同名覆盖、条目不搬家），Lua 投影成全局表闭包、
  JS 投影成 `ya.<module>.<fn>`；`ScriptApiRegistry` 头注释收窄为编辑 / 自动化 JSON 命令表。
  GameRuntime `registerGameplayScriptFunctions` 登记 `world.find(name)`、`world.viewSize()`，在 Lua / JS init 前；
  `GameplayLua` 的 `world` 表改为取或建，保住投影进来的函数。
- G12 修复：`TextureLibrary` 预建（Nearest / Linear）×4 寻址 sampler，`resolveSlotSampler` 按 `samplerConfig` 取。
- 途中发现并修复：精灵图像上下颠倒（共享 quad 的 `texCoord.y` 朝 +Y，`uvRect` 是图像空间）；只改 `Sprite2DWorld.slang`。
- 素材：生图后 `Tools/quantize_walk_sheet.py` 量化——生图不在整齐网格上，按连通区域找 12 个人物，
  原图先压 12 色调色板再逐块多数表决，脚底对齐格子底行；右向行 = 左向行镜像（生图的右向第 3 帧朝反了）。
  用户在 16×16 / 16×24 预览中选 16×24：精灵 1×1.5 单位，`Player.lua` 按 `sprite.size.y` 抬高半个超出量让脚踩格底。
  示例名按用户要求为 `2DRpgPrototype`（C++ 模块结构体仍叫 `RpgPrototypeModule`，标识符不能以数字开头）。
  Tiny Town 图集与草地 tile 拷入 `Content/Textures/`，`LICENSE.md` 记来源。
- 场景：`Town.scene.json` 草地一张大精灵（Repeat + Nearest 平铺），树 / 灌木 / 告示牌 / 栅栏为 atlas `uvRect` 精灵，
  玩家 z=0.1 在景物 0.05 之上。`FollowCamera.lua` 取最大整数缩放使纵向至少 12 格，`orthoHalfHeight = 视口高 / (2×16×缩放)`，
  相机位置吸附到屏幕像素；`Player.lua` 位置吸附到 texel。
- 验证：`ya-testing` 1442 全过（新增 `ModuleFunctionsReplaceInPlaceOnReRegistration`、
  `ModuleFunctionsBecomeGlobalTables`、`ModuleFunctionsExportUnderYa`）；2DRpgPrototype 运行 / 编辑器、GreedySnake、
  HelloMaterial 冒烟退出码 0，截图确认最近邻与朝向。
- 未完成：按键注入自动化（等 S7 `input.inject_key`）。下一步 R1。

## 2026-09-29 — R1：Tilemap（数据 + 渲染 + 编辑笔刷 + 通行查询）

R1a / R1b / R1c 共享 `TilemapComponent`、`Town.scene.json` 与 tileset 文档，R1b / R1c 改的正是 R1a
引入的文件，所以落在一个提交里；下面按三段记录。

### R1a：Tileset 资产 + TilemapComponent + 渲染

- 目标与边界：手写一份带 tilemap 的场景，Play 看到地面 / 装饰 / 遮挡三层，玩家在地面与遮挡层之间；
  不新增 pass（复用 Sprite2DStage），不做查询 API（R1c）、不做笔刷（R1b）、不加脚本函数。
- 数据：Tileset（atlas TextureSlot + tile 几何 + 稀疏 solid 表）存 .yatileset.json，TilesetRef
  照 TextureRef 的样子只序列化 path、缓存解析结果，同步加载（小 authoring JSON，同 .yaui 待遇）；
  TilemapComponent 与 Sprite2DComponent 同模块：tileset / cellSize / width / height /
  layers（name/zOffset/cells，左下起按行，0 = 空否则 tile+1）/ layer。
- 渲染：纯函数 appendTilemapCandidates 把范围内非空格展开成 WorldSpriteCandidate（entityId 取
  tilemap 实体，layer 取组件 layer，sortOrder 取层号）；extractor 在 snapshot 里全量展开（与精灵
  一致，View 裁剪留给 R4 定），无内边距图集 uv 半像素内缩；resolve 走 GameplayResourceBinding
 （tileset 文档同步 + atlas 贴图异步），未就绪产出零候选（无占位图规则与精灵一致）。
- 内容：Content/Tilesets/town.yatileset.json（tiny_town 图集，Nearest + ClampToEdge，solid 记树冠 4
  与树干 16）；Town.scene.json 加 1022 号 TilemapGround（6x5，origin (-5,-2,0.01)，overlay 层
  z=0.21，玩家 0.1 在 decor 0.04 与 overlay 之间；玩家出生格 (5,2) 是草地）。
- 保留 / 偏离：snapshot 层不做 View 可见裁剪（与精灵一致；纯函数留了 cell range 参数给 R4 的
  frustum / chunk 缓存）；Tileset 文件热重载不在 R1a（改文件需重载场景，笔刷改的是组件数据即时生效）。
- 途中发现（引擎既有行为，非本计划改动）：反序列化 TextureRef 会立即触发贴图加载，需要 App 上下文
  （VFS + AssetManager）；测试里只能验到 TilemapComponent 子对象与 tileset 文件结构，整场景加载由冒烟覆盖。
- 验证：新增 5 测试全绿（TilemapComponentTest.RoundTripsLayers、TilemapExtractionTest x2、
  TilemapSceneTest x2）；ya-render-3d-test 191 全过（基线 188+3）；
  ya-testing 1381 通过，23 失败全是 AppAutomationControl 系 TCP 回环被本机沙盒拒绝（环境性，
  与 tilemap 无关）；运行 / 编辑器冒烟因沙盒无显示器无法执行（SDL 起不来），改用文件级验证代替。
- 未完成：冒烟待有显示器环境补跑。

### R1b：编辑器 Tile 笔刷

- 目标与边界：选中 tilemap 后在正交 XY 视口里画 / 擦 / 矩形填充 / 吸管，一笔一个撤销步，保存重开仍在；
  Inspector 可改宽高 / 每格尺寸 / tileset 且改宽高保留已有格子。不做通行查询（R1c）、不加脚本函数、
  不新增 pass。
- 引擎侧 cell 编辑（`TilemapComponent`）：`resize` / `setCell` / `fillRect` 只改组件数据；`onEdit()` 用
  `_editWidth/_editHeight` 这份 transient 旧尺寸做按行重排，所以 Inspector 把宽 6 改成 10 时左边 6 列原样保留，
  不是按线性前缀硬搬；`onPostSerialize()` 同景；图层 cell 数与 width*height 不符时提取照样跳过该层。
- 撤销：`EditorTilemapUndo`（`FTileLayerSnapshot` + `pushTileLayerUndo`）推的是关卡根会话的同一个 `UndoStack`
  （与 `EditorTransformUndo`、Inspector 属性编辑同栈），一笔按下到松开存整层前 / 后 cell；前后相同不记步。
- 笔刷：`EditorTileBrushController` 管 Move/Paint/Erase/RectFill/Eyedropper、矩形图章、相邻格间 Bresenham 连线、
  press 时抓一次 `_strokeBefore`；`screenToCell` 走 view×projection 反投影再乘 tilemap 世界逆矩阵；
  网格线与悬停 / 矩形框走编辑器 overlay 的世界绘制列表（`recordWorldOverlay`），不加新 pass。
- 接线：`EditorSurface` 把 `EditorViewportGizmoOverlay` 的 brush 指向 `EditorLayer::tileBrush()` 并挂同一 UndoStack；
  `EditorViewportGizmoOverlay::dispatchEvent` 在 brush 处于「已上膛 + 正交 XY + 选中 tilemap」时优先于 gizmo 接管左键，
  `wantsPointerCapture()` 把进行中的笔画算进去，`isActive()` 也一并算上，笔画期间编辑器不会把鼠标当相机导航。
- Tile Palette tab：`EditorTilePaletteTab`（工具行 / 图层下拉 / 图集网格）+ 内嵌 `UITileAtlasGrid`（点选或框选图章，
  拖拽期间自己 setPointerCapture）；注册进 `EditorTabSpawnerRegistry`（tabId `tile-palette`，归关卡编辑器根会话）。
- 顺带：`Tileset` 从 Render3D 下沉到 Core（`Core/Common/Tileset.h`），`TilesetRef` 接进 asset-ref 名单
  （`isAssetRefType` / `resolveAssetRef` / `hasAssetResolveError` / `registerAssetRef` / picker 各分支 + `openTilesetPicker`），
  Inspector 的 tileset 行因此是资产行而非纯文本行。
- 偏差：plan 写「图集网格」为 tile 单选 / 框选，实现一致；未做笔画中右键取消的 UI 入口（`cancelStroke()` 已具备，
  等有明确按键约定再接）。
- 途中修复（本轮新引入，非既有 bug）：`EntityIdPass.h` 用了 `DrawCandidateView` 却只做前置声明、没包含定义它的
  `RenderFrameData.h`；新增 `TilemapComponent.cpp` 改变 unity 分组后暴露，补上 include 即解。
- 验证：`ya-testing` 1386 通过 / 23 失败（22 个 `AppAutomationControl*` 走 TCP 回环被沙盒拒绝、
  1 个 `GUIWindowManagerTest.DragOverlaySessionIsExemptFromFocusAndInput` 需显示器，均环境性）；
  Tilemap 相关 10 项全过（`TilemapEditTest` x3、`TilemapComponentTest`、`TilemapExtractionTest` x2、
  `TilemapSceneTest` x2、`TilemapEditUndoTest`、`TilemapInspectorTest`）；`ya-render-3d` 与 `ya-game-editor` 编译通过。
- 未完成：编辑器冒烟（需显示器，沙盒内 SDL 起不来）。

### R1c：通行查询 + 被墙挡住 + 相机限界

- 目标与边界：玩家被地图挡住、方向键在地图边界停下；相机跟随但画面不越出地图。
  不加碰撞体 / 物理、不新增 pass、不加脚本专用的平行接口。
- 查询面（D1）：`TilemapComponent` 四个反射方法 `worldToCell` / `cellToWorld` / `isSolid` / `bounds`，
  四个都能过脚本边界，所以玩家与以后的 NPC 走同一条路，没有第二套绑定。
  - `isSolid(x, y)`：任一图层该格是 tileset 的 solid tile 即阻挡；**出界也算阻挡**，地图边缘因此
    像墙一样挡住玩家，不用在脚本里再写一遍范围判断。
  - `worldToCell` / `cellToWorld` / `bounds` 都带 owner 的 Transform（组件经 `getOwner()` 取，
    `TransformSystem::computeWorldMatrix` 递归算父级）；没有 owner 时退化为 identity，纯数据测试
    与提取路径不必造 Scene。
  - 「小接口」（D1 原话）在这里读作「窄的能力面」：四个名字够用，且实现只有 tile 一种。
    真正的「谁是场景的碰撞权威」这件事没有抽象——现在只有一个地图、一个调用方，抽象是投机；
    R2a 的 NPC 或 R3 的换图成为第二个调用方时再决定接口形态（见「未完成」）。
- 玩家（Player.lua）：不再自带一套整数格。它的 cell 直接是 TilemapGround 的 cell，
  位置由 `cellToWorld` 给出，通行由 `isSolid` 判定——**地图是碰撞权威**，编辑器画的墙不用改脚本就生效。
  这次重写顺带修掉了 R0 遗留的隐式约定：旧版玩家格与地图格差半格，靠 `+5 / +2` 这类魔法偏移才对齐，
  现在两边用同一个格定义。
- 相机（FollowCamera.lua）：按 `bounds()` 把取景矩形夹在地图内；地图某轴比视野还窄时该轴取中，
  否则 clamp 会和跟随互相拉扯、在边缘抖动。取景仍吸附到整屏幕像素。地图缺失时只跟随、不夹取，
  R3 换图不会先崩在限界上。
- 内容重建：地图从 6x5 扩到 32x20（origin 由 (-5,-2) 改为 (-16,-10)），理由——原地图比可视范围还小，
  「相机停在地图边界内」这条验收根本演示不出来。按 D3「场景即地图」把原先散落的精灵收进地图：
  草地精灵（`Ground`）、树冠 / 树干（5 棵树的 Top/Trunk 共 10 个精灵）、灌木 / 蘑菇 / 告示牌、
  4 段栅栏共 13 个实体合并成 Ground / Decor / Overlay 三层，场景实体从 21 降到 3（Camera / Player /
  TilemapGround）。tileset 的 `solid` 表补上栅栏 80–82（原本只有树冠 4 与树干 16），
  所以「栅栏挡住玩家」也是地图数据而不是脚本判断。玩家出生格改为地图中心格 (16,10)，
  与 footLift 对齐，首帧不动。
- 偏差：计划说查询面「抽成小接口」，实现为组件上的窄能力面而不是 C++ 抽象基类，理由如上；
  如果要在 Box2D/Jolt 之间切换，那时需要的是「碰撞权威的发现」（谁回答查询）而不是这四个数学方法，
  一并留到第二个调用方出现时决定。
- 验证：`ya-testing` 1391 通过 / 23 失败（22 个 `AppAutomationControl*` 需 TCP 回环、
  1 个 `GUIWindowManagerTest` 需显示器，均环境性）；新增 `TilemapQueryTest` x4
  （`SolidTileBlocks` / `OutOfBoundsIsSolid` / `WorldToCellUsesTheOwnerTransform` /
  `OwnerlessMapUsesIdentityTransform`）与 `LuaScriptBindingTest.TilemapQueryFaceReachesLua`
  （反射签名确实能过脚本边界）；`TilemapSceneTest` 断言随新内容更新；两个 Lua 脚本用 lua 5.4
  `loadfile` 语法检查通过；`ya-game-runtime` / `2DRpgPrototype` / `ya-game-editor` / `ya-testing` 编译通过。
- 未完成：编辑器冒烟（沙盒无显示器，SDL 起不来）；相机限界与「画墙→被挡住」只有手测路径，
  没有自动化（等 S7 的 `input.inject_key`）；「谁是碰撞权威」的发现留到 R2a/R3。下一步 R2a。

## 2026-09-30 — R2a：事件实体与交互

- 目标与边界：编辑器摆 NPC 与告示牌（精灵 + 各自脚本），玩家面朝按确认键 → 其 `onInteract`
  被调用；NPC 占住的格子走不进去。不碰 `world.spawnSprite` / `ui.*`（game-ui 线所有）、
  不新增渲染 pass、不造事件总线；对话框归 R2b。冲突检查：`ui-behavior-capabilities` 到 C1d
  （C2/C3 未动工，R2a 先定具名调用接口，C3 复用）；`game-ui-script-framework` 停在 S3b。
- 逼出来的引擎改动（对应游戏需求）：
  - `entity:call(name, ...)` ← 玩家要调目标脚本函数。`LuaScriptingSystem::callNamed`
    （实例面：host bindSelf 后 `self:<name>(args...)`，带返回值）+ `callEntityScript`
    （实体面：走查 `LuaScriptComponent::scripts`，第一个定义该名字的已加载脚本作答，
    `rebindEntitySelf` 复用 FEntityScriptHost 的 self.entity 绑定）。init() 以无捕获
    原生方法挂在 Entity 类型上——Lua / JS 共用，且重复 init（测试）不会重绑悬垂系统。
    语义：未定义返回 nil；目标脚本出错抛 ScriptError 到调用方（错误不能伪装成安静 nil）。
  - `map:entityAt(x, y)` ← 玩家要找面前格子里的 actor、进门格前查占位。原生方法注册在
    `GameplayScriptFunctions.cpp`（GameRuntime 才同时看得见 Scene 与组件）：返回格子里
    第一个「有 Sprite2D 又有 LuaScript」的实体，逐候选问地图自己的 `worldToCell`，
    相机 / 纯变换实体永不作答；线性遍历（D-T4），量大再加索引。tilemap 组件保持在
    Render3D 看不见场景，注册点不放那里。
  - 「角色 z = 图层基准 − y × ε」← 玩家与 NPC 同层时按屏幕上下正确遮挡（G2 不透明深度）。
    收进示例 `Content/Scripts/Actor.lua`（`Actor.zFor` + hero 图帧算式），三个脚本共用；
    约定与「地图即碰撞权威」「entity:call 互调」写进新 skill `.agent/skills/2d-gameplay/`。
- 内容：`Actor.lua`（新）、`Player.lua`（tryStep 加 `isSolid(...) or entityAt(...)`；
  确认键 Space/Enter/E 边沿（`input:isKeyPressed`）触发 `target:call("onInteract")`；
  z 改按 Actor.zFor 随 y 重算）、`Npc.lua`（新，onInteract 转身朝玩家 + print）、
  `Sign.lua`（新，onInteract 变暖色 + print，替 R2b 的文字占位）；`Town.scene.json`
  加 Sign（1022，tiny_town tile 5 改成精灵放回 (13,5) 格，Decor 层该格清零）与
  Npc（1023，hero 图调蓝放在 (14,9) 格）。
- 保留 / 偏离：
  - 「扩展 S1 的 call」落地为并行的 callNamed（invoke 的 bool 过滤语义与其三个调用方原样保留）；
    C3（Lua call 热路径）落地时复用同一入口。
  - entityAt 的 actor 判定（精灵+脚本）是引擎面约定的最小实现；将来「隐形触发区」需求
    出现时再放宽，届时一并评估 D-T4 的索引。
  - 计划写「两个以上脚本重复这段时下沉成组件字段」：z 约定现在是 Player/Npc/Sign 三处
    require 同一个 Actor.lua（游戏内容内复用），尚未到引擎组件；组件字段属于 authored
    sprite 能力（scene-2d 线），出现第四处重复再评估。
- 验证：新增 8 测试全绿——`LuaScriptHostTest.NamedCallReturnsValue` /
  `NamedCallOnMissingFunctionIsNil` / `NamedCallPropagatesTargetErrors`、
  `LuaEntityScriptCallTest` x3（第一个定义者作答、参数/nil、目标错误在调用方可见）、
  `TilemapEntityQueryTest` x2（actor 命中、非 actor 静默）；脚本 / tilemap 相关 90 项全绿；
  `ya-testing` 1421 通过，唯一失败仍是既有红灯
  `GUIWindowManagerTest.DragOverlaySessionIsExemptFromFocusAndInput`；
  2DRpgPrototype 运行 / 编辑器与 GreedySnake 冒烟（--exit-after-frame=120）exit 0、
  无脚本错误；全部脚本 luajit loadfile 语法检查通过。
- 手测步骤（验证「面朝 + 确认」）：Play 后走到告示牌（(13,5) 格）或 NPC（(14,9) 格）旁，
  面朝它按 Space/Enter/E：NPC 转身朝玩家并在控制台打印欢迎；告示牌变暖色并打印；
  走进 NPC 所在格被挡住；站在 NPC 上方时 NPC 遮住玩家、下方时玩家遮住 NPC。
- 未完成：按键注入自动化（等 S7）；R2b 对话框。下一步 R2b。

## 2026-09-30 — R2b：对话框

- 目标与边界：交互弹出对话框、逐字显示、确认键翻页/关闭、对话期间玩家不能移动。
  S4 未落地 → 移动锁由玩法状态控制（S4 落地后迁移 modal）；不改 `ui.*` / `world.*`
  语义（game-ui 线所有）；无引擎代码改动，全部落在示例内容。
- 内容（`Example/2DRpgPrototype/Content/`）：
  - `UI/Dialogue.yaui.json`：根 panel（Hidden）挂 `script.lua` behavior，Box/Body/Hint
    三个子控件全部平铺在根下用 canvas 槽（底部居中 880×160 卡片 + 内嵌正文 + 右下键位提示）。
  - `UI/Dialogue.lua`：`say(lines, onDone)` / `busy()` / 逐字（`self:every` 0.02s 一字，
    `revealTimer:cancel()` 收尾）/ 确认键三段（补完本页 → 翻页 → 关闭）；开框帧
    （`time:getFrameIndex()`）屏蔽确认键，起始那次按键不会跳过第一页。
  - `Town.scene.json`：`widgetEntries` 加 Dialogue 条目（autoMount，rootSlot 抄 GreedySnake
    全屏 canvas）。
  - `Npc.lua` / `Sign.lua`：onInteract 保留各自的转身/变色反应，改为经
    `ui.get("Dialogue"):say(LINES)` 说话。
  - `Player.lua`：每帧 `dialogue:busy()` 决定 `talking`，说话时锁移动与交互；确认键
    「再武装」——按下触发后必须完全松开才允许下一次，同一次按键不会同时推进对话又再次触发。
- 途中发现（引擎既有，非本计划改动）：
  - **yaui 槽格式必须匹配父控件**：给 `engine.border` 的子控件写 canvas 槽会在
    `UIContentSlot::deserialize` 断言崩进程（`UILayout.cpp` 的 content/overlay 槽反序列化
    用无守卫的 `node["padding"]`）。格式不匹配应是可读的错误，不是 assert——健壮性债，
    留给 gui 线；本次内容改为平铺根 panel（canvas 槽，GameOver 已验证的形状）。
  - **JS `ya.entity.get(id)` 按句柄不按名字**：字符串被 `JS_ToUint32` 归 0，拿到的是
    第一个实体（Camera）。自动化里用 `ya.entity.list()` 按名字过滤再调用。
  - **Lua 全局是小写 `time`**（`LuaTimeApi` 实例），大写 `Time` 是 usertype 表，
    在其上调方法报 "received nil for self"。
- 验证（自动化控制口真实验证）：`ya.py control start --game` 起实例，`eval_js` 按
  `ya.entity.list()` 过滤出 Npc/Sign 调 `call("onInteract")`，`capture_screenshot`
  （presentation 目标）截图确认——对话框底部居中弹出、逐字显示进行中、右下键位提示、
  NPC 转身朝玩家；对已打开的框再次 `say` 正确替换页。翻页/关闭需要确认键，留手测。
  另：2DRpgPrototype 运行/编辑器与 GreedySnake 冒烟（--exit-after-frame=120）exit 0
  无错误；`ya-testing` 1421 通过（唯一失败仍是既有显示器红灯）；脚本 luajit 语法检查通过。
- 手测步骤：面朝 NPC/告示牌按 Space → 弹框逐字；再按一次 → 本页立即补完；再按 → 翻页；
  末页再按 → 关闭、恢复移动。对话期间方向键应无效。
- 未完成：翻页/关闭的按键自动化（等 S7 `input.inject_key`）；移动锁迁 S4 modal。
  下一步 R3（切换地图与跨场景状态）。

## 2026-09-30 — R3：切换地图与跨场景状态

- 目标与边界：走进门切到 House 场景站上出生点、出来回到门口、宝箱状态跨场景保留。
  不合并自动化 `scene.load` 与玩法 `world.loadScene` 两套 API（共用 SceneManager::loadScene
  底层）；不做实例索引优化。冲突检查同前两条线未动本处。
- 引擎改动（对应游戏需求）：
  - `world.loadScene(path, spawnName)`（GameplayScriptFunctions 注册）← 门脚本要换图。
    只排队（`AppSceneServices::requestSceneTransfer`，同帧后者覆盖），**在帧尾结构变更阶段执行**
    （orchestrator `flushStructuralChanges` 末尾）：一次转移会停掉全部脚本，绝不能在脚本
    运行中内联发生。执行 = waitIdle → 旧场景 onStop → SceneManager::loadScene → 出生点
    放置；**保持 play 会话**（app 状态、UI host、常驻表都不动——走 stopRuntime 会让编辑器
    误以为退出 play，也会把常驻表一起清掉）。
  - 显式常驻表 `Persist`（LuaScriptingSystem::persistentState/resetPersistentState）←
    宝箱状态要跨图保留但不跨局泄漏。init 时绑定全局；**只有 stopRuntime/stopSimulation
    （退出 play）换新表**，转移路径不碰它。普通 Lua 表，脚本可存任意值。
  - `placePlayerAtSpawn(scene, spawnName)`（自由函数）← 出生点放置。约定：玩法主角实体
    名为 "Player"，出生标记是场景根上的 Node3D，只取其 x/y（z 归角色自己）。
- 内容：`Door.lua`（propertyOverrides 授权目标场景与出生点，Inspector 可改）+ `Chest.lua`
  （Persist.opened[实体名] 记开态，关=107 号罐/开=94 号金罐换图）+ `House.scene.json`
  （10×7 室内：石板地 108、墙 99 入 solid、门、箱、SpawnFromTown、玩家、相机、Dialogue 条目）
  + Town 加 DoorToHouse 与 SpawnFromHouse + `Player.lua`（tryStep 先问
  `occupant:call("blocksEntry") ~= false` 才阻挡——门可穿行、罐子阻挡、无作答=阻挡；
  到达新格 `pokeArrival` 触发 `onPlayerEnter`，查询用 `entityAt(x, y, self.entity)` 排除自己）。
- 途中发现（内容侧三连坑，全部真实踩到）：
  - **cell 值 = tile 序号 + 1**：手写 House 时把裸 tile 序号填进 cells，整个地图错位一格
    （isSolid 探测的"异常"其实是它，不是 tileset 解析问题）。
  - **TilesetRef 的 JSON 形状**是 `{"__base__": {"AssetRefBase": {"_path": ...}}}`，
    不是 TextureSlot 那套 bEnable/samplerConfig（写错时 ReflectionSerializer 静默丢弃，
    只有 Warn）。
  - **每个场景的行走地图实体必须叫 `TilemapGround`**（Player.lua 按名字找地图）——写进
    skill 约定；违反时 onInit 报错、字段全空、onUpdate 每帧刷错。
- 验证（自动化控制口端到端）：`eval_js`（`ya.entity.list()` 按名过滤，`ya.entity.get`
  按句柄不按名）触发门 → 截图确认 House 渲染完整、玩家站在出生点 → 开箱（金罐 + 对话）→
  回 Town → 再进 House → **金罐仍是打开的**。往返切换共 3 次全成功。另：SceneTransferTest
  四项全绿（SpawnPointPlacesPlayer / PersistentStateSurvivesTransfer /
  TransferWithoutSpawnKeepsAuthoredPositions / PlayStopClearsPersistentState，App 用
  AppModuleTestAccess 无头组装 + 临时目录场景文件走真 loadScene）；`ya-testing` 1425 通过
  （唯一失败仍是显示器红灯）；2DRpgPrototype / GreedySnake 冒烟 exit 0；脚本 luajit 语法通过。
  注意：control 实例有约 220s 的寿命上限（防遗忘自杀），长验证中途会被掐，需要续命就重启。
- 未完成：按键注入自动化（等 S7）；`world.loadScene` 的 spawn 只放 Player 一名实体。
  下一步 R4（规模与定案）。

## 2026-09-30 — R4：规模与定案

- 目标与边界：64×64、3 层地图 + 20 NPC，量 profile 构建下的帧时间与 draw 数；
  **只产出数据与决策记录，不改渲染**。完整数据、方法与复现命令见 `r4-measurements.md`。
- 夹具：`Content/Scenes/TownLarge.scene.json`（64×64；Ground 4096 / Decor 776 / Overlay 241 格，
  20 NPC + 玩家；5120 个 tile 候选 + 21 精灵候选），由 `Tools/make_scale_scene.py` 确定性生成
  （`--size/--npcs/--out` 可造其它尺寸，用于斜率与对照）。
- 测量手段：profile 构建 + runtime CPU trace（speedscope），帧根取 `iterate`（600 帧，跳前 60）；
  分析脚本 `.agent/plan/rpg-prototype/measure_trace.py`（flat self 表 + inclusive 表 + 最热帧树）。
  读数前提写进了记录：默认帧限速（120fps）会把小场景的绝对帧时间夹住，只有超出预算的场景
  （TownLarge 10.56ms）帧时间可用；`Tick/FpsControl` 的 self 就是限速 sleep。
- 数据（profile，ms/帧）：三点拟合 `recordFamily ≈ 1.14ms + 1.23µs × 候选数`
  （700→1.77 / 1329→3.05 / 5134→7.41，残差 ±3%，点对点边际 1.15~2.04µs）；
  窗口 1024×768→512×384（像素 1/4）帧只降 7%，`recordFamily` 不变、`Render/Frame` self 降 0.84ms
  → 瓶颈是逐候选 CPU 录制而非填充；提取（5134 候选）0.10-0.15ms；Lua 0.015-0.04ms。
- 决策（交 `scene-2d-world-and-game-ui`）：①合批/实例化**需要**且优先级最高（唯一随内容
  线性增长的成本项，纹理高度集中）；②16 张纹理表当前不是约束，但每帧重建 + 逐候选线性查找
  要随合批改成直接映射；③纯 2D View 绕开 Deferred 按正确性推进、不以性能为由（收益 ~1ms 级）；
  ④tile 区块缓存现在不需要（提取是提交的 1/50，4× 面积再评估）。
- 未决（已列交接）：`Render/Frame` 的 self 时间（4.3/5.0/2.9ms，与候选数不单调，疑似
  `beginRecordedFrame` 的帧栅栏等待或 offscreen pump）在 CPU trace 里无法归因；draw 数是
  按实现规则 + 场景数据推导（环境无 RenderDoc），长期量化建议加只读诊断计数走 automation。
- 验证：`TownLarge` 在 profile 下跑通 600 帧 exit 0；测量脚本对三个夹具 + 尺寸对照共 4 条
  trace 均可复现；测完已恢复原构建模式（debug）。
- 未完成：B2（Lua 插件化与旧绑定收口，排在 game-ui S7 之后）。R0–R4 全部落地。

## 2026-09-30 — 评审与双面 quad 修复

- 评审（R0–R4 六个问题）写进 `review-2026-09-30.md`，未排期待办列在 `todo.md`；资源层每帧 resolve
  另立计划 `../archive/resource-handle-events/`（已归档）。
- 修复：共享 `EPrimitiveGeometry::Quad` 带正反两套绕序（`createFullscreenQuad` 12 个索引，背面给编辑器
  放置的 Quad 网格用）。三个使用方设 `cullMode None`，每个像素画两遍：`Sprite2DStage`（半透明精灵
  混合两次）、`LightStage`（延迟光照全屏 pass 片元开销翻倍，画面不变）、`ViewOverlayStage` billboard
  （编辑器图标混合两次）。三处都改为剔除背面：从任意方向看恰有一套绕序朝前，仍双面可见、只画一次，
  与视口 Y 翻转无关。几何不动。
- 验证：ya-testing 1469 全过；HelloMaterial 前后截图逐像素一致（光照 pass 不混合，符合预期）；
  2DRpgPrototype 前后截图仅物体轮廓差异（两次运行跟随相机约 1px 偏移，场景精灵均为 alpha 裁剪不透明）；
  2DRpgPrototype 编辑器、GreedySnake 冒烟 exit 0。
- 保留：GPU 侧收益未量化（环境无 GPU 计时）；半透明精灵的颜色修正没有现成场景可截图对照。

## 2026-09-30 — `Render/Frame` 归因

- 给 `RuntimeRenderContext::tick` 里没有 scope 的步骤补了 `YA_PROFILE_SCOPE`，
  并在 `VulkanRender::beginRecordedFrame` 内拆出 `waitFrameFence` / `DeferredDeletionQueue::flush`；
  在 `prepareDerivedState` 和 Terrain / GameplayBinding 两个处理器外包了 scope（资源计划 H1 的基线要用）。
  误名 scope `RenderFrameExtractor::sceneSnapshot`（实际包的是 `prepareViews`）改名为 `Render/PrepareViews`，
  真正的快照抽取加上 `Render/ExtractScenes`。
- 结果（`r4-measurements.md` §7）：`Render/Frame` self 从 2.9–5ms 降到 0.006ms；TownLarge 上的主体是
  `waitFrameFence` 2.18ms（CPU/GPU 串行，归 M4），`SubmitPresent` 0.52ms；资源准备 0.011ms/帧。
- 验证：profile 构建 TownLarge / Town 600 帧 exit 0；已切回 debug；ya-testing 1469 全过；
  HelloMaterial 运行时 / 编辑器、2DRpgPrototype 冒烟 exit 0。
- 保留：MoltenVK 下 `tickGpuMs` 恒 0，GPU 时间只能由栅栏等待反推；`FPSControl` 与配置
  `fpsLimit: 60` 对不上的现象未查。

## 2026-10-02 — 清空 AssetManager frame task sink

- `App::quit` 在 `TaskQueue::stop()` 之后调用 `AssetManager::setFrameTaskSink({})`。
  stop 会 join worker 并丢掉尚未执行的主线程完成回调，异步加载此后不再投递；
  清空排在 `onQuit` / 场景卸载 / `_deleter` 之前，App 仍然有效。之后的
  `dispatchToGameThread` 走无 sink 的内联回落。
- 没有把 sink 从文件静态改成 `AssetManager` 成员：管理器是 Meyers 单例，进程级寿命
  比 App 更长，搬到成员上不结束对 `&app` 的捕获，真正的修复是对称清空。
- `LinkageFramework::setFrameTaskSink` 安全，未改：sink 是实例成员，system 在
  `quit` 的 `_deleter` 里销毁（此时 App 仍有效），且 `shutdown` 先置取消标志。
  已入队的延迟任务不捕获 App。
- 验证：`ya-resource-runtime-closure-test --gtest_filter=AssetManagerFrameTaskSink.*`
  1 通过。
- 偏离：无。`resource-leftovers.md` 已删掉这一条。

## 2026-10-02 — 合并 callNamed 与 invoke

- 只留 `LuaScriptingSystem::callNamed(instance, name, ScriptArgs, ENamedCallError)`，
  返回第一个 `ScriptValue`。`ENamedCallError::Throw` 抛 `ScriptError`（`entity:call` /
  `callEntityScript`）；`Swallow` 记日志并返回空值（`onShow` / `onHide`）。
  “返回 true 表示已消费”由调用方自己看返回值。删掉 sol 参数版 `invoke`。
  生命周期 `call(instance, ELuaScriptCallback, dt)` 未动。`ScriptApiRegistry::invoke`
  与 `JSScriptingSystem::invoke` 未动。
- 验证：`ya-testing --gtest_filter=LuaScriptHostTest.*:LuaEntityScriptCallTest.*:GameUIScriptTest.*`
  27 通过，含 `NamedCallSwallowContainsTargetErrors`。
- 偏离：无。

## 2026-10-02 — 脚本 callMethod 接入编辑漏斗

- `ScriptRefKind` 增加 `afterCall`，由 `script::callMethod` 在反射方法成功返回后调用。
  只对非 const 方法触发；const 来自已有的 `Function::isConst()`（`Class::function` 对
  `Ret (T::*)(Args...) const` 的重载，不是手工标注）。组件引用的 `afterCall` 与
  `afterWrite` 走同一个 `notifyComponentEdited`。方法自己已做本地更新，不再补
  `onPostSerialize`。原生方法没有 const 记录，不发通知（`entityAt` 是查询）。
  Lua / JS 都走这一层。
- 验证：`ya-testing --gtest_filter=LuaScriptBindingTest.*` 9 通过，含
  `ComponentMethodCallRoutesThroughTheSceneEditFunnel`（`getPosition` 不通知，
  `setPosition` 通知一次）；`ScriptBindingTest.*` 8 通过。
- 偏离：无。`setModelPath` / `AssetRef::setPath` 没有 `YA_REFLECT_METHOD`，脚本
  今天调不到它们；漏斗覆盖的是已经反射出来的非 const 方法。`resource-leftovers.md`
  已删掉 callMethod 这一条。

## 2026-10-02 — input.inject_key 与 R0–R3 按键自动化

- 注入走平台键盘同一条路：`OsEventPump::emitKey` 造 `KeyPressedEvent` /
  `KeyReleasedEvent`，命令里立刻 `App::dispatchEvent`（这一帧的 poll 已经结束，
  排到下一轮 poll 的话脚本看不到）。`hold` 的抬起进 `enqueueKey`，由
  `SdlEventSource::pollEvents` 在后续帧 poll 开头排空，和 SDL 事件共用 emit。
  按下后同一帧 `wasKeyPressed`（Lua `isKeyPressed`）为真，`preUpdate` 之后为假，
  `isKeyPressed`（Lua `isKeyDown`）保持到抬起。键名用已有的 `keyFromName`
  （`Right` / `Space` / `W`），没有新表。
- 命令：`input.inject_key`，参数 `{key, action: "down"|"up"|"hold", frames?}`。
  `hold` 要求 `frames >= 1`：现在按下，过 `frames` 个逻辑帧在 poll 里抬起。
  返回里带 `down` / `edge`，表示事件已经进了 `InputManager`。
- 断言对话需要读挂载控件，现有命令做不到，加了 `ui.query {entry, widget?}`
  → `{name, visible, text?}`。
- 端到端（`python3 Script/ya.py control`，`--game`，Town 启动场景）：
  `Script/automation/2d-rpg/walk_one_cell.py` 右走一格 `(0.5, 0.75) -> (1.5, 0.75)`；
  `blocked_by_wall.py` 从 (16,10) 向下五格到 (16,5)，再向下被栅栏挡住，位置停在
  `(0.5, -4.25)`；`dialogue_page.py` 走到 NPC 上方、面朝下、Space 打开对话并翻页，
  正文从 `Welcome t` 变成 `Mind the fences`。三个脚本 exit 0。
- 验证：`InputInjection.*` 1 通过；全量 `ya-testing` 1509 例、1508 过、1 跳过
  （`RHISurfaceContext.ExtraWindowUnpresentableDoesNotBlockStartupWindowPresent`）；
  GreedySnake 与 2DRpgPrototype `--exit-after-frame=120` 退出码 0。
- 偏离：自动化 `scene.load` 会 `stopRuntime` 且不重新 `startRuntime`，脚本因此不 tick。
  端到端不重载场景，用项目默认的 Town。`ui.query` 不在原步骤里，没有它断言不了对话正文。

## 2026-10-02 — 合并 scene-2d-world-and-game-ui

- 2D 世界只剩一条线：`scene-2d-world-and-game-ui` 归档到 `archive/`（归档理由 2：
  被本线接管）；`P0-contract-matrix.md` 移到本目录。原线继续生效的约束收在
  plan.md §9，四个未完成矩阵项映射到评审步骤 3 / 5（见 §9 表与 feature_matrix）。
- 归属转移（plan.md §5）：命名调用原语、`input.inject_key` 归本线；game-ui S5/S7
  仍定 `onKey` / `spawnSprite` / `ui.*` 的语义，但新形态注册在 B1 中立层；C3 只管
  `call(ELuaScriptCallback)` 生命周期路径。
- 偏离：无代码改动。

## 2026-10-03 — 像素完美相机的高度是 View 设备像素

`resolveCameraViewFraming` 的高度是该 View 的设备像素（`outputRect.extent` = 逻辑点 × `pixelDensity`）。2x 表面上整数 zoom 翻倍，一个 texel 占 `zoom` 个设备像素。脚本的 `viewSize` / `viewAspect` 仍是世界半宽高和宽高比，不改成像素。

## 2026-10-03 — P0 契约冻结（评审步骤 3，只改文档）

- 精灵之间的前后关系改为只用画家顺序：精灵对 3D 不透明深度只测不写，不透明与半透明走同一条混合管线。
  依据是主流 2D 引擎（Unity 2D、Godot 4、Bevy）都是画家顺序，只有 Unreal Paper2D 靠深度；深度方案需要
  `z = 基准 − y × ε` 约定，抗锯齿边缘的美术也会出问题。用户在三个方案里选了画家顺序。
- 排序键 `(layer, ySortRank, yKey, order, tiebreak)`，y-sort 逐对象开关（`Sprite2DComponent` 与 tilemap 子层各一个，
  tilemap 默认关）。比较函数只有一个，提取期排序与 2D 拾取共用。冻结在 `P0-contract-matrix.md` 的 C1–C5。
- 保留：摆在矩阵开头的 owner 表作为 `pipeline_contract_audit` 的结论；`kTextureTableSize = 16` 与分批规则（不为合批打乱顺序）。
- 偏离 / 未完成：纯 2D View 的 graph 策略用户选择先不定，等 `render-view-family` P3；混合场景的像素验收没有证据，
  本步骤不写代码，已整理成步骤 5 的验收项。`2d-gameplay` skill 里的 z 约定仍然有效，到步骤 5 才删。
