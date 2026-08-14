# App / GUI 边界迁移 TODO

> 更新时间：2026-08-15
> 作用：只追踪还没完成的边界迁移主线；不再重复记录已在 `gui-architecture-convergence` 完成的 layout / route / workbench 收敛。

## 当前优先级

当前激活切片：`phase-a4 batch-2 Product/Host 与 AppServices 拆分`

当前输入工件：

- `owner-model.md`
- `directory-charter.md`
- `capability-appform-mapping.md`
- `nativewindow-api-triage.md`
- `directory-target-include-audit.md`
- `product-host-file-audit.md`
- `appservices-file-audit.md`

## P0 — 输入工件就位

- [x] 迁入 owner / directory / capability / NativeWindow / target-include 审计工件
- [x] 把旧计划切回历史基线，不再继续承载活跃迁移待办

## P1 — Phase A1 move/rename 设计

- [x] 写出第一轮 no-behavior move/rename batch 表
- [x] 写出 forward-header / compatibility alias 过渡策略
- [x] 写出 target rename 顺序与 build 断点
- [x] 定义每个 batch 的完成条件与回退点
- [x] 落盘 `first-batch-move-design.md`

## P2 — Phase A2 file-level consumer audit

- [x] 审计 `Foundation/Core/Application/*`：逐文件切到 `App/Kernel` 或 `App/Control`
- [x] 审计 `Framework/AppRuntime/*`：确认 GUI bootstrap / native event / window manager 归位
- [x] 审计 `Framework/GUI/App/*`：确认 GUI host 装配与 compatibility alias 边界
- [x] 审计 `Framework/AppServices/*`：逐文件归回真实 owner，而不是继续挂在泛化 services 桶
- [x] 审计 `Product/Host/*`：区分共享能力消费者与 app-form shell

## P3 — Phase A3 Batch 1 no-behavior 迁移

- [x] 落地 `Foundation/Core/Application/*` -> `App/Kernel` + `App/Control`
- [x] 落地 `Framework/AppRuntime/*` + `Framework/GUI/App/*` -> `GUI/Host/*`
- [x] 修正 include 路径、公开头与 target 名称
- [x] 仅在必要处保留 compatibility 头，并标出删除条件
- [x] 构建验证 Batch 1 涉及 target

## P4 — Phase A4 Batch 2 Product/Host 与 AppServices 拆分

- [x] 减法：删除 Product/Host 的 dead/compat 噪声（WindowsDialogWindow、NetDriver、Switcher、AppContext、AppEvent、Host/NativeWindowManager compat 头、broken Host/Config/ConfigManager mirror）
- [x] 按真实 owner 拆分 `Framework/AppServices`：ShadowSettings / PostProcessingState / AppAutomationShadowOverrides / RuntimeServices 迁到 `Render3D/Common`，删除 `ya-app-services` target，消费者拼写 `AppServices/*` -> `Render3D/Common/*`
- [ ] 拆出 `Product/Host` 内的共享能力消费者 façade（AppRenderServices / AppSceneServices / AppTaskManager 等，按 audit 跟随 app-form shell 归位）
- [ ] 仅把剩余 app-form shell 归到具体 runtime/editor 分支
- [x] 验证 `GUIWorkbench` 链接闭包不受 `Product/Host` / `Game` 语义污染（Batch 1 已验；本轮删除 `ya-app-services` 不影响 GUI 闭包）

## P5 — Phase A5 过渡清理

- [ ] 删除已无消费者的 compatibility 头（`Core/Application/*`、`AppRuntime/*`、`GUI/App/*` 转发头）
- [ ] 删除仅服务旧路径的 target alias（`ya-app-runtime` / `ya-gui-app-host` compat target）
- [ ] 更新相关计划工件，关闭本迁移主线

## 当前下一刀

1. 依据 `product-host-file-audit.md` 把 `Product/Host` 内剩余 consumer façade（AppRenderServices / AppSceneServices / AppTaskManager / InputRouter / GameUI / ImGui adapter 等）按 app-form shell 口径归位；
2. 仅把剩余 app-form shell 归到具体 runtime/editor 分支；
3. 执行 Phase A5 过渡清理：删除 `Core/Application/*`、`AppRuntime/*`、`GUI/App/*` 转发头与 `ya-app-runtime` / `ya-gui-app-host` compat target；
4. 持续验证 `GUIWorkbench` 闭包不被 `Product/Host` / `Game` 语义污染。
