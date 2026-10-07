# Progress

## 2026-10-07 — 立项（方案问答闭环）

- 来源：`showcase-roadmap` S5 行（`a1037cff` 入库）；用户拍板记录与 SOTA 问答结论沉淀进 `plan.md` §1 / §2。
- 三问拍板：路线 = 单一注册表条目 + 双调用约定（SOTA 形态，非纯 any 通道、非反射层加脚本槽）；注册时序 = GameRuntime 集中一处注册清单；迁移 = 试点先行分批。
- 现场勘察确认：`reflects` 是仓库内代码非 submodule；`boxValue`/`unboxValue` 转换链已存在；`Class::functions` 为 public map 可运行时注入；13 注册点分布已核实。
- 时序约束：先于 G3.2；与 C3 串行（S5 先），`Entity:call` 迁移即 C3 的基线交接点。