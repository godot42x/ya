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

- [ ] `Framework/Game/Render` -> `Framework/Render`
- [ ] `Framework/Game/Resource` -> `Framework/Resource`
- [ ] `Framework/Game/Scene` -> `Framework/Scene`
- [ ] `Framework/Game/Physics` -> `Framework/Physics`
- [ ] `Framework/Game/Gameplay/ECS + Systems + Linkage` -> `Framework/ECS`
- [ ] Systems 内脚本系统（Lua/JSScriptingSystem）归 `Framework/Scripting`（先审计再拆）
- [ ] 拍板 `Framework/Hierarchy` 归宿
- [ ] 消费者 include 拼写迁移 + 构建验证

## P3 — Batch 3 清理与脚本同步

- [ ] 删除无消费者的 compat 转发头 / compat target
- [ ] 同步 `Script/*.py` 硬编码 target 名 / 路径
- [ ] 移除 `Engine/Source/xmake.lua` 旧路径 includes
- [ ] 全量构建 + GUI-only 闭包 + `ya-testing` 回归

## 待拍板（不阻塞主线，迁移前需定）

- [ ] `Framework/Hierarchy` 归宿：Framework/Scene 还是 Framework/Core
- [ ] ECS 内部结构：Systems（transform/animation/camera）与 Scripting 的边界审计
