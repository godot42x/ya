# GUI 框架长处吸收表

> 建立日期：2026-08-15  
> 作用：把跨框架调研转化为 YA GUI 的明确设计输入。这里记录“吸收什么、为什么、何时做、什么不做”，避免后续只凭框架名类比。

## 0. 总原则

不复制框架外形，只吸收已经被多个系统验证的局部机制：

```text
先匹配问题
→ 再匹配 cost model
→ 只实现满足当前消费者的最小机制
→ 用 correctness test 与 profile 决定是否继续升级
```

任何候选能力必须回答：

1. 当前 YA 的真实问题是什么？
2. 它解决正确性还是性能？
3. 最小 owner 在哪里？
4. 会不会破坏 immutable snapshot、tree lifecycle 或 frame boundary？
5. 有什么可执行验收？
6. 什么数据出现后才升级？

## 1. WPF：Property Impact Metadata

### 长处

WPF 的 property metadata 可以声明属性变化影响 Measure、Arrange 或 Render。属性 effective value 改变后，由 property contract 自动触发相应 invalidation，而不是让每个调用者记住该调用哪个 dirty API。

### YA 吸收

建立最小 property impact：

```cpp
enum class EUIPropertyImpact : uint8_t
{
    None,
    Paint,
    Layout,
    SubtreePaintContext,
};
```

它服务两条写路径：

- runtime changed-only setter；
- reflection/document mutation transaction。

同一个属性只能有一个稳定 impact contract；调用方不能自行降级 dirty level。

### 不照搬

- 不建设完整 Dependency Property precedence；
- 不在本阶段加入 animation/style inheritance/coercion 大系统；
- 不把所有字段立即包装为通用 variant property。

### 阶段

Phase 1A/1B。

## 2. Flutter：Property-aware Invalidation 与边界成本意识

### 长处

- RenderObject property setter 根据影响区分 `markNeedsPaint` 与 `markNeedsLayout`；
- layout 与 paint pipeline 独立；
- RepaintBoundary 用独立 layer 隔离 repaint，并提供判断 boundary 是否值得的诊断思路；
- parentData 由 parent 持有 child-specific layout 数据，与 YA 的 parent-owned `UISlot` 方向一致。

### YA 吸收

- setter/property metadata 决定 Paint/Layout；
- 保持 `UILayout` / `UISlot` 与 paint cache 职责分离；
- boundary 必须有 profile 证据和收益计数；
- 增加 boundary candidate 的“parent/child repaint 频率”评估，而不是靠经验插 flag。

### 不照搬

- Phase 4 不创建 composited layer；
- 不把 CPU draw-item cache 伪装成 Flutter RepaintBoundary；
- 暂不实现局部 relayout boundary。

### 阶段

Phase 1、Phase 4 assessment。

## 3. Vue：动态 Dependency Edge 与批处理

### 长处

- getter/effect 收集真实读取依赖；
- 条件读取变化后重新收集 dependency；
- computed 缓存派生值；
- 同一 tick 更新可批量合并；
- reactivity debugging 能解释 track/trigger 来源。

### YA 吸收

- reactive edge 携带 widget/property-specific dirty level；
- 区分 paint-collected edge 与 persistent binding edge；
- paint-collected edge 在 self rebuild 前清除并重新收集；
- 增加 invalidation reason 与 dependent visit 计数；
- batching 只有 profile 证明重复 notify 成本后才做。

### 不照搬

- 不引入全局异步 microtask scheduler；
- 不允许默认延迟到下一帧；
- 不建设通用 JavaScript proxy 风格对象系统。

### 阶段

Phase 0、1A；batching 为 Phase 3 条件项。

## 4. Qt：Bindable Property、更新合并与 Dirty Region

### 长处

- bindable property 自动追踪表达式读取；
- 属性更新必须避免对外暴露中间不一致状态；
- `QWidget::update()` 合并多次请求；
- paint event 携带 dirty region，可以只重画受影响区域。

### YA 吸收

- mutation transaction 只在对象不变量完整后提交；
- binding getter 保持无副作用，写入点保持单一；
- 若未来实现 batching，合并的是 widget dirty transition，而不是重复执行 paint；
- dirty region 作为 item cache 和 subtree cache 都不足时的后续候选。

### 不照搬

- 当前不实现 QRegion 运算和区域合并；
- 不把区域重绘作为 Phase 1 正确性修复；
- 不引入 Qt 式 meta-object property runtime。

### 阶段

Phase 1B transaction；Phase 3 batching；区域级优化延后。

## 5. Slate / UMG：Typed Reason、Subtree Cache、Volatile 与 Insights

### 长处

- 明确区分 paint/layout/hierarchy 等 invalidation reason；
- Invalidation Box 缓存 subtree geometry/paint 信息；
- volatile widget 可在缓存 subtree 内保留高频动态区域；
- Slate Insights 可以看到 widget 的 invalidation/update reason。

### YA 吸收

- debug-only `EUIInvalidationReason`；
- dump last reason、transition count 和 widget name；
- Phase 4 若启动，以 CPU subtree cache ownership 为核心；
- `_bVolatile` 只作为明确的动态 island，不作为普通 binding 替代；
- hierarchy mutation 是独立 reason。

### 不照搬

- 不立即建设 global invalidation root；
- 不复制完整 Slate attribute descriptor 系统；
- 不因为已有 `_bVolatile` 就默认保留 presenter 每帧字段同步。

### 阶段

