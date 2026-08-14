# App / GUI 边界迁移 TODO

> 更新时间：2026-08-14
> 作用：只追踪还没完成的边界迁移主线；不再重复记录已在 `gui-architecture-convergence` 完成的 layout / route / workbench 收敛。

## 当前优先级

当前激活切片：`phase-a1 first move/rename batch design`

当前输入工件：

- `owner-model.md`
- `directory-charter.md`
- `capability-appform-mapping.md`
- `nativewindow-api-triage.md`
- `directory-target-include-audit.md`

## P0 — 输入工件就位

- [x] 迁入 owner / directory / capability / NativeWindow / target-include 审计工件
- [x] 把旧计划切回历史基线，不再继续承载活跃迁移待办

## P1 — Phase A1 move/rename 设计

- [ ] 写出第一轮 no-behavior move/rename batch 表
- [ ] 写出 forward-header / compatibility alias 过渡策略
- [ ] 写出 target rename 顺序与 build 断点
- [ ] 定义每个 batch 的完成条件与回退点

## P2 — Phase A2 file-level consumer audit

- [ ] 审计 `Foundation/Core/Application/*`：逐文件切到 `App/Kernel` 或 `App/Control`
- [ ] 审计 `Framework/AppRuntime/*`：确认 GUI bootstrap / native event / window manager 归位
- [ ] 审计 `Framework/GUI/App/*`：确认 GUI host 装配与 compatibility alias 边界
- [ ] 审计 `Framework/AppServices/*`：逐文件归回真实 owner，而不是继续挂在泛化 services 桶
- [ ] 审计 `Product/Host/*`：区分共享能力消费者与 app-form shell

## P3 — Phase A3 Batch 1 no-behavior 迁移

- [ ] 落地 `Foundation/Core/Application/*` -> `App/Kernel` + `App/Control`
- [ ] 落地 `Framework/AppRuntime/*` + `Framework/GUI/App/*` -> `GUI/Host/*`
- [ ] 修正 include 路径、公开头与 target 名称
- [ ] 仅在必要处保留 compatibility 头，并标出删除条件
- [ ] 构建验证 Batch 1 涉及 target

## P4 — Phase A4 Batch 2 Product/Host 与 AppServices 拆分

- [ ] 拆出 `Product/Host` 内的共享能力消费者
- [ ] 仅把剩余 app-form shell 归到具体 runtime/editor 分支
- [ ] 按真实 owner 拆分 `Framework/AppServices`
- [ ] 验证 `GUIWorkbench` 链接闭包不再受 `Product/Host` / `Game` 语义污染

## P5 — Phase A5 过渡清理

- [ ] 删除已无消费者的 compatibility 头
- [ ] 删除仅服务旧路径的 target alias
- [ ] 更新相关计划工件，关闭本迁移主线

## 当前下一刀

1. 先把第一轮 move/rename batch 设计写实；
2. 紧接着做 `Product/Host` 和 `Framework/AppServices` 的 file-level consumer audit；
3. 再开始 Batch 1 的真实 no-behavior 迁移。
