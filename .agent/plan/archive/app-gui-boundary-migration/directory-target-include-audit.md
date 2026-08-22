# Phase A Directory / Target / Include Audit

> 更新时间：2026-08-14
> 作用：把当前目录、target、公开 include root、实际导出闭包与未来 owner 边界放到一张表里。下一轮 no-behavior move/rename 设计必须以此为基线，而不是凭目录名猜边界。

## 1. 审计结论先说

当前 repo 的主要问题不是“没有 target”，而是：

1. 物理目录边界、target 边界、owner 边界没有对齐；
2. 某些 target 的自我注释已经在说“这是共享/窄边界”，但实际公开面仍然更宽；
3. `Product/Host`、`Foundation/Core/Application`、`Foundation/RHI/NativeWindow*` 这三处最容易继续制造错误心智模型。

因此第一轮迁移不该先改行为，而应该先做：

- 目录归位设计；
- 公开 include root 收口；
- target charter 与名字对齐；
- 必要 forward header/compat alias 计划。

## 2. 目录 -> target -> include root 审计表

| 当前目录 | 当前 target / include root | 当前实际公开面 | 未来 owner / 目录去向 | target 收口建议 | 风险 / 备注 |
|---|---|---|---|---|---|
| `Foundation/Core` | `ya-foundation-core` / `Engine/Source/Foundation/Core/include` | `headerfiles` 公开 `Foundation/Core/include/**.h`，同时把 `Foundation/Core/**.h` 整体打进目标；`files` 也是 `Foundation/Core/**.cpp` 全量收集 | `Core/*` | 先停止把 `Application/*` 当成 Core 同级逻辑；后续从该 target 中拆出 `App/*` | 这是当前最明显的“目录与 owner 不一致”；`Application/*` 实际上已经污染 Core 公开面 |
| `Foundation/Core/Application` | 仍属于 `ya-foundation-core`；没有单独 include root | `AppKernel.*`、`AutomationRun.*`、`AutomationControlServer.*`、`GuiEventDriver.*`、`BmpDiff.*` 全部跟 Core 一起导出 | `App/Kernel/*` + `App/Control/*` | 第一轮可以先做物理 move + 转发头；是否立刻拆成独立 target 取决于闭包与依赖裁剪收益 | 这是 App 无窗口主链的真实根；不能继续藏在 Core 里 |
| `Foundation/RHI` | `ya-rhi` / `Engine/Source/Foundation/RHI/include` | 公开 `Render.h`、`RenderDefines.h`、`Shader.h`，也公开了 `NativeWindow.h`；`files` 明确编译 `NativeWindow.cpp` | `Render/RHI/*`；但 `NativeWindow` 需拆到 `GUI/Host/Window` + `Render/RHI/Present` | 先把 `NativeWindow` 从 RHI 公开面做语义拆分，再决定 header 物理归位 | 当前 RHI 公开面混入完整窗口对象，是 Phase A 关键污染点 |
| `Framework/AppRuntime` | `ya-app-runtime` / `Engine/Source/Framework/AppRuntime/include` | 模块自述是“reusable native app kernel pieces”；实际内容只有 `Bootstrap/AppBootstrap.*` 与 `Window/NativeWindowManager.*` | `GUI/Host/*` | 第一轮优先物理并到 `GUI/Host`；target 名后续同步收口 | 目录名叫 AppRuntime，但内容是 window/bootstrap/native runtime 机制，不是无窗口 App/Kernel |
| `Framework/GUI/App` | `ya-gui-app-host` / `Engine/Source/Framework/GUI/App/include` | 模块自述已经很清楚：standalone native GUI app lifecycle；公开 `GUIAppHost/GUIWindowHost` 等宿主面 | `GUI/Host/*` | 与 `ya-app-runtime` 一起做 Host 侧收口；首轮保留 `GUIAppHost` compatibility alias | 这里不是第二个 App 根，而是 GUI host |
| `Framework/GUI/Runtime/*` | `ya-gui-resources` / `ya-gui-draw2d` / `ya-gui-widgets` / `ya-gui-compose` 等各自 include root | 运行时边界相对清楚，已能被 `GUIWorkbench` 独立消费 | `GUI/Runtime/*` | 保持现有 capability-first 闭包；本轮不做大改 | 这是当前最干净的一段共享能力线 |
| `Framework/AppServices` | `ya-app-services` / `Engine/Source/Framework/AppServices/include` | 模块自述说“narrow host-service contracts”；当前公开 `RuntimeServices`、`AppAutomation`、`PostProcessingState`、`ShadowSettings` | 按真实职责回 `Render/Runtime` 或具体 app-form shell | 先做 file-level consumer audit，再决定拆向；不应继续以 `AppServices` 作为长期根 | 名字像共享 App 主链，但内容已经偏 runtime/render contract |
| `Product/Host` | `ya-host` / `Engine/Source/Product/Host/include` | `files` 全量收集 `**.cpp`；依赖 `ya-app-runtime`、render3d、scene、GUI、imgui、imguizmo、vulkan backend 等大闭包 | 只保留 app-form shell；共享能力需先拆出 | 先做 file-level 分类：shared capability vs branch-local shell；不要整体 rename 到 `Game` | 当前最宽、最混杂的 target；后续若直接 move 会把历史噪声整包带走 |
| `Product/Editor` | `ya-editor` / `Engine/Source/Product/Editor/include` | 通过 `ya-engine` 拉巨大闭包，并叠加 imgui/imguizmo | `Applications/GameEditor/*`（语义） | 暂不动实现；先在计划里只把它定性为 app-form shell | 它明显不是共享能力 owner；只是当前 product/editor 壳 |
| `Example/GUIWorkbench` | `GUIWorkbench` binary / 无共享 include root，仅 Example 源集 | 只依赖 `ya-gui-app-host` + `ya-gui-tooling`，没有 Scene/ECS/Render3D/Product Host/Editor 依赖 | 继续留在 `Example/GUIWorkbench/*` | 保持不动；作为 GUI-only 闭包基线 | 这是验证迁移是否把 GUI 重新污染回 Product/Game 的最好哨兵 |

