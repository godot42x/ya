# Phase A Capability / App-Form Mapping

> 更新时间：2026-08-14
> 作用：把“共享能力轴”和“应用形态轴”拆成两张可直接执行的映射表。后续任何目录 move/rename、target 收口、include root 调整，都要先回答这两张表，而不是只凭“它现在主要给 Game 用”。

## 1. 使用规则

这份映射表先回答两个问题：

1. 某个能力本身的 owner 是谁，它应该落在哪条共享能力线；
2. 某个 app form 只是组合哪些能力，它自己的 shell 应该有多薄。

硬约束：

- `Game / Editor` 只表示 app form，不再表示共享能力归宿；
- 共享能力先找 capability owner，再由 `GameRuntime / GameEditor / GuiWorkbench / CLI / RenderServer / DccEditor ...` 组合；
- 未来如果要拆出 GUI-only、CLI-only、DCC-only 仓，这张表必须仍然成立。

## 2. 共享能力轴映射

| 能力 | 正式 owner / 边界 | 当前证据 | 未来目录 / target 组 | 备注 |
|---|---|---|---|---|
| `Core` | 最低共享底座；不带 app/window/product 语义 | `ya-foundation-core` 当前公开整个 `Foundation/Core/**`，连 `Application/*` 也一起导出 | `Core/*`；后续从 `ya-foundation-core` 中剥离 `App/*` | 现状闭包过大，但 `Core` 本身不该承载 App 主链 |
| `App/Kernel` | 唯一主循环、event source、delegate tick、exit policy、service registry | `Foundation/Core/Application/AppKernel.*` 已明确服务 GUI host 与 Product Host | `App/Kernel/*`；后续宜有独立 `ya-app-kernel` 或等价闭包 | 无窗口；CLI / DS / server 可只停在这里 |
| `App/Control` | CLI、command surface、scenario、frame stepping、capture/diff、remote/agent control | `AutomationRun.*`、`AutomationControlServer.*`、`GuiEventDriver.*`、`BmpDiff.*` 目前都在 `Foundation/Core/Application` | `App/Control/*`；后续视闭包再决定是否独立 target | `automation` 只是 control plane 子能力，不再单独平行长一套 |
| `Render/RHI` | 图形 API 抽象、shader-facing contract、backend 接口；只接受最小 present bridge | `ya-rhi` 当前公开 `Render.h`、`Shader.h`、`NativeWindow.h` | `Render/RHI/*` | `NativeWindow.h` 暂时污染了 RHI 公开面；后续需拆 present bridge |
| `Render/Runtime` | 3D runtime、frame graph/pipeline/runtime render services | 现状散落在 `ya-render-3d`、`ya-render-ecs-adapters`、`Framework/AppServices`、`Product/Host` | `Render/Runtime/*` | 本轮未做全量审计，但方向上必须从 `Game / Product` 语义桶中抽出 |
| `GUI/Runtime` | retain UI：widget tree、layout/slot、route、snapshot、draw2d、compose、resource | `ya-gui-resources`、`ya-gui-draw2d`、`ya-gui-widgets`、`ya-gui-compose` 已形成清晰闭包 | `GUI/Runtime/*` | GUI-only 路线的核心可复用层 |
| `GUI/Host` | window/bootstrap/native event source/native window manager/presenter/headless host | `ya-app-runtime` + `ya-gui-app-host` 共同构成当前 GUI 窗口宿主链 | `GUI/Host/*` | `Framework/AppRuntime` 与 `Framework/GUI/App` 最终都要汇到这里 |
| `Scene` | world/scene/runtime scene lifecycle | `ya-scene-core`、`ya-scene-runtime` 目前主要经 `ya-host` 消费 | `Scene/*` | 共享能力，不该默认藏在 `Game` |
| `Physics` | physics runtime / tooling / data contracts | 当前未做 target 审计，但已明确不该以 `Game` 作为默认归宿 | `Physics/*` | 后续目录收口时与 `Scene` 同样按 capability owner 处理 |
| `Scripting` | script runtime、binding、tooling glue | 当前未做 target 审计，但已明确不该继续以 `Game` 作为目录语义代称 | `Scripting/*` | 共享能力 |
| `Reflection` | 反射元数据、代码生成桥、tooling 支撑 | 当前底座在 Core/插件链上，未来可能继续外部预处理化 | `Reflection/*` 或保留为 Core/tooling 的独立能力轴 | GUI app、game、editor 都可能消费 |

