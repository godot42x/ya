# Progress

## 2026-10-07 — 立项（方案问答闭环）

- 来源：`showcase-roadmap` S5 行（`a1037cff` 入库）；用户拍板记录与 SOTA 问答结论沉淀进 `plan.md` §1 / §2。
- 三问拍板：路线 = 单一注册表条目 + 双调用约定（SOTA 形态，非纯 any 通道、非反射层加脚本槽）；注册时序 = GameRuntime 集中一处注册清单；迁移 = 试点先行分批。
- 现场勘察确认：`reflects` 是仓库内代码非 submodule；`boxValue`/`unboxValue` 转换链已存在；`Class::functions` 为 public map 可运行时注入；13 注册点分布已核实。
- 时序约束：先于 G3.2；与 C3 串行（S5 先），`Entity:call` 迁移即 C3 的基线交接点。

## 2026-10-08 — S5.1 完成（未提交）

- `script::fn` 前向推导签名，内部仍落 `ScriptNativeFn`。`Entity&` / 指针是 C++ 对象接收者（从 `void* self` 绑定，不进 `argTypeIndices`），`entityMethod` 收进这一层。`ScriptSelfHandle` 是 owner 槽（0 或 1，两个以上 `nativeSignatureOk` 为假）。其余参数必须 `crossesToScripts`，且不能是非 const 左值引用。`ScriptValue` 可作返回值，不能作参数。
- `script::addMethod` 把签名登记为 `Function`（`try_emplace` 先到者胜）。同名后到者被丢弃，`ClassRegistry::duplicateFunctionHook` 回调，`ScriptBindings` 打 `YA_CORE_WARN("script method '{}::{}' keeps the first registration")`。`reflects` 不依赖 Core / Log / `ScriptValue`。调用体挂 `ScriptMethod.native`，`callMethod` 有跳板时直通，不走 any。
- 试点：`SceneScriptBindings.cpp` 的 get/has/add/remove。`addNativeMethod` 保留。backend 与其余 9 个注册点未动。
- 验证：`ScriptNativeMethod.*` 2 过；`*Script*` 98/98；全量 `ya-testing` 1716/1716（基线 1714 + 2）。`walk_one_cell.py` 走过一格 `(0.5, 0.5) -> (1.4375, 0.5)`。GreedySnake `--exit-after-frame=90` 退出码 0，日志无脚本错误。
- 偏离：无。保留：GameRuntime 集中注册清单留到后续 checkpoint，本步仍在 `ensureSceneScriptBindings` 模块启动时登记；负编译用例用测试里的 `static_assert(!nativeSignatureOk<...>)`，没有单独的失败翻译单元。未撞上 unity 缺 include。
## 2026-10-08 — 主 agent 审查（S5.1 入库前）

- 修复：`callMethod` native 分支原先传 `resolve(ref)` 忽略 `ownerPath`——经继承导出的 registry native（父类型注册、派生类型调用）会拿到未调整指针再被适配器 `static_cast`，非零偏移时 UB。现 registry native 与反射方法同样走 `toOwner`；旧 API native 保持原契约（body 自行下降）。S5.1 的 Entity 四点不受影响（ownerPath 空），S5.2 的 GUI 类型层级必须带上这个修正。
- 语义变化记录：`Class` 反射注册从 `insert_or_assign`（后到者胜）改为 `insertFunction`（先到者胜 + hook WARN）——这是全反射链的语义翻转，不止 native；全仓无重注册覆盖用法，hook 提供可观测性。
- S5.2 前置约束：`addMethod` 在目标类型没有反射 `Class` 时 WARN 并丢弃（不注册）。迁 LuaWidgetHandle 8 点前先核对 UIElement / timer / clicked 各类型都有反射类；缺的要么补反射注册，要么给 addMethod 加合成 Class 支持——届时定。
- 复验：`ScriptNativeMethod.*` 2 过、`*Script*` 98/98、全量 1716 exit 0、`walk_one_cell` 正常、GreedySnake 无 ERROR。
