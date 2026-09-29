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
- 未完成：按键注入自动化（等 S7 `input.inject_key`）。下一步 R1a。