## 3. 应用形态轴映射

| App form | 当前事实 / 证据 | 应只组合哪些能力 | 不该再 owning 的东西 | 壳 target 结论 |
|---|---|---|---|---|
| `GuiWorkbench` | `Example/GUIWorkbench` 是独立 binary；只依赖 `ya-gui-app-host` + `ya-gui-tooling` | `App/Kernel`、`App/Control`（按需）、`Render/RHI`、`GUI/Runtime`、`GUI/Host`、tooling shell | Scene / Physics / Scripting / Product Host / Editor | 它是最干净的 GUI-only app-form 样本，应继续作为闭包基线 |
| `GameRuntime` | 当前主要经 `ya-host` / `ya-engine` 组合；`ya-host` 同时混着 render/scene/gui/gameplay/editor-adjacent 壳语义 | `App/Kernel`、`App/Control`、`Render/*`、`GUI/*`（若有 runtime UI）、`Scene`、`Physics`、`Scripting`、game-specific shell | 顶层 Product/Host 大桶；共享 scene/render/runtime contracts | 现阶段只把它视作 app form；具体 leaf 名要等 target/include 审计后拍板 |
| `GameEditor` | `ya-editor` 目前经 `ya-engine` 拉起巨大闭包，并叠加 ImGui/imguizmo | `App/Kernel`、`App/Control`、`Render/*`、`GUI/*`、`Scene`、`Physics`、`Scripting`、editor-specific shell | 共享 GUI/App/Scene/Physics/Scripting owner | 当前仍是厚壳；下一步只允许继续变薄 |
| `CLI` | 未来形态；用户已明确必须支持无窗口 app | `App/Kernel`；按需加 `App/Control` 与具体共享能力 | GUI/Host、window/bootstrap、present | 这是 “App 不默认带 window” 的关键验证体 |
| `RenderServer` | 未来形态；可能无本地窗口、对外输出流或远端结果 | `App/Kernel`、`App/Control`、`Render/*` | GUI/Host、Game/Editor 壳 | 用来约束 `Render` 与 `GUI` 不能互相捆绑 |
| `DccEditor / ModelViewer` | 未来形态；用户已明确不希望所有 3D 工具都被迫归入 `Game` | `App/Kernel`、`App/Control`、`Render/*`、`GUI/*`、按需 `Scene/Physics/Scripting` | Game-only 目录语义 | 这是 capability-first 目录树是否成立的试金石 |

## 4. 现阶段可直接执行的 owner 结论

### 4.1 可以先视为“共享能力”的东西

- `AppKernel`
- `AutomationRun / AutomationControlServer / GuiEventDriver / BmpDiff`
- `WidgetTree / Layout / Slot / Draw2D / Compose / GUI resources`
- `NativeWindowManager / GUIWindowHost / GUIHeadlessHost / GUIRenderSurface`
- `Scene / Physics / Scripting / Reflection`（即便当前物理目录未完全归位）

### 4.2 可以先视为“应用壳”的东西

- `Example/GUIWorkbench/*`
- `Product/Editor/*`
- `Product/Host/*` 的剩余 branch-local shell（前提是先把共享能力剥出来）

## 5. 对下一轮迁移设计的直接约束

1. `Foundation/Core/Application` 不能再作为一个混合树整体迁移；必须拆成 `App/Kernel` 与 `App/Control` 两段。
2. `Framework/AppRuntime` 不是泛用 App 层，而是 GUI window/bootstrap 路径；它要并回 `GUI/Host`。
3. `Framework/GUI/App` 不是第二个 App 根，而是 GUI host 侧的装配/宿主层；它也要并回 `GUI/Host`。
4. `Product/Host` 不能整体原样迁到 `Game`；必须先拆“共享能力 vs app-form shell”。
5. `GUIWorkbench` 继续作为 GUI-only 链接闭包基线；后续任何迁移都要保证它不被 `Product/Host` 或 `Game` 语义重新污染。
