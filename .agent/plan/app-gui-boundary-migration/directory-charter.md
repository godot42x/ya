# Directory Charter — 可复用框架 / 应用形态 两层

> 更新时间：2026-08-15
> 方向修正（第 3 次）：确认 Scene / Physics / ECS 同样不是「游戏专属」，而是引擎通用能力。据此，`GameEngine` 层不再有实质内容，取消之；最终为两层。
> 历史：v1 capability-first 平铺 → v2 Framework/GameEngine/Applications 三层 → v3（当前）Framework/Applications 两层。

## 1. 分层原则

物理目录只分两层，一句话回答裁剪问题：

- `Framework`：引擎无关的可复用能力。任何 app form（GUI app、CLI、3D 工具、游戏、编辑器）都能复用；不含任何游戏领域语义。
- `Applications`：应用形态组合壳。可执行入口，只组合、不承载引擎能力。

关键结论：这个仓库里不存在「游戏专属能力层」。Scene / Physics / ECS / Render / Resource 都是通用能力（3D 工具、可视化、仿真同样消费）；真正游戏专属的是具体游戏内容（`Example/`）与 game-runtime 组合壳（`Applications/GameRuntime`），而不是某棵能力树。

单向依赖硬约束：

- `Framework` 不得反向依赖 `Applications`；
- `Applications` 是最上层壳，只向下依赖；
- GUI-only / 3D 工具形态只需 `Framework` 即可最小链接闭包。

## 2. 目标物理目录树

```text
Framework/           可复用能力层（引擎无关）
  Core/              底座：日志/反射/序列化/容器/数学/线程/平台/VFS + 能力注册面(ScriptApiRegistry)
  RHI/               底层图形抽象：Vulkan/OpenGL 后端（独立于 Render，平级）
  App/               无窗口主链：Kernel + Control + Module
  GUI/               retain UI + 窗口宿主：Runtime + Host + Tooling
  Hierarchy/         renderer-independent 场景树基类(Node)，被 GUI/Scene3D/Editor 共享
  Render/            3D 渲染运行时：RenderGraph / Render3D / Adapters（依赖 RHI）
  Resource/          资源加载：Mesh / Texture / Material / Asset
  Scene/             场景图与场景生命周期：Core / Runtime / Serialization / Scene3D
  Physics/           物理模拟
  ECS/               实体组件系统：Core / Systems(含脚本运行时 Lua/JS) / Linkage / Components
Applications/        应用形态层（可执行壳）
  GameRuntime/
  GameEditor/
  GuiWorkbench/      （未来；现 Example/GUIWorkbench）
```

## 3. 各层 charter

### Framework（可复用能力层）

允许职责：

- Core：日志、反射、序列化、容器、数学、线程、平台、VFS、字符串；引擎能力注册面（ScriptApiRegistry，脚本 / 自动化 RPC / agent 的统一入口，纯 JSON 接口不依赖 ECS/Scene 实现）；
- RHI：图形 API 抽象、shader 契约、backend（Vulkan/OpenGL）；只接受最小 present bridge，不依赖完整窗口对象；
- App：AppKernel 唯一主循环、无窗口 module lifecycle、service registry、control plane；
- GUI：widget tree、layout/slot、event route、snapshot、draw2d、compose、resource、window host、native window manager、headless host；
- Hierarchy：renderer-independent 场景树基类 Node，被 GUI widgets / Scene3D / Editor 共享（只依赖 Core，不得依赖 GUI/Scene/ECS）；
- Render：RenderGraph、Render3D、ECS adapters（3D 渲染运行时，DCC/查看器也复用）；
- Resource：Mesh/Texture/Material/Asset 的加载、resolve、dirty queue；
- Scene：场景图、scene lifecycle、serialization、scene3D；
- Physics：物理模拟；
- ECS：实体组件系统核心、通用 systems（transform/animation/camera 等）、脚本运行时（LuaScriptingSystem / JSScriptingSystem，作为 ECS System 实现）、component linkage、通用组件。

禁止职责：

- app-form 壳；
- editor/product 专属 shell；
- 具体游戏内容（属于 Example / Applications）。

### Applications（应用形态层）

允许职责：

- 可执行入口、app 组合逻辑、game-runtime / editor shell。

禁止职责：

- 承载引擎能力；
- 被 Framework 反向依赖。

## 4. 当前 -> 目标映射（已落地，2026-08-15）

