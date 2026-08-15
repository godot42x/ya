# GUI 失效传播机制：跨框架调研分析

> 建立日期：2026-08-15
> 配套：`plan.md`（吸收计划）；本文件是计划的**分析依据与推导过程**。
> 触发背景：M1 增量复用引入后，按钮 hover/press/focus 状态不刷新（瞬态状态改裸 bool 不标脏）、WorkbenchSurface presenter 每帧同步不标脏——暴露「失效传播缺口」。

## 0. 问题本质

引入增量复用后，「状态 → 像素」的传播链上，**任何一处「改了状态没标脏」都会导致视觉不一致**。这类 bug 不 crash、不报错，只是「不刷新」，极难调试。所以需要系统化解决，而非逐个补 markPaintDirty。

调研目的：**不独抄 UE**，跨框架蒸馏「脏传播 + 单向数据流」各家做法，找系统性方案。

## 1. 各框架失效传播机制分析

### 1.1 UE Slate

- **机制**：`TAttribute`（属性可缓存，依赖变才重求值）；`InvalidationPanel`（invalidation root，脏标记向上传播到它截止）；`volatile`（绕过缓存每帧重画）。
- **权衡**：缓存求值省重算，但依赖追踪复杂；volatile 是「放弃精确、保证正确」的兜底。
- **可吸收**：volatile 兜底（已落地 `_bVolatile`）+ CPU 侧 subtree geometry/paint cache 的边界思想。
- **与当前实现的差异**：现有 `invalidateSubtree()` 是向 descendants 批量标脏，不对应 Slate 的 descendant dirty 向 invalidation root 聚合；两者不能等同。

### 1.2 React

- **机制**：声明式 re-render（数据变 → 整棵组件子树重跑 render 函数）+ Virtual DOM diff（diff 出最小 DOM 变更）+ 稳定 key + Fiber（可中断调度）。
- **cost model**：**不维护 Vue 式 getter-level 精细依赖图，以 component update + reconciliation 为主**；可能多执行 render，再用 identity/key/type 与 diff 收敛 live tree 变更。
- **权衡**：心智模型简单（「数据变了就重渲染」），代价是 re-render 有浪费、需 diff 补救；依赖关系复杂且变化频繁时占优。
- **可吸收**：声明式描述 → diff → 最小 patch（对应 Phase 5 keyed reconciliation）；稳定 key（对应 Phase 5 的 identity 契约）。

### 1.3 Vue

- **机制**：getter 收集 reactive property → effect/component 依赖、setter 触发 notify；随后仍通过组件更新与 patch 机制产生最终 UI；computed 缓存；nextTick 批处理。
- **cost model**：**精确追踪（花追踪成本），少重渲染（省重渲染成本）**——与 React 正好相反。
- **权衡**：re-render 精确无浪费，代价是维护依赖图 + 依赖收集有 edge case（条件读取、动态依赖）；数据稀疏变化时占优。
- **可吸收**：批处理合并（对应 Phase 3，profile 驱动）。我们的 `Reactive` 就是 Vue 式的依赖追踪。

### 1.4 Flutter

- **机制**：三棵树（Widget 不可变配置 / Element 实例 / RenderObject 布局绘制）；property setter 根据影响调用 `markNeedsPaint` / `markNeedsLayout`；`RepaintBoundary` 形成独立 composited layer，隔离 repaint 范围。
- **权衡**：边界适合隔离高频局部与低频静态区域，但 layer 也有内存、合成和维护成本。
- **可吸收**：property-aware Paint/Layout invalidation，以及 dirty 聚合/边界划分思想。
- **不直接照搬**：本计划 Phase 4 是 CPU flattened draw-item subtree cache，不创建 Flutter 式 composited layer；其缓存所有权更接近 Slate Invalidation Box。

### 1.5 Qt

- **机制**：`QWidget::update(QRect)` 标记**像素区域**脏，`repaint` 只画脏区域（父窗口裁剪 + 区域合并）。
- **权衡**：区域级重绘比 widget 级更细，适合局部刷新；代价是区域管理复杂。
- **可吸收**：区域级粒度（我们已有 draw-item 段复用的雏形，未到区域级）。

### 1.6 ImGui

- **机制**：应用通常每帧重新提交 UI 描述，显著减少 retained presentation state 的同步负担；库内部仍维护窗口、输入、导航等交互状态。
- **权衡**：应用侧较少出现「状态改了但忘记让 retained presentation 更新」的问题，代价是每帧执行 UI 描述与布局/绘制生成。
- **可吸收**：对局部内容使用每帧重建作为一致性兜底（对应 `_bVolatile`）；它不是完整 immediate-mode 模型，也不意味着“零状态”或“绝对一致”。

### 1.7 Godot

- **机制**：`CanvasItem::queue_redraw()` 请求在后续 draw processing/idle 阶段重绘；重复请求可合并，未变化的 drawing commands 可复用。
- **权衡**：避免同帧多次无效重绘。
- **可吸收**：延迟调度与合并思想（与 Vue nextTick 类似），但 YA 必须在本次 `buildSnapshot()` 的 layout/paint 判断前 flush，不能机械翻译成“帧末”。

