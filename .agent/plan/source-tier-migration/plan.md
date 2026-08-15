# Source 树两层化迁移计划：Framework / Applications

> 建立日期：2026-08-15
> 状态：方向已定（两层），尚未开始迁移。
> 前置事实源：`../app-gui-boundary-migration/directory-charter.md`（v3 两层 charter）。

## 1. 目标

把 `Engine/Source/` 从「新旧两套布局并存」收口为两层：

```text
Framework/    Core / RHI / App / GUI / Render / Scripting / Resource / Scene / Physics / ECS
Applications/ GameRuntime / GameEditor / GuiWorkbench(未来)
```

核心收益：裁剪问题变成一句话——一切引擎能力都在 `Framework`，游戏只是 `Applications` 下的一种形态（依赖 Framework 的通用能力）。

## 2. 已落地 vs 待迁移

已落地（`app-gui-boundary-migration` 主线成果，本轮只物理再分层，不重做 owner 判定）：

- `App/{Kernel,Control,Module}`
- `GUI/Host`
- `Applications/{GameRuntime,GameEditor}`

待迁移（仍滞留在 `Foundation` / `Framework` 旧历史层）：

- `Foundation/Core`、`Foundation/RHI`、`Foundation/Core/Scripting`
- `Framework/Game/{Scene,Physics,Render,Resource,Gameplay}`
- `Framework/GUI/{Runtime,Tooling}`
- `Framework/Hierarchy`

关键判断：`Framework/Game` 下没有任何「游戏专属」内容。Scene/Physics/Render/Resource/ECS/Systems 全是通用能力（动画/脚本/变换/相机也都是 3D 工具通用系统）；真正的游戏专属在 `Applications/GameRuntime`（组合壳）+ `Example/`（具体游戏内容）。

## 3. 迁移批次

### Batch 1 — Framework 容器建立 + 无窗口/UI/底座能力迁入

- `Foundation/Core` -> `Framework/Core`（含 Reflection）
- `Foundation/Core/Scripting` -> `Framework/Scripting`
- `Foundation/RHI` -> `Framework/RHI`
- `App/*` -> `Framework/App/*`
- `GUI/Host` + `Framework/GUI/{Runtime,Tooling}` -> `Framework/GUI/*`
- 收口 target 名与 include root；保留最小 compat 转发头（删除条件写注释）

### Batch 2 — 引擎能力（Render/Resource/Scene/Physics/ECS）迁入 Framework

- `Framework/Game/Render` -> `Framework/Render`
- `Framework/Game/Resource` -> `Framework/Resource`
- `Framework/Game/Scene` -> `Framework/Scene`
- `Framework/Game/Physics` -> `Framework/Physics`
- `Framework/Game/Gameplay/ECS + Systems + Linkage` -> `Framework/ECS`（Systems 内脚本系统归 Scripting，待审计）
- 拍板 `Framework/Hierarchy` 归宿
- 消费者 include 拼写迁移（`Game/Scene/*` -> `Scene/*` 等）

### Batch 3 — 清理与脚本同步

- 删除已无消费者的 compat 转发头与 compat target
- 同步 `Script/ya.py`、`Script/ya_bundle_tool.py`、`Script/ya_module_lint.py` 硬编码 target 名 / 路径
- 移除 `Engine/Source/xmake.lua` 对旧路径的 includes
- 全量构建 + GUI-only 闭包 + `ya-testing` 回归

## 4. 验证方法（沿用 app-gui-boundary-migration）

每个 batch 三类证据：

1. 结构证据：move 表 / include-root 对照；
2. 构建证据：受影响 target 单独 `xmake b` 通过；
3. 闭包证据：`GUIWorkbench` 仍只依赖 `Framework` 的 GUI 子集，不穿进 Scene/Physics/ECS。

命令：`xmake show -t <target>`、`xmake b <target>`、必要时 `python3 Script/ya.py cfg`。

## 5. 决策记录（2026-08-15）

| 决策点 | 结论 |
|---|---|
| 可复用层命名 | `Framework` |
| 游戏层命名 | `GameEngine`（已取消：确认无游戏专属能力层） |
| Render(3D) + Resource | `Framework` |
| RHI | `Framework/RHI`，独立于 Render |
| Scene / Physics / ECS | `Framework`（通用能力，非游戏专属） |

## 6. 完成标准

1. 新开发者能从目录直接读出：哪些是可复用引擎能力、哪些是应用形态；
2. `Framework` 整层不引用任何 `Game` 语义，可被 GUI/3D 工具独立裁剪；
3. GUI-only 闭包不穿进 Scene/Physics/ECS；
4. 旧 `Foundation` / `Framework/Game` / `Framework/GUI`（部分）目录名不再作为活跃语义存在。
