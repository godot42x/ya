# App / GUI 边界迁移计划：owner、物理树、target 与过渡头收口

> 建立日期：2026-08-14
> 状态：激活。此计划从 `../gui-architecture-convergence/` 中拆出，专门承接还未完成的目录/target/owner 迁移工作；旧计划保留为 GUI 内核与对象模型收敛的历史基线。
> 范围：只做 no-behavior 的边界迁移设计与批次落地；不继续扩写 layout / route / workbench feature 计划。

## 0. 为什么单独拆计划

`gui-architecture-convergence` 已经同时承载了：

- GUI 主链路与对象模型定稿；
- Layout / Slot / route / workbench regression 的阶段记录；
- App / GUI / Product / Host 的目录与 owner 收口；
- 当前尚未开始的物理迁移批次。

继续把“剩余迁移工作”挂在旧计划下，会造成两类噪声：

1. 已完成的内核收敛与尚未开始的物理迁移混在一起，读者很难判断当前真正活跃的切片；
2. 迁移输入工件（目录审计、能力/应用形态映射、NativeWindow API triage）会继续埋在历史大计划里，不利于后续直接按批次执行。

因此，本计划的职责只有一个：把剩余的 App / GUI 边界迁移做成一条单独主线，并为真实 move/rename patch 提供唯一输入。

## 1. 目标与非目标

### 1.1 目标

1. 把共享能力轴与应用形态轴落实到物理目录、target 和 include root 上，而不是只停留在概念口径。
2. 收口唯一主链：`App/Kernel (+ App/Control)` -> 共享能力（Render / GUI / Scene / Physics / Scripting ...）-> app-form shell。
3. 完成第一轮 no-behavior 迁移设计，并按批次落地：文件移动、include 修正、target/name 收口、必要的 forward-header 过渡。
4. 让 `GUIWorkbench` 继续保持 GUI-only closure sentinel，不再被 `Product/Host` 或 `Game` 语义污染。
5. 拆清 `INativeWindow`、`IPresentSurfaceSource`、`NativeWindowManager` 与 app shell 之间的真实 owner 边界。

### 1.2 非目标

- 不在本计划里继续改 Widget / Layout / Slot / route 行为；
- 不在本计划里继续扩写 workbench feature gallery；
- 不把 `Product/Host` 拆分顺手变成一轮功能重写；
- 不在物理迁移批次里混入 RHI、GUI runtime 或 editor 交互逻辑修复。

## 2. 当前事实源

本计划以下列工件为唯一输入，后续 move/rename 设计必须直接引用它们：

- `owner-model.md`：唯一主循环、GUI window host、WidgetTree owner 链；
- `directory-charter.md`：共享能力轴 / 应用形态轴、允许职责与禁止职责；
- `capability-appform-mapping.md`：能力轴与 app-form 双视图映射；
- `nativewindow-api-triage.md`：`INativeWindow` / `IPresentSurfaceSource` / host-policy 三分表；
- `directory-target-include-audit.md`：当前目录 / target / include-root 闭包与 GUIWorkbench sentinel 基线。

如果后续出现新的边界判断，应该先回写这些输入工件，再开始实际迁移。

## 3. 迁移对象模型

本计划的迁移对象不是“功能模块”，而是四类边界对象：

1. **共享主链对象**：`AppKernel`、control plane、service registry；
2. **GUI bootstrap / host 对象**：`GUIAppHost`、`NativeWindowManager`、`INativeWindow`、SDL event source、present bridge；
3. **共享能力消费者**：`Framework/AppServices`、可能仍挂在 `Product/Host` 的 runtime/editor glue；
4. **app-form shell**：`GameRuntime`、`GameEditor`、`GuiWorkbench` 等最上层组合壳。

迁移时的硬约束：

- windowless path 必须能在 `App/Kernel`（按需加 `App/Control`）截停；
- window 语义只从 `GUI/Host` 开始，不回流到 `App`；
- `Game / Editor` 只能表示 app-form shell，不再继续承担共享能力默认归宿；
- `Product/Host` 只能被拆散或归位，不能在原语义上继续扩容。

## 4. 分阶段落地顺序

### Phase A1 — 设计批次，不改行为

输出：

- 第一轮 move/rename batch 表；
- forward-header 过渡策略；
- target rename 顺序；
- 每个 batch 的 build / closure 验证命令。

### Phase A2 — file-level consumer audit

先逐文件审计以下区域，而不是直接移动整目录：

- `Foundation/Core/Application/*`；
- `Framework/AppRuntime/*`；
- `Framework/GUI/App/*`；
- `Framework/AppServices/*`；
- `Product/Host/*`。

目标：确认哪些是共享主链，哪些是 GUI bootstrap，哪些只是 app-form shell 消费者。

### Phase A3 — Batch 1 no-behavior 迁移

第一批只处理最清晰、闭包最强的归位：

1. `Foundation/Core/Application/*` -> `App/Kernel` + `App/Control`；
2. `Framework/AppRuntime/*` + `Framework/GUI/App/*` -> `GUI/Host/*`；
3. 目标 target 名与 include root 同步收口；
4. 必要时保留最小 forward-header / compatibility alias，但必须有删除时机。

### Phase A4 — Batch 2 Product/Host 与 AppServices 拆分

前提：A2 审计完成。

这一批的目标不是“给 Product/Host 找个新名字”，而是先拆出：

- 共享能力消费者回到各自 owner；
- 仅剩 app-form shell 的部分，再归到对应 runtime/editor 分支。

### Phase A5 — 过渡清理

完成条件：

- 删除已无消费者的 compatibility 头；
- 删除只服务旧路径的 target alias；
- 再次验证 `GUIWorkbench` 闭包不被污染。

## 5. 验证方法

每个 batch 至少留下三类证据：

1. 结构证据：move 表 / owner 表 / include-root 对照表；
2. 构建证据：受影响 target 的单独构建通过；
3. 消费闭包证据：`GUIWorkbench` 仍只依赖 GUI-only 路径，不被 `Product/Host` / `Game` 回灌。

后续真实迁移时，验证命令以 XMake 为准，优先：

- `xmake show -t <target>`
- `xmake b <target>`
- 必要时 `python3 Script/ya.py cfg` 刷新导航闭包

## 6. 完成标准

1. 新开发者能直接从目录读出：共享主链、GUI host、共享能力、app-form shell 各自在哪；
2. `Product/Host` 不再作为活跃语义桶存在；
3. `Framework/AppRuntime` 与 `Framework/GUI/App` 不再并存为两条 host 语义；
4. `NativeWindow` 相关边界不再要求调用方默认看到完整 window + present 混合接口；
5. 至少完成一轮 no-behavior 迁移并验证与迁移前行为等价。
