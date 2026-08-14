# Phase A Directory Charter — 面向未来拆仓的物理目录树

> 更新时间：2026-08-14
> 作用：把 GUI / game / editor 的物理目录目标写死。这里定义的是未来真实目录树，不再把 Foundation / Framework / Product 当成目标结构；它们只代表当前历史现状。

补充约束：目录规划必须同时回答两件事——共享能力放哪里、应用形态怎么组合；不能再让 `Game` / `Editor` 同时承担这两层语义。

## 1. 这次为什么要改口径

上一版 charter 解决了“谁归谁”的语义问题，但目标树仍然是：

    Foundation -> Framework/App -> Framework/GUI|Game -> Product|Example

这对职责讨论有帮助，但对物理目录并不够好，原因有三个：

1. 它仍然在用抽象分层词，而不是未来真正要落地的树；
2. Product 会继续变成垃圾桶层，把本该归到 GUI / Game / Editor 分支内的壳代码继续混在一起；
3. 如果未来只想抽走 GUI app 栈，这套名字并不能直接告诉人“该带走哪几棵树”。

所以本 charter 现在明确区分两件事：

- Foundation / Framework / Product：只是当前 repo 的历史现状；
- capability-first roots + app-form roots：才是未来物理目录目标。

还要补一层语义区分：

- 共享能力轴：`Core`、`App`、`Render`、`GUI`、`Scene`、`Physics`、`Scripting`、`Reflection`；
- 应用形态轴：`GameRuntime`、`GameEditor`、`GuiWorkbench`、`DccEditor`、`ModelViewer`、`CLI`、`RenderServer`。

即便 Phase A 还不会一次性把所有目录都迁到这套终局命名，也必须先停止把共享能力继续堆进 `Game` / `Product` 语义桶里。

## 2. 目标物理目录树