### 1.8 WPF

- **机制**：Dependency Property 通过 metadata 描述属性行为；`AffectsMeasure` / `AffectsArrange` / `AffectsRender` 在 effective value 改变时自动触发对应 invalidation；同一属性系统还承载 binding、inheritance、animation、coercion 与 change callback。
- **权衡**：属性影响从调用方手工判断上升为 property contract，正确性和工具能力强；代价是完整 property system 很重，metadata precedence/inheritance/coercion 也会显著增加复杂度。
- **可吸收**：为 YA runtime visual/layout property 建立最小 impact metadata（Paint / Layout / SubtreePaintContext），由 setter/reflection mutation 共用。
- **不直接照搬**：不建设 WPF 式完整 Dependency Property precedence、animation 和 inheritance 系统；当前只吸收“属性声明自身失效影响”的长处。

## 2. 横向对比与共性提炼

### 2.1 脏传播的两个正交维度

| 维度 | 取值 |
|---|---|
| 粒度（标多细） | 全树（ImGui）→ 子树（React/Vue 组件）→ 单 widget（Slate）→ 区域（Qt） |
| 边界（传多远） | 无专门边界 → dirty 聚合到 CPU cache root（Slate）或 composited layer boundary（Flutter） |

我们当前是「widget 粒度 + 无边界」的中间态。

### 2.2 追踪成本 vs 重渲染成本（React 与 Vue 的反向取舍）

- React：不维护 getter-level 精细依赖图，以 component update + reconciliation 收敛变更。
- Vue：维护 reactive property → effect/component 的细粒度依赖，再进入组件 patch。
- **本质：同一个「数据变如何最小更新」问题，不同的 cost model，没有唯一正确答案。** 适应场景不同。

### 2.3 三层解耦（契约 / 机制 / 策略）

React/Vue 共享「声明式契约」（数据是唯一事实源，声明式描述 UI），却在策略层分化——这个分化是**被动**的：浏览器布局/渲染引擎它们改不了，策略层是唯一能差异化处。

映射到本引擎（三层都是自己的，可主动决定解耦）：

| 层 | 解耦必要性 | 形式 |
|---|---|---|
| 契约 vs 机制 | 必须 | 已存在（Reactive 不知 layout，UILayout 不知数据来源） |
| 机制 vs 策略 | 暂不需要接口级 | 标志位组合（策略互补而非平行替换，抽接口是过度设计） |
| 描述 vs diff 策略 | 需要（Phase 5） | presenter 退回纯描述者，diff 负责最小更新 |

更完整的“各框架长处 → YA 决策 → 落地阶段”见 `framework-lessons.md`。

## 3. 从分析到计划的推导

### 3.1 调研给的是「工具箱」，不是「待办清单」

跨框架调研得到的批处理（Vue）、边界（Flutter）、声明式 diff（React）、稳定 key（React）——这些是**可选优化手段**，价值在于「某类性能问题出现时，知道有对应解法」，而非「调研了就都要做」。这是推导的第一前提。

### 3.2 正确性 vs 优化二分（推导主线）

核心二分：**漏标脏 = 视觉 bug（正确性问题）；多重建 = 性能损失（优化问题）。**

- 正确性问题（漏标脏）→「状态改了显示不更新」，用户可见的 bug，必须根治。
- 优化问题（多重建）→「白做了一点计算」，不影响正确性，只有成为瓶颈才值得动手。

这正是用户定调「一致性 > 性能」的推导：**正确性（Phase 0/1/2）无论如何都做；优化（Phase 3/4/5）由 profile 证明值得才做。**

### 3.3 为什么 Phase 0/1/2 是必做基础

- **Phase 0 基线/测试**：没有测量就无法判断「优化是否值得」；也是后续所有「测量驱动」判断的前置。
- **Phase 1 属性/绑定失效收口**：presenter 每帧写字段 + `_bVolatile` 兜底，是「正确性靠兜底硬撑」的状态。changed-only setter 与 property-aware binding 把 dirty reason 固定到属性消费点。
- **Phase 2 统一 paint/cache validity**：`UIContainer`/`UIScrollViewport`/`UISplitPane` 覆盖完整 `paint()`，且 cache 未显式覆盖 build context / inherited paint context。统一模板是正确性可推理、统计一致和后续 boundary 演进的前置；特殊 override 本身不必然已经构成视觉 bug。

### 3.4 为什么 Phase 3/4/5 是「优化」，profile 驱动

- **批处理（Phase 3）**：只有当「同帧重复 notify」成为 profile 热点才有意义；否则「延迟 flush」的复杂度纯属自找，还引入新的失效窗口风险（`dirty 必须在本帧判断前可见`这条不变式）。
- **边界（Phase 4）**：只有当「clean frame 的 traversal」是瓶颈才有意义；否则「skip subtree」的 cache ownership / dirty aggregation 复杂度不划算。
- **keyed reconciliation（Phase 5）**：只有当「动态列表 + 状态保留」出现真实消费者才有意义；静态列表用全量 rebuild 就够。

