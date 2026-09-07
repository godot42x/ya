# GUI Framework 架构加固计划

> 建立日期：2026-09-07  
> 输入：当前 `Framework/GUI`、`WidgetTree`、`UIFrameSnapshot`、`Render2DComposePass` 与 standalone/GameRuntime host 实现  
> 状态：待评审；只处理已由代码证据确认的边界与性能风险

配套执行工件：

- `todo.md`：可领取任务、依赖、验收和提交边界；
- `progress.md`：只记录已经发生的实现与验证事实；
- `feature_matrix.json`：机器可读的能力和 checkpoint 状态；
- `session_checklist.md`：每轮固定开工、实施和收尾步骤。

发生总体设计变化时更新本文件；普通实施进度只写入 `progress.md` 与
`feature_matrix.json`，避免把 `plan.md` 继续堆成流水账。

## 0. 当前结论

GUI Framework 的主链保持不变：

```text
product/host event
    -> WidgetTree dispatch / opt-in tick
    -> dirty layout + incremental paint
    -> immutable UIFrameSnapshot
    -> Render2DComposePass
    -> Render2D batch / present
```

以下能力已经成立，不再作为重构目标：

- `WidgetTree` 是 live tree 与 attach/detach/focus/capture/drag 生命周期 owner。
- `UIElement::wantsTick()` 是 opt-in，tree 只 tick 可见子树。
- 几何 authoring 归 parent-owned `UISlot`；widget 只读取最终 layout rect。
- command recording 只消费 `UIFrameSnapshot`，不访问 live tree。
- paint 已有 per-widget draw-item cache；layout 已有 dirty taxonomy 与基础 skip proof。
- ColorEdit 的 SV 渐变使用两层一维顶点色 quad，设计正确，不需要专用纹理或 shader。
- standalone GUI、GameRuntime UI 与 Editor 共用 widgets/compose 契约，但各自保留宿主和 presentation 编排。

本计划只推进三个真实缺口：

1. 相同 clip 下每个 draw item 都 push/pop，导致 Render2D batch 被反复 flush。
2. 基类具备 assigned-layout skip，但多个 layout host override 绕过该入口。
3. `IGuiTextureSource::requestLoad` 的完成回调没有在接口上冻结线程契约。

## 1. 架构边界

### 保持的 owner

| 能力 | Owner | 本计划中的约束 |
|---|---|---|
| tree membership / input route / tick | `WidgetTree` | 不新增第二套 scheduler 或 event bus |
| measure / arrange | `UILayout` + parent-owned `UISlot` | 不把 geometry 写回 widget authoring 字段 |
| paint intent / immutable packet | `UIFrameBuilder` / `UIFrameSnapshot` | snapshot 不加入 live widget 指针 |
| clip/scissor batching | `Render2DComposePass` / `Render2D` | 保持 painter order，不按材质重排 item |
| texture lookup policy | product/host `IGuiTextureSource` | Widgets 不访问 `AssetManager` |
| GPU resource lifetime | snapshot + compose/render owner | command recording 期间不解析 live asset 状态 |

### 明确不做

- 不重写 `WidgetTree` 生命周期。
- 不新增中心 GUI event bus、`EditorPanel` 或平行 tick 系统。
- 不把 ColorEdit 渐变改成离屏 RT、格子纹理或专用 pipeline。
- 不按行数拆 `UIElement`、`WidgetTree`、`GUIAppHost` 或 `Render2DComposePass`。
- 不在没有多窗口并行录制需求前实例化整个静态 `Render2D`。
- 不立即把 `Texture` 替换成新造的 opaque handle；snapshot 强引用当前承担明确的 submit 前保活职责。
- 不恢复 Description/Reconciler 作为静态页面主路径。
- 不把已完成的 invalidation、dock、style、DSL 工作重新包装成 checkpoint。

## 2. C0：测量与正确性基线

### 目标

在改变 compose/layout 行为前，证明三个缺口的真实成本与现有结果，避免以架构推测代替证据。

### 工作项