目标不是“再多分几层”，而是让目录一眼就能读出：哪些是无窗口共享主链，哪些是 GUI/windowed 分支，哪些是共享能力，哪些是 app form：

    Core
    App
      -> Kernel
      -> Control
    Render
      -> RHI
      -> Runtime
    GUI
      -> Runtime
      -> Host
    Scene
    Physics
    Scripting
    Reflection
    Applications
      -> GameRuntime
      -> GameEditor
      -> GuiWorkbench
      -> DccEditor
      -> ModelViewer
      -> CLI
      -> RenderServer
    Example/*

解释：

- Core：最低共享底座；
- App：无窗口的共享应用主链，只保留 kernel 与 control plane；
- Render / GUI / Scene / Physics / Scripting / Reflection：共享能力分支；
- Applications：应用形态组合分支；
- Example：可执行样例、feature gallery、smoke/regression app。

这条树要直接支撑未来拆仓：

- GUI-only 形态应能大体带走：App/Kernel + App/Control + Render + GUI + Example/GUI*；
- CLI / DS / server / render-service 形态应能大体带走：App/Kernel（按需再加 App/Control 与相关共享能力）；
- windowed game/editor 形态应能大体带走：GUI + Render + Scene + Physics + Scripting + 具体 app form；
- headless game / DS 形态应能大体带走：App/Kernel + Render/Runtime + Scene + 具体 app form（按需再加 App/Control）；
- 若 editor 未来使用 retain UI，很自然是 app form 依赖 GUI + Render + Scene + 其它共享能力；但共享能力绝不反向依赖 app form。

## 3. 各物理分支 charter

### 3.1 Core

允许职责：

- 日志、反射、字符串、基础容器、线程、数学、VFS、平台无关系统服务；
- RHI 抽象与最底层图形/平台基础设施；
- 不带 app 语义的辅助工具。

禁止职责：

- 主循环、control-plane orchestration、窗口装配；
- widget/layout/event route；
- scene/world/editor/product 语义。

说明：

- Core 是最低共享底座，但不是“所有共享东西都塞这里”；
- 一旦一个能力已经具有应用生命周期、控制面或窗口主链语义，就应上移到 App。

### 3.2 App

允许职责：

- AppKernel 与唯一主循环；
- 无窗口应用可复用的 app-level services registry、module lifecycle、tick/exit policy；
- 共享 control plane：CLI、command dispatch、事件注入、frame stepping、截图、golden diff、scenario orchestration、remote/agent control；
- 不带 native window 语义的 event/control contracts。

禁止职责：

- retain UI widget/layout/compose 细节；
- native window、native window manager、window bootstrap、presenter；
- scene/render/gameplay/editor 专属语义；
- example 页面内容。

说明：

- 顶层只能有一个 App/；
- 但 App 不能是一整团；它只拆成：App/Kernel、App/Control；
- CLI、DS、server、render-service 之类 windowless 形态只停在 App/Kernel（按需依赖 App/Control）；
- GUI app、windowed game、runtime editor 再往上叠 GUI/Host，而不是在 App 再长出窗口层；
- 后续不再允许 GUI/App、Game/App、Editor/App 这种第二主循环错觉目录继续存在。

建议内部职责：

- App/Kernel：唯一 loop、module lifecycle、service registry、headless tick/exit policy；
- App/Control：共享控制协议、CLI/command surface、scenario/frame stepping、capture/diff、remote/agent control 抽象，不默认绑定 native window；
- App 内不再保留 Window 子树；窗口、present、native event source 统一归到 GUI/Host。

### 3.3 GUI

允许职责：

- retain UI 内核：widget tree、layout/slot、event route、snapshot、draw2d、compose、resource；
- GUI window host / presenter / headless host；
- native window、native window manager、window bootstrap、native event source；
- `INativeWindow` 这类窗口对象及其 lifecycle/identity 语义；
- 可被多个 GUI app 复用的 tooling 基座。

禁止职责：

- game scene/world/document/product 壳语义；
- editor-specific ImGui panel glue；
- 把完整 window 接口原样塞进 RHI 作为 present 入口；
- demo/example 页面本体。

说明：

- `WindowManager` 是合理的 GUI host owner 概念；真正要拆的是“完整窗口对象”与“RHI present bridge”混在一起的接口；
- future RHI 只依赖最小的 `PresentSurfaceSource` 一类桥接接口，而不是 title/size/raw handle/activation 这些窗口管理语义。

- GUI 是未来 GUI-only repo 的主干；
- window 属于 GUI，而不是 App：以后 game engine / editor 也走这套 GUI bootstrap；
- 这一支必须能在没有 Game / Editor 的情况下独立存在。

### 3.4 Game

允许职责：

- `GameRuntime` 这一 app form 自己的壳与组装逻辑；
- gameplay domain、game rule、game-only content/workflow glue；
- 只服务 game runtime、且明确不打算被其它 app form 复用的 UI / control 扩展。

禁止职责：

- editor-only shell；
- 把共享 app 能力重新复制一遍；
- 把共享 `Scene / Physics / Scripting / RenderRuntime / Reflection` 长期藏在这里；
- 让 GUI-only app 为了跑起来被迫依赖这一支。

说明：

- `Game` 若作为可见目录名存在，只表达 app form，不再表达共享能力归宿；
- 历史上落在 `Framework/Game` / `Product/Host` 的共享能力，后续需要逐步抽回能力轴，只把真正的 game runtime 壳留在这里。

### 3.5 Editor

允许职责：

- 编辑器壳、editor-only tools、资产/场景/editor workflow glue；
- ImGui editor 适配层，以及未来 editor 对 retain UI 的桥接层。

禁止职责：

- 成为 GUI framework 的上游依赖；
- 把共享 app kernel / control plane 再复制一套；
- 把共享 `Scene / Physics / Scripting / RenderRuntime / Reflection` 长期藏在这里；
- 承担 game runtime 主壳语义。

说明：

- `Editor` 若作为可见目录名存在，只表达 app form；
- 它可以依赖 GUI、Render、Scene、Physics、Scripting 等共享能力，也可以按需要依赖 game runtime 壳，但反向依赖绝不成立。

### 3.6 Example

允许职责：

- 可执行入口；
- feature gallery、benchmark、smoke/regression app；
- 将来验证 GUI-only / game-only / editor-adjacent 的最小闭环程序。

禁止职责：

- 被共享能力根或 app-form shell 反向依赖；
- 承载长期共享内核类型。

说明：

- GUIWorkbench、GUIFrameworkSmoke、未来真实 GUI app example 都留在 Example/*；
- framework 不再产出“自己带页面内容的产品程序目录”。

## 4. 当前目录到未来物理树的映射

| 当前目录 | 未来去向 | 说明 |
|---|---|---|
| Foundation/Core/*（除 Application） | Core/* | 底层共享设施归 Core |
| Foundation/RHI/* | Render/RHI/* | RHI 仍是最底层共享基础；但不再顺带拥有完整窗口对象语义 |
| Foundation/Core/Application/* | App/Kernel/* + App/Control/* | AppKernel、共享 control plane、shared command/capture/scenario 归 App 的无窗口主链 |
| Framework/AppRuntime/* | GUI/Host/* | 这块其实是 window/bootstrap/native event source，属于 GUI 宿主链，不属于共享 App |
| Framework/GUI/* | GUI/* | GUI 运行时与 host/tooling 全部收回 GUI 分支 |
| Framework/GUI/App/* | GUI/Host/* | 具体 GUI 宿主归 Host，而不是第二个 App 根 |
| Foundation/RHI/NativeWindow* | GUI/Host/Window/* + Render/RHI/Present/* | 把窗口对象与 present bridge 拆开；RHI 只依赖后者 |
| Framework/AppServices/* | 按真实职责回到 Render/Runtime、Scene、Physics、Scripting 或具体 app shell | 它不是共享 App 主链；也不再默认整体落进 Game 桶 |
| Product/Host/* | 具体 app form 内的 branch-local shell | 必须离开顶层 Product；确切 leaf 名由 target/include audit 决定 |
| Product/Editor/* | Applications/GameEditor/*（语义） | editor product 语义进入 app-form 分支；是否保留顶层 Editor 名字由后续迁移批次决定 |
| Example/GUIWorkbench/* | Example/GUIWorkbench/* | 位置正确，继续保留 |

关键变化只有两个：

1. Product 不再是目标顶层目录；
2. Foundation/Core/Application 与 Framework/AppRuntime 不再并成一团 App；前者归 App/Kernel + App/Control，后者归 GUI/Host。
3. `Game` / `Editor` 不再承担共享 Physics / Scripting / Scene / RenderRuntime 的默认归宿语义。

## 5. 命名禁用词与保留词

### 5.1 保留词

- Core：最低共享底座；
- App：无窗口应用主链；其下只保留 Kernel / Control；
- GUI：retain UI 与 GUI 宿主分支；
- Game / Editor：若保留为可见目录名，只表示 app form；
- Host：具体宿主对象或宿主子目录，例如 GUI/Host；
- Shell / Runtime：只允许作为具体 app form 内的叶子级壳语义候选，不得再冒充顶层共享层。

### 5.2 禁用词 / 禁止模式

- 顶层禁止继续以 Foundation / Framework / Product 作为未来目标目录；
- 禁止新增 GUI/App、Game/App、Editor/App 这种重复主循环语义目录；
- 禁止把 Host 用成兜底大桶，例如新的 Product/Host / Game/Host；
- 禁止把 Product 作为“暂时不知道放哪就先塞这里”的过渡层。

## 6. 未来拆仓/裁剪的硬约束

目录收口不是为了好看，而是为了可裁剪。Phase A 之后，默认以以下约束审视每次 move/rename：

1. GUI 不得依赖任何 app form；
2. App/Kernel 与 App/Control 不得依赖 GUI 或任何 app form；
3. window/bootstrap/native event source 属于 GUI/Host，而不是 App；
4. Render/RHI 只依赖最小 present bridge，不依赖完整 window manager / activation / title 语义；
5. Core 不得依赖 App / GUI / Render / Scene / Physics / Scripting / Reflection / app forms；
6. 共享能力若被多个 app form 消费，就不得继续藏在 `Game` / `Editor` 语义分支内；
7. Example 只向下依赖，不能变成共享层；
8. 如果一个 GUI app 未来抽仓时仍需要连带某个 app-form shell 才能编译，说明当前目录/target 设计失败。

`Editor` 若保留为可见目录名，也只表示 app form；它可以依赖共享能力，但共享能力不能反向依赖它。

## 7. 第一轮 no-behavior 迁移批次

### Batch 1 — 文档与闭包审计

- 产出“当前目录 -> 未来目录 -> target -> include 根”映射表；
- 识别哪些 target 已经形成真实边界，哪些只是历史目录名噪声；
- 决定 Product/Host 中哪些是共享能力、哪些才是 app-form shell，再给 shell 选首个落点候选。

### Batch 2 — App 子树拆分

- 把 Foundation/Core/Application/* 按职责拆到 App/Kernel/* 与 App/Control/*；
- 首轮允许 target 名保守不动，但物理目录和 include 根必须开始收口。

### Batch 3 — GUI 宿主归位

- 把 Framework/AppRuntime/* 与 Framework/GUI/App/* 一起收到 GUI/Host/*；
- Framework/GUI/App/* -> GUI/Host/*；
- 保留 GUIAppHost compatibility alias 一轮，但不再新增引用。

### Batch 4 — 取消顶层 Product 垃圾桶

- Product/Editor/* -> app-form editor shell（物理名是否保留 `Editor/*` 由批次决定）；
- Product/Host/* -> 先拆共享能力，再把剩余壳归到具体 app form 内的 branch-local shell；
- 首轮只做 move/include/target 闭包修正，不混入逻辑重写。

### Batch 5 — 误名层回收

- Framework/AppServices/* 单独按真实职责回收到 Render/Runtime、Scene、Physics、Scripting 或具体 app shell；
- 不与前四批混做，避免把“共享 App 主链收口”与“game runtime contract 回收”搅在一起。

## 8. 停止线

出现以下任一情况，就说明当前 move/rename 方案偏了：

1. 为了迁目录，不得不再造一层新的 App / Host / Framework facade；
2. GUI-only 形态仍然需要跨进某个 app-form shell 才能完成最小链接闭包；
3. Product 虽然被删掉了，但同样的垃圾桶语义又在别的顶层名字下复活；
4. 新目录树不能直接回答“只做 GUI app 时将来该带走哪几棵树”。