### 3.5 为什么「机制 vs 策略用标志位」但「边界不是单纯标志位」

「机制 vs 策略」用标志位组合（`_bVolatile` / dirty 粒度）表达是诚实的——策略是正交组合而非平行替换。但 **boundary 是例外**：它不能由单纯 `_bInvBoundary` 标志位完成，因为「跳过 subtree traversal」必须同时定义 cache ownership、dirty aggregation、traversal skip 三件事，缺一不可。所以 Phase 4 明确「不直接实现 `_bInvBoundary`」，而是等 profile 证明需要时整体设计。

## 4. 决策记录

1. **暂不把 Reactive/失效传播抽公共库**：`ReactiveBase` 存 `UIElement*`（dependents 类型写死），泛化需抽象 dependents 类型；等第二个真实消费者（渲染资源失效/场景图）出现再提炼共性，避免过早抽象。
2. **一致性 > 性能**：正确性（失效链完整）必做，优化（批处理/边界/diff）profile 驱动；任何机制不得以牺牲一致性为代价。
3. **机制 vs 策略用标志位而非接口**：接口抽象只在策略平行可替换时才值得做；当前策略是正交标志位组合，用字段表达更诚实。
4. **Paint 与 Layout 是不同传播语义**：不能由一个通用 boundary 同时截断（新计划 §3.4）；局部 layout 不在本计划内。
5. **`bindCache/cacheItems` 不是 Virtual DOM diff 接口**：reconciler 更新 live tree，draw-item cache 只决定重建/复用，二者职责分离。
6. **stable key 先于 keyed diff**：key 是 reconciliation 的前置契约，不是后续优化。

## 5. 基于当前实现补充出的关键约束

### 5.1 Dirty level 属于 dependency/property edge

当前 `ReactiveBase` 只保存一份全局 `EDirtyLevel`，但同一 reactive 可能同时被不同属性消费：

```text
同一个 style reactive
  → 固定尺寸文本读取 color：Paint
  → AutoSize 文本读取 font size：Layout
  → 容器读取 padding：Layout
```

因此 dirty level 不能长期只挂在 reactive value 上。正确模型是：

```text
Reactive value
  → dependent widget/property edge
  → edge-specific Paint / Layout reason
```

尤其 `UIText::paintSelf()` 读取 `resolvedText()/resolvedStyle()`，而
`computeDesiredSize()` 当前仍读取裸 `_text/_fontSize`。AutoSize + binding 会出现
paint 使用新值、layout 仍使用旧值的正确性缺口，必须在 Phase 0/1 覆盖。

依赖生命周期也必须区分：

- **paint-collected edge**：由一次 `paintSelf()` 中的读取产生；下次重建 self segment 前可清除并重新收集；
- **persistent binding edge**：布局、slot 或其他非 paint consumer 在 bind 时登记；不能被 `clearDependencies()` 随 paint 重建清掉。

当前 `UISplitPane::bindSplitRatio()` 在 bind 时手工登记 Layout dependent，但
`UIElement::clearDependencies()` 会清除 widget 保存的全部 dependencies。它目前只是因为
`UISplitPane` 覆盖完整 `paint()`、未执行基类 clear 流程而偶然存活；统一 paint pipeline
后若不先拆分依赖生命周期，split-ratio binding 会失效。Phase 1 必须先建立该契约，
Phase 2 才能安全收口 paint override。

### 5.2 Cache validity 不只由 widget dirty 决定

当前 draw-item segment 已经包含解析后的 target-pixel position、size、clip 与
textScale，因此还依赖：

- `UIFrameBuildContext::uiScale`；
- `UIFrameBuildContext::offset`；
- texture resolver/resource generation；
- ancestor clip 等 inherited paint context。

仅用 `UIElement*` 作为 cache key 不足以表达这些输入。第一阶段至少应在 build
context generation 改变时清空 cache；ancestor clip/context 改变时应使受影响 subtree
失效。未来若需要更细粒度，再考虑 context fingerprint 或缓存 logical draw items。

### 5.3 Public field + setter 只能是迁移态

只增加 setter 但长期保留公开 runtime write path，无法从架构上杜绝漏标脏。实施可以分步：

1. 先迁移现有 runtime mutation；
2. 再将 runtime mutable visual/layout property 收口到 setter/property mutation API；
3. document/reflection authoring 通过显式 transaction 在末尾统一失效。

### 5.4 CPU subtree cache 的主要参考

Phase 4 的目标是 CPU flattened draw-item subtree cache：

- 主要参考 Slate Invalidation Box 的 subtree cache ownership；
- 只借鉴 Flutter RepaintBoundary 的 dirty 聚合与边界划分思想；
- 不创建 composited layer，也不做 GPU render-to-texture。