1. 为 Render2D compose 验证建立稳定观测：
   - 同一 clip 下连续 N 个 sprite 的 screen flush 数；
   - clip A -> clip B -> unclipped 的 scissor transition 数；
   - item 顺序和 snapshot digest 不变。
2. 为 layout host 建立 arrange 计数测试：
   - clean sibling subtree 在另一分支 layout dirty 后是否被重新 arrange；
   - Container/Button/Scroll/Split/Overlay/SizeBox/Dock 等 override 是否遵循同一 skip proof；
   - geometry 或 layout revision 改变时不得错误跳过。
3. 冻结当前 texture callback 事实：
   - GameRuntime `AssetManager` completion 当前经 game thread dispatch；
   - standalone host 当前同步完成；
   - 增加一个刻意从 foreign thread completion 的契约测试，作为 C3 的红灯用例。
4. 记录代表性界面基线：
   - clipped long list；
   - Editor Inspector；
   - Dock workspace；
   - ColorEdit popup。

### 验收

- 测试能稳定暴露 clip flush 放大和 layout override 重排范围。
- 现有 snapshot 像素/结构结果有基线。
- C1-C3 每一步都有改前/改后数字，不以“代码更优雅”为验收。

## 3. C1：按 clip run 合批 compose

### 问题

snapshot 已把 inherited clip 展平到每个 draw item。当前 replay 对每个 clipped item 都执行：

```text
pushClip -> emit item -> popClip
```

`popClip` 会 flush pending batch，因此同一个 ScrollViewport 内的连续 item 无法合批。

### 目标设计

snapshot schema 暂时不变。Compose replay 维护当前生效的 flattened clip：

```text
for item in painter order:
    if item.clip != activeClip:
        close previous clip
        open incoming clip
    emit item
close active clip at end
```

规则：

- 相邻且 clip 相同的 item 共用一次 scissor 状态。
- clipped/unclipped、clip A/clip B 切换时仍先 flush，保持 painter order。
- 不跨非相邻 run 重排 item。
- 不给 snapshot 引入 BeginClip/EndClip widget 树结构，也不恢复 command recording 对 live tree 的依赖。

### 验收

- 同一 clip 下 N 个连续 screen quad 收敛为一个 screen batch（资源/容量 overflow 除外）。
- nested clip 展平后的视觉结果、draw item 顺序、structural digest 不变。
- 覆盖 clipped text/sprite/line、空 clip、相邻不同 clip 和 unclipped transition。
- GPU/offscreen parity 门禁通过。

### 提交边界

一个 `[gui/compose]` checkpoint：compose 状态切换、对应测试和本计划进度一起提交。不得混入 Render2D singleton 或 snapshot payload 重构。

## 4. C2：统一 layout host 的 assigned-rect skip

### 问题

`UIElement::layoutAssigned()` 已有 `tryReuseAssignedLayout()`，但多个 specialized host override 后直接 `setLayoutRect + arrange`，局部 layout invalidation 仍可能穿透到 clean sibling subtree。

### 目标设计

统一“接收 parent rect -> 校验 revision/dirty proof -> 必要时 arrange”的模板语义。具体实现以最小接口为准：

- 优先让所有 host 复用基类统一入口；
- 若 specialized host 必须在 arrange 前同步内部 layout state，则提供单一、受保护的 assignment helper；
- 不在每个 override 复制一份 skip 判断；
- Popup/Dock 等会更新 parent-owned slot 的 host，必须先使相应 layout revision 失效，不能被错误 skip。

本 checkpoint 不直接实现完整 measure cache；先保证现有 dirty subtree proof 对所有 layout host 一致生效。只有基线仍显示 measure 成本显著，才另开 measure-cache checkpoint。

### 验收

- clean assigned rect + clean revision 会跳过 host arrange。
- child desired size、slot、visibility、structure、scroll/split ratio 改变时不会错误跳过。
- clean sibling subtree 的 arrange count 不随另一分支的局部 layout 修改线性增长。
- 现有 `WidgetLayoutTest`、`WidgetTreeTest`、`UIFrameSnapshotTest` 和 Editor scale baseline 通过。

### 提交边界

一个 `[gui/layout]` checkpoint：统一 assignment/skip 契约和测试。不得同时拆控件目录或改 DSL。