物理树已收口为两层。以下映射全部执行完毕（commit `76112f26` Batch 1、`5f27a9d7` Batch 2）：

| 迁移前目录 | 落地后目录 | 说明 |
|---|---|---|
| Foundation/Core（底座 + Reflection） | Framework/Core | Reflection 并入 Core |
| Foundation/Core/Scripting | Framework/Core/Scripting | 随 Core 移动，未拆 target |
| Foundation/RHI | Framework/RHI | 独立于 Render，平级 |
| App/{Kernel,Control,Module} | Framework/App/{Kernel,Control,Module} | 物理下沉一层 |
| GUI/Host + Framework/GUI/{Runtime,Tooling} | Framework/GUI/{Host,Runtime,Tooling} | Host 与 Runtime/Tooling 汇合 |
| Framework/Game/Render | Framework/Render | 通用 3D 渲染 |
| Framework/Game/Resource | Framework/Resource | 通用资源加载 |
| Framework/Game/Scene | Framework/Scene | 通用场景 |
| Framework/Game/Physics | Framework/Physics | 通用物理 |
| Framework/Game/Gameplay/ECS/Core | Framework/ECS/Core | ECS 核心 |
| Framework/Game/Gameplay/Systems | Framework/ECS/Systems | 通用系统 + 脚本运行时（Lua/JS） |
| Framework/Game/Gameplay/Linkage | Framework/ECS/Linkage | 组件联动 |
| Applications/{GameRuntime,GameEditor} | 保留 | |
| Framework/Hierarchy | 保留独立模块 | renderer-independent scene-tree base，被 GUI/Scene3D 共享，不归 Scene/Core |
| Example/GUIWorkbench | 保留 | 未来上提 Applications/GuiWorkbench |

## 4.1 脚本能力的归属结论（2026-08-15 评估）

原计划把 `ECS/Systems` 内脚本系统拆归独立 `Framework/Scripting` 模块。经依赖审计后**维持现状**，理由：

1. `LuaScriptingSystem` / `JSScriptingSystem` 继承 `ISystem`/`GameInstanceSystem`，本质就是 ECS System，放在 `ECS/Systems` 语义正确；
2. `LuaScriptingSystem.cpp` 依赖 `ECS/Systems/Components/{Camera,PointLight}Component.h` 与 `Scene3D/TransformComponent.h`——它是"把引擎组件绑定给 Lua 的应用层系统"，不是可独立成层的"脚本基础设施"；强行拆会让 `Scripting` 反向依赖 `ECS/Systems`，层次反而更乱；
3. `ScriptApiRegistry`（能力注册面）是纯 JSON 接口、不依赖 ECS/Scene 实现，留在 `Core/Scripting` 合理；其 ECS/Scene 注册实现 `registerCoreScriptApis` 已在 `Applications/GameRuntime/ScriptApiCore.cpp`（app-form 组装层），依赖方向正确；
4. 拆分成本（约 30 文件 + target 拆分 + 宏改动 + 消费者迁移）与回归风险（脚本 / 自动化 RPC 为核心链路）明显高于收益（仅让 `ya-ecs-systems` 去掉 sol2/quickjs/lua 三个依赖）。

若未来确需"ECS 不依赖脚本运行时"的裁剪能力，再按 `Scripting/{Core,Systems,Components}` 结构独立成 `ya-scripting` target（依赖 `ya-ecs-core` + `ya-ecs-systems` + `ya-scene-*`）。

## 5. 命名规则

保留词：

- Framework：引擎无关可复用能力层；
- Applications：应用形态层；
- Core/RHI/App/GUI/Hierarchy/Render/Resource/Scene/Physics/ECS：Framework 子模块。

禁用词 / 禁止模式：

- 顶层禁止 Foundation / Product / GameEngine（GameEngine 已取消）；
- 禁止出现 GUI/App、Game/App、Editor/App 这种「第二主循环」目录；
- 禁止用 Host / Shell / Runtime 冒充顶层共享层（只作叶子级语义）；
- 禁止任何 `Game` 命名的目录继续包住通用能力。

## 6. 停止线

1. 为了迁目录再造新的 facade 层 → 停；
2. GUI-only / 3D 工具形态仍需跨进某个游戏语义目录才能链接 → 停；
3. 两层收口后，「只做 GUI app / 3D 工具带走哪几棵树」仍答不清 → 停。