Phase 0 diagnostics；Phase 4 optional。

## 6. React：Pure Description、Stable Identity 与单向数据流

### 长处

- UI 由输入描述，而不是由 presenter 持续修补输出状态；
- state 与 tree position/type/key 绑定；
- stable key 支持列表 reorder 时保留 identity；
- render/commit 分离，描述阶段不直接执行外部副作用；
- lifting state up 减少多个 presentation fact source。

### YA 吸收

- model 是业务事实源，widget transient state 只保存交互必要状态；
- presenter 优先调用 changed-only property API，不维护重复 `_last*` 补偿状态；
- dynamic list 真正需要保留 identity 时才引入 keyed reconciliation；
- key/type/move/remove 契约先于 diff 实现；
- reconciliation 只更新 live tree，不侵入 draw-item cache。

### 不照搬

- 不建设通用 Virtual DOM；
- 不让所有 retained widget 每帧重新构造 description；
- 不引入 Fiber/concurrent scheduling。

### 阶段

Phase 1 Workbench migration；Phase 5 optional。

## 7. Godot：Cached Draw Commands 与 Explicit Redraw

### 长处

- drawing commands 可缓存；
- state 改变时显式 `queue_redraw()`；
- parent CanvasItem 的 transform/visibility 等会形成 inherited paint context；
- redraw 请求与实际 draw processing 解耦。

### YA 吸收

- 保持 draw-item segment cache；
- setter 是 redraw/invalidation 的唯一正常入口；
- inherited clip/transform/opacity 等变化必须纳入 subtree cache validity；
- 若做 batching，flush 必须发生在 snapshot 前。

### 不照搬

- 不建立第二套 CanvasItem tree；
- 不把 idle-time redraw 时序直接映射为 frame-end；
- 不允许命令录制读取 live widget。

### 阶段

Phase 0 cache validity、Phase 1 setter、Phase 2 inherited context。

## 8. Dear ImGui：最小状态同步与 Dynamic Island

### 长处

- 最小化用户侧 UI state duplication 和同步；
- 动态工具界面编写成本低；
- 每帧描述天然适合快速变化的 debug/tool UI；
- API 使用者不需要维护复杂 retained mutation 协议。

### YA 吸收

- Workbench/presenter 不保存可从 model 派生的第二份 presentation truth；
- `_bVolatile` 作为小范围 dynamic island；
- 未来可在 retained tree 上提供 immediate convenience layer：
  - 每帧产生轻量 description；
  - 内部按 key reconcile 到 retained widgets；
  - 仍走统一 snapshot/render pipeline。

### 不照搬

- 不把整个 runtime UI 改成 immediate mode；
- 不宣称 immediate mode 无内部状态；
- accessibility、国际化和产品 UI 不能因工具 API 简单而被忽略。

### 阶段

Phase 1 去重复状态；immediate convenience layer 不在当前主线。

## 9. 能力优先级

| 能力 | 来源 | YA 性质 | 优先级 |
|---|---|---|---|
| property impact metadata | WPF / Flutter | 正确性 | 必做 |
| property-aware dependency edge | Vue / Qt | 正确性 | 必做 |
| persistent vs paint-collected edge | Vue + 当前代码事实 | 正确性 | 必做 |
| invalidation reason trace | Slate | 可诊断性 | 必做 |
| build/inherited context cache validity | Godot / retained cache 共性 | 正确性 | 必做 |
| changed-only setter + mutation transaction | WPF / Qt / Flutter | 正确性 | 必做 |
| unified paint template | Slate/Flutter pipeline 思想 | 可推理性 | 必做 |
| batching/coalescing | Vue / Qt / Godot | 性能 | profile 驱动 |
| CPU subtree cache | Slate | 性能 | profile 驱动 |
| keyed list reconciliation | React | 维护性/性能 | 真实消费者驱动 |
| dirty region | Qt | 性能 | 延后 |
| immediate convenience layer | Dear ImGui | 开发体验 | 独立立项 |
| composited repaint layer | Flutter | 性能/GPU | 当前不做 |

## 10. 官方参考

- Flutter RenderObject / RepaintBoundary:
  - `https://api.flutter.dev/flutter/rendering/RenderObject-class.html`
  - `https://api.flutter.dev/flutter/rendering/RenderObject/markNeedsPaint.html`
  - `https://api.flutter.dev/flutter/widgets/RepaintBoundary-class.html`
- Slate invalidation / insights:
  - `https://dev.epicgames.com/documentation/en-us/unreal-engine/invalidation-in-slate-and-umg-for-unreal-engine`
  - `https://dev.epicgames.com/documentation/en-us/unreal-engine/slate-insights-in-unreal-engine`
- React state identity:
  - `https://react.dev/learn/preserving-and-resetting-state`
- Vue reactivity / batching:
  - `https://vuejs.org/guide/extras/reactivity-in-depth.html`
  - `https://vuejs.org/api/general.html#nexttick`
- Qt update/binding:
  - `https://doc.qt.io/qt-6/qwidget.html`
  - `https://doc.qt.io/qt-6/bindableproperties.html`
- Godot CanvasItem:
  - `https://docs.godotengine.org/en/stable/classes/class_canvasitem.html`
- Dear ImGui paradigm:
  - `https://github.com/ocornut/imgui/wiki/About-the-IMGUI-paradigm`
- WPF property metadata:
  - `https://learn.microsoft.com/en-us/dotnet/desktop/wpf/properties/framework-property-metadata`