## 5. C3：冻结异步纹理完成线程契约

### 当前事实

- GameRuntime 的 `AssetManager` texture callback 当前回到 game thread。
- standalone host 的文件加载当前同步完成。
- 但 `IGuiTextureSource` 公共接口没有声明 completion 所在线程，`FGuiTextureCatalog::notify()` 最终触发同步 Reactive mutation。

### 决策

本阶段采用简单、明确的契约：

> `FGuiTextureReady` 必须在拥有该 `WidgetTree` 的 UI thread 执行。

原因：当前两个真实 source 已满足；为尚不存在的 worker-thread source 提前引入 mutex/跨线程队列会扩大 WidgetTree 的调度职责。

### 工作项

1. 在 `IGuiTextureSource` 接口和两个真实 adapter 上写明 completion thread。
2. tree/catalog 记录 owner thread，并在 debug 下对错误线程 completion fail loudly；不得静默丢失 dirty 通知。
3. foreign-thread 测试验证诊断，不访问 widget dependent 或 snapshot cache。
4. 若未来出现真实 worker-thread source，adapter 负责 dispatch 到 UI thread；只有两个以上 host 重复该逻辑时，才引入共享 UI task sink。

### 验收

- current GameRuntime 与 standalone source 行为不变。
- foreign-thread completion 有确定诊断，不形成数据竞争。
- resource-ready 只标脏绑定该 path 的 widget；不退回全树 generation invalidation。

### 提交边界

一个 `[gui/resource]` checkpoint：线程契约、诊断和测试。不得顺手改 AssetManager 调度系统。

## 6. C4：完成后三项决策门

C1-C3 完成并有 profile 后，才评估以下方向；默认不实施。

### 6.1 Widgets 与 RHI/Texture 边界

当前 snapshot 对 `Texture` 的强引用是 render-facing packet 的保活机制，不等于 widget 在调用 RHI。只有满足至少一项时才设计 opaque resource reference：

- `ya-gui-widgets` 有真实的无 render-resources 独立裁剪需求；
- 增加第二个非 RHI compose backend；
- public `Texture` 类型造成明确 DLL/编译闭包问题；
- 能证明新 handle 同时保留 snapshot 到 queue submit 的生命周期。

否则保持当前结构。

### 6.2 `UIFrameDrawItem` payload 密度

只有 snapshot item 数量和复制带宽成为可测热点时，才把 sprite/text/line/gradient payload 改为紧凑 tagged storage。ColorEdit 的少量四角颜色不是启动该重构的理由。

### 6.3 Render2D session 实例化

只有出现多窗口并行 recording、嵌套 session 或 Render2D 多实例需求时，才把静态 session/resource owner 实例化。当前 pass slot 已解决串行多 pass 资源隔离，不以“singleton 不够现代”为由重写。

## 7. 整线验收

功能门禁：

- 输入 route、focus/capture、popup/modal、drag/drop 行为不变。
- WidgetTree attach/detach/opt-in tick 生命周期不变。
- snapshot 仍是 command recording 的唯一 GUI 输入。
- ColorEdit、Dock、Tree/Table、Inspector 和 runtime Game UI 无视觉回归。

性能门禁：

- clipped list 的 flush 数按 clip run 而不是 item 数增长。
- 局部 layout 更新不会重新 arrange 无关 clean subtree。
- clean snapshot `rebuiltWidgets == 0` 的既有基线保持。

建议验证：

```bash
xmake b ya-gui-closure-test
xmake run ya-gui-closure-test -- --gtest_filter='WidgetTreeTest.*:WidgetLayoutTest.*:UIFrameSnapshotTest.*:ToolControlsTest.ColorEdit*'
xmake b ya-gui-host
xmake b ya-game-runtime
xmake b ya-game-editor
python3 Script/automation/gui/run_workbench_gpu_parity.py
```

每个 checkpoint 提交前必须检查共享工作区 diff；只提交该目标对应的代码、测试与本计划进度，不吸收当前已有的 Editor/Dock/ColorEdit/Workbench 未提交改动。