## 3. 当前 target 自述与实际边界的偏差

### 3.1 `ya-app-runtime`

`xmake.lua` 自述是“reusable native app kernel pieces”，但当前内容只有：

- `Bootstrap/AppBootstrap.*`
- `Window/NativeWindowManager.*`

这说明它的真实 owner 更接近 `GUI/Host`，不是未来的 `App/Kernel`。

### 3.2 `ya-gui-app-host`

它的自述已经和目标方向比较一致：standalone native GUI app lifecycle，且强调不依赖 Scene/ECS/Render3D/Product Host/Editor。

问题不在这个模块本身，而在它旁边还有一个名字叫 `AppRuntime` 的宿主块，造成“两个 App 根”的错觉。

### 3.3 `ya-rhi`

当前 `NativeWindow.h` 还在 `ya-rhi` 的公开 header 列表里。这不是一个命名小问题，而是明确的 owner 泄漏：RHI public surface 仍然依赖完整窗口对象语义。

### 3.4 `ya-host`

`ya-host` 当前更像“历史 app shell + 多条共享能力 glue”的混合体：

- scene lifecycle
- render adapters
- imgui backend
- vulkan backend direct usage
- GUI fonts / Game UI bridge
- automation / screenshot / task / network / utility

所以它不是简单 rename 就能变清楚的模块；必须先分类剥离共享能力。

## 4. 第一轮 no-behavior 迁移的建议批次

### Batch 1 — 先做物理树与头归位设计

1. `Foundation/Core/Application` -> `App/Kernel` + `App/Control`
2. `Framework/AppRuntime` + `Framework/GUI/App` -> 同一 `GUI/Host`
3. `NativeWindow` 拆成 window object / present bridge 两个 public 面

这一批只做：

- move/rename 设计；
- include root 与 forward header 设计；
- target charter 重新命名。

不做：

- 行为改写；
- presenter 重写；
- GUI/render 逻辑顺手重构。

### Batch 2 — 再做 `Product/Host` file-level consumer audit

目标不是立刻把 `Product/Host` 删掉，而是先把里面的东西分成：

- 明确共享能力；
- 明确 GUI / render / scene glue；
- 明确只属于 `GameRuntime` 或 `GameEditor` 的 branch-local shell。

在没有这张 file-level consumer audit 之前，不允许直接把它整体迁到 `Game/*`。

## 5. 这份审计对下一轮的直接要求

1. 新的 move/rename 计划必须同时写清：目录去向、target 名、public include root、compat forward header 是否保留。
2. 若某次迁移只改目录名、不改 target charter，就等于没有真正收口 owner 边界。
3. `GUIWorkbench` 必须作为回归哨兵：一旦迁移后它开始穿过 `Product/Host`、`Scene` 或 `Game` 才能运行，说明迁移方向错误。
4. `Product/Host` 的后续动作必须以 file-level consumer audit 为前置，不允许整体打包搬家。
