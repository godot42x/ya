# Source 树两层化迁移 TODO

> 更新时间：2026-08-15
> 作用：只追踪「Foundation / Framework -> Framework / Applications」这条新主线。

## 当前优先级

当前激活切片：`Batch 1 — Framework 容器建立 + 无窗口/UI/底座能力迁入`

输入工件：

- `../app-gui-boundary-migration/directory-charter.md`（v3 两层 charter）
- `plan.md`（本计划）

## P0 — 方向就位

- [x] 可复用层命名 = Framework
- [x] Render(3D) + Resource 归 Framework、RHI 独立于 Render
- [x] Scene / Physics / ECS 归 Framework（通用能力）
- [x] 取消 GameEngine 层（确认无游戏专属能力层）
- [x] 更新 `directory-charter.md` 为两层 charter
- [x] 新建本计划目录

## P1 — Batch 1 Framework 容器 + 无窗口/UI/底座能力

- [x] 写 `first-batch-move-design.md`（目录级 move 表 + xmake/脚本改动清单 + checkpoint）
- [x] `Foundation/Core` -> `Framework/Core`（含 Reflection + Scripting，不拆 target）
- [x] `Foundation/RHI`（含 Backend） -> `Framework/RHI`（shader codegen 相对路径深度不变，无需改）
- [x] `App/{Kernel,Control,Module}` -> `Framework/App/*`
- [x] `GUI/Host` -> `Framework/GUI/Host`，`Framework/GUI/xmake.lua` 增加 Host include
- [x] 删死文件 `UnifiedReflection.deprecated.h`（无消费者）
- [x] 更新顶层 `xmake.lua` includes、`YA.xmake.lua` PCH 路径、`ya_module_lint.py` 物理路径表
- [x] 构建验证 C1–C4 + GUI-only 闭包（全绿）

## P2 — Batch 2 引擎能力迁入 Framework

- [x] `Framework/Game/Render` -> `Framework/Render`
- [x] `Framework/Game/Resource` -> `Framework/Resource`
- [x] `Framework/Game/Scene` -> `Framework/Scene`
- [x] `Framework/Game/Physics` -> `Framework/Physics`
- [x] `Framework/Game/Gameplay/ECS/Core + Systems + Linkage` -> `Framework/ECS/{Core,Systems,Linkage}`
- [x] 删空 `Framework/Game` 目录
- [x] 更新 `Engine/Source/xmake.lua` includes、`Script/ya_module_lint.py` MODULES 表
- [x] 构建验证：ya-engine 聚合 + GUIWorkbench 闭包 + module lint（全绿）
- [x] ECS 语义收口（Batch 2b）：`Gameplay/Systems/`+`Gameplay/Linkage/` -> `ECS/Systems/`+`ECS/Linkage/`（39 文件）、target `ya-gameplay-systems` -> `ya-ecs-systems`、宏 `YA_GAMEPLAY_SYSTEMS_API` -> `YA_ECS_SYSTEMS_API`（已提交见 memory）

## P3 — Batch 3 清理与脚本同步

- [x] 同步 `Script/*.py` 中其余硬编码 target 名 / 路径（确认零残留，仅 ya_module_lint.py 需改，已在 Batch 1/2 改）
- [x] 全量构建 + GUI-only 闭包 + `ya-testing` 回归（ya-engine / GUIWorkbench / GameRuntime / GameEditor / ya-testing 全绿）
- [x] 更新 `directory-charter.md` 中「当前→目标映射」为已落地状态

## 待拍板（不阻塞主线，后续批次）

- [x] `Framework/Hierarchy` 归宿：保持独立模块（GUI/Scene3D 共享，不归 Scene/Core，已落 charter）
- [x] 脚本系统归属：经依赖审计维持现状（脚本运行时是 ECS System 留在 ECS/Systems，ScriptApiRegistry 留在 Core；拆分收益 < 成本，已记录评估结论于 charter 4.1）
