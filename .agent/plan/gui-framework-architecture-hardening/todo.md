# GUI Framework 架构加固 TODO

> 更新时间：2026-09-07  
> 状态：`[ ]` 未开始，`[-]` 进行中，`[x]` 完成，`[~]` 条件延后，`[-x]` 停止。

## 当前切片

当前激活切片：无。下一项是 `GAH-002`。共享工作区仍有 Editor/Dock/ColorEdit/Workbench
未提交改动；后续 checkpoint 继续只提交本任务文件，不得批量吸收。

执行规则：

- 同时最多一个任务为 `[-]`。
- 一个任务对应一个用户可验收目标；测试和计划状态随代码同一提交。
- 先固定失败证据或基线，再修改契约。
- C1、C2、C3 不互相混提；C4 只有启动条件满足才展开。
- 若审计证明原问题不存在，将任务标为 `[-x]` 并在 `progress.md` 写明证据，不制造替代工作。

## C0 — 基线与问题定界

- [x] `GAH-001` Clip run 与 Render2D flush 基线
  - 依赖：无。
  - 工作：构造同 clip 连续 item、clip 切换和 unclipped transition；记录 flush/scissor 数及 painter order。
  - 验收：测试稳定证明当前 flush 是否随 item 数增长；现有 snapshot 结构/像素结果有对照。
  - 非目标：不在本任务实现合批。
  - 提交：`[test/gui] baseline clipped compose batches`。

- [ ] `GAH-002` Layout host skip 基线
  - 依赖：无。
  - 工作：为 Container/Button/CheckBox/Overlay/Scroll/Split/SizeBox/Popup/Dock 的 assigned layout 增加 arrange 观测；构造局部分支 dirty。
  - 验收：明确哪些 override 绕过 `tryReuseAssignedLayout()`，并锁定 clean sibling 的现状计数。
  - 非目标：不在本任务引入 measure cache。
  - 提交：`[test/gui] baseline layout host skip`。

- [ ] `GAH-003` Texture completion 线程事实测试
  - 依赖：无。
  - 工作：确认 GameRuntime completion 经 game thread、standalone source 同步完成；增加 foreign-thread completion 测试 seam。
  - 验收：当前两个真实 adapter 的线程事实被测试或明确代码证据覆盖；接口缺口可以稳定复现。
  - 非目标：不修改 AssetManager 调度系统。
  - 提交：`[test/gui] baseline texture completion thread`。

- [ ] `GAH-004` 代表性场景性能记录
  - 依赖：GAH-001、GAH-002。
  - 工作：记录 clipped long list、Inspector、Dock workspace、ColorEdit popup 的 draw items、flush、layout/paint/arrange 数据。
  - 验收：数字和运行命令写入 `progress.md`；不能只写主观结论。
  - 提交：随最后一个 C0 基线任务提交，不单独制造纯进度提交。

## C1 — Compose clip-run 合批

- [ ] `GAH-101` 相邻相同 clip 共享 scissor run
  - 依赖：GAH-001、GAH-004。
  - 工作：replay 维护 active flattened clip；只在 clipped/unclipped 或不同 clip 间切换。
  - 验收：同 clip N 个连续 quad 在无 overflow 时为一个 screen batch；顺序、clip 结果和 snapshot schema 不变。
  - 测试：sprite/text/line、A->B、A->none、空范围、nested clip 展平。
  - 提交：`[gui/compose] batch adjacent items by clip run`。

- [ ] `GAH-102` Clip 合批 GPU/offscreen 收口
  - 依赖：GAH-101。
  - 工作：运行 Workbench GPU/offscreen parity 和 clipped 场景。
  - 验收：zero-diff；flush 数相较 GAH-001 降低；证据写入 `progress.md`。
  - 提交：与 GAH-101 同一 checkpoint，不单独提交测试结果。

## C2 — Layout host skip 契约

- [ ] `GAH-201` 统一 assigned-layout skip 入口
  - 依赖：GAH-002、GAH-004。
  - 工作：让 specialized host 复用一个 assignment/skip 模板；删除各自绕过 skip 的重复路径。
  - 验收：clean rect + clean revision 跳过 arrange；局部 dirty 不重排无关 sibling subtree。
  - 非目标：不实现完整 measure cache，不移动控件目录。
  - 提交：`[gui/layout] unify assigned layout skip`。

- [ ] `GAH-202` Layout invalidation 反例门禁
  - 依赖：GAH-201。
  - 工作：覆盖 desired size、slot、visibility、structure、scroll offset、split ratio、popup content slot 和 Dock projection 变化。
  - 验收：所有真实 layout mutation 都不会被错误跳过；Editor scale baseline 不退化。
  - 提交：与 GAH-201 同一 checkpoint。

- [~] `GAH-203` Measure cache 评估
  - 启动条件：GAH-201 后的 profile 仍显示 measure 是显著热点，且能点名重复 measure consumer。
  - 未满足时：保持延后，不为“完整性”实现。

## C3 — Texture completion 线程契约

- [ ] `GAH-301` 冻结 UI-owner-thread completion
  - 依赖：GAH-003。
  - 工作：在 `IGuiTextureSource` 和 adapter 上声明 completion 线程；tree/catalog 建立 debug ownership 诊断。
  - 验收：foreign-thread completion fail loudly 且不触碰 dependent/cache；真实 adapter 行为不变。
  - 非目标：不新增通用 GUI task queue。
  - 提交：`[gui/resource] define texture completion thread contract`。

- [~] `GAH-302` 共享 UI task sink
  - 启动条件：至少两个真实 source 都需要把 worker completion dispatch 回 UI owner thread。
  - 未满足时：由各 adapter 使用其产品线程调度设施。

## C4 — 条件决策门

- [~] `GAH-401` Widgets/Texture opaque reference 评估
  - 启动条件：出现无 render-resources 裁剪、第二 compose backend、明确 DLL/编译闭包问题之一。
  - 必须证明新引用仍可保活 GPU 资源到 submit 完成。

- [~] `GAH-402` `UIFrameDrawItem` 紧凑 payload 评估
  - 启动条件：snapshot item 数或复制带宽成为测量热点。

- [~] `GAH-403` Render2D session 实例化评估
  - 启动条件：真实多窗口并行 recording、嵌套 session 或多个 Render2D owner。

## 整线完成条件

- GAH-101、GAH-201、GAH-301 的目标均完成，或被证据明确停止。
- 条件项未启动不阻塞主线完成，但必须保留启动条件和状态。
- `progress.md` 有构建、测试、场景及性能对比证据。
- `feature_matrix.json` 与 TODO 状态一致且可解析。
- 没有引入新的平行生命周期、中心 bus、无消费者抽象或按行数拆文件。
