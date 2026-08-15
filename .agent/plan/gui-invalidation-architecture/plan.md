# GUI 失效与增量更新架构

> 建立日期：2026-08-15  
> 最近评审：2026-08-15  
> 输入：现有 YA GUI 实现 + 跨框架调研（Flutter / React / Vue / Qt / ImGui / Godot / UE Slate，分析见 `audit.md`）  
> 状态：已评审；按测量驱动的阶段计划实施

配套执行工件：

- `framework-lessons.md`：各框架可吸收能力、拒绝照搬项与阶段映射；
- `todo.md`：带依赖、验收和提交边界的任务清单；
- `process.md`：每轮实施、验证、决策门与状态更新流程；
- `progress.md`：逐轮事实记录和下一接力点；
- `feature_matrix.json`：机器可读能力状态；
- `session_checklist.md`：固定开工/收尾步骤。

## 0. 目标

建立一条可靠、可测量的 GUI 更新链：

```text
数据或视觉属性变化
    → 在 snapshot 前产生正确的 Paint / Layout 失效
    → layout（仅在需要时）
    → paint（重建 dirty widget，复用 clean widget）
    → immutable UIFrameSnapshot
```

优化顺序遵循：

1. **先保证所有变化都进入失效链。**
2. **再消除无意义的每帧重建。**
3. **最后根据 profile 引入批处理、子树缓存或声明式 reconciliation。**

核心不变式：

- 数据/显示一致性优先于性能。
- dirty 必须在本帧 `layout()` / `buildSnapshot()` 判断前可见，不允许默认延迟一帧。
- Dirty level 属于 widget/property 对 reactive value 的消费关系，不能默认由 reactive value 全局决定。
- Paint 与 Layout 是不同传播语义，不能由一个通用 boundary 同时截断。
- draw-item cache 只有在 widget、build context 与 inherited paint context 都兼容时才能复用。
- 命令录制只消费 immutable snapshot，不读取 live tree。
- attach / detach / reparent / 析构后，不得留下 reactive、cache 或调度队列悬空引用。

## 1. 当前架构事实

### 1.1 已有能力

- `Reactive<T>::get()` 在 paint 期间收集 `UIElement*` dependent。
- `ReactiveBase::notifyDependents()` 按 `Paint / Layout` 粒度立即标脏。
- `UIElement` 持有 per-widget `_bPaintDirty`。
- `WidgetTree` 双缓冲保存每个 widget 的 draw-item segment。
- `buildSnapshot()` 每帧遍历完整 WidgetTree：
  - dirty widget 重跑 `paintSelf()`；
  - clean widget 复用 draw-item segment；
  - children 仍会被递归访问。
- `_bVolatile` 是绕过增量缓存的一致性兜底。
- `VisualFlag` 为部分 transient bool 提供“写入即 paint dirty”。
- `GuiPerfStats` 已提供 layout/paint 时间、painted/rebuilt widget 数和 draw-item 数。

### 1.2 当前限制

- 大量公开视觉字段允许直接写入，无法自动区分 Paint / Layout 失效。
- Workbench presenter 每帧复制 model 到 widget 字段，只能靠 `_bVolatile` 保证显示一致。
- `markPaintDirty()` 只标记当前 widget，不向 ancestor 传播。
- `invalidateSubtree()` 只向 descendants 递归，当前没有真实调用者。
- 当前没有可跳过完整 subtree traversal 的缓存边界。
- `UIContainer` / `UIScrollViewport` / `UISplitPane` 覆盖完整 `paint()`，绕开基类的统一 per-widget cache 流程。
- `ReactiveBase` 的 dirty level 挂在 value 上，无法表达同一 value 被 Paint 与 Layout property 同时消费。
- `UIText` 的 bound text/style 在 paint 中解析，但 AutoSize measure 仍读取裸 `_text/_fontSize`。
- draw-item cache 以 `UIElement*` 为 key，却缓存了受 `uiScale`、`offset`、resolver 和 ancestor clip 影响的最终像素数据。
- draw-item cache 只解决 live widget 的 paint segment 复用，不承担声明式 tree/list diff。

## 2. 架构分层

```text
数据契约层
  model / Reactive / binding / presenter description

live tree 层
  UIElement / WidgetTree / UILayout / UISlot

失效策略层
  Paint dirty / Layout dirty / Volatile / future subtree cache

输出机制层
  paint walk / draw-item cache / UIFrameSnapshot / Render2D
```

边界约束：

- 数据契约层不得直接依赖 draw-item cache。
- 声明式 description 若引入，只负责 reconcile live tree，不直接复用 snapshot cache。
- live widget identity 是 draw-item cache identity 的一部分，不是完整 validity 条件。
- 当前不抽象通用引擎级 Reactive/Invalidation 库；等第二个真实消费者出现后再提炼。

## 3. Dirty 语义

### 3.1 Paint dirty

适用于只改变当前 widget 输出 draw items、但不改变几何关系的变化，例如：

- fill/text color；
- hover / pressed / selected；
- 文本内容在固定尺寸控件中变化；
- image/texture；
- visibility 在不参与 layout 变化的明确场景。

当前语义：

```text
widget paint dirty
    → 当前 widget 下次重跑 paintSelf()
    → siblings/ancestors 不必重建
    → 每帧 tree traversal 仍然存在
```

### 3.2 Layout dirty

适用于可能改变自身或其他节点 rect/desired size 的变化，例如：

- size / position / anchors；
- AutoSize 文本变化；
- padding / spacing / margin；
- slot Auto/Fill/weight/min/max；
- child attach/detach/reorder；
- visibility 导致 layout participation 改变。

当前 `WidgetTree` 是全树 layout dirty。第一阶段保持这个正确且简单的模型，不提前引入局部 layout。

### 3.3 Subtree invalidation

现有 `invalidateSubtree()` 没有真实消费者，也不是 Flutter/Slate 式 boundary 的基础。

处理决定：

- Phase 1A 不依赖它。
- 先审计 clip、visibility、future transform/opacity/theme 等 inherited paint inputs。
- 若 ancestor paint context 改变需要使 descendants 的 resolved segment 失效，则保留并重命名/明确该 API。
- 只有确认不存在真实消费者时才删除，避免错误地移除当前最小的 subtree correctness 工具。

### 3.4 Boundary

未来只考虑 **Subtree Paint Cache Boundary**：

```text
descendant paint dirty
    → 聚合到最近 paint-cache boundary
    → clean boundary 可复用整个 flattened subtree segment
    → 跳过 boundary 内 paint traversal
```

Paint boundary：

- 不阻断 layout dirty；
- 不等于 GPU offscreen layer；
- 不由单纯 `_bInvBoundary` 标志位完成；
- 必须同时定义 subtree cache ownership、dirty aggregation 与 traversal skip。

局部 layout boundary 暂不在本计划内。

### 3.5 Cache validity

当前 draw-item segment 保存最终 target-pixel 数据，因此复用条件至少包括：

```text
widget live identity
+ widget paint inputs 未变化
+ UIFrameBuildContext 兼容
+ inherited paint context 兼容
+ referenced resource generation 兼容
```

第一阶段采用简单、保守的正确性策略：

- `uiScale` / `offset` 或 host 提供的 build-context generation 改变时，清空 tree 的两份 item cache；
- texture resolver 本身不做 `std::function` 比较，由 host 提供稳定 generation/token；
- ancestor clip/context 改变时，标脏受影响 subtree；
- 后续只有 profile 证明全量 cache invalidation 成本过高，才引入 context fingerprint 或 logical draw-item cache。

## 4. 执行阶段

### Phase 0 — 基线、测试与可观测性

#### 目标

在改变更新策略前，建立可比较基线和生命周期门禁。

#### 工作项

1. 为 reactive invalidation 增加轻量计数：
   - `notifyCalls`
   - `dependentVisits`
   - `paintDirtyTransitions`
   - `layoutDirtyTransitions`
   - `cacheInvalidationsByContext`
2. 增加 debug-only invalidation reason：
   - `PaintProperty`
   - `LayoutProperty`
   - `ReactivePaint`
   - `ReactiveLayout`
   - `ChildStructure`
   - `GeometryChanged`
   - `BuildContextChanged`
   - `InheritedPaintContext`
   - `Volatile`
3. 记录 Workbench 稳态帧和交互帧：
   - `layoutMS`
   - `paintMS`
   - `paintedWidgets`
   - `rebuiltWidgets`
   - `drawItems`
4. 增加最小测试：
   - 单个 reactive paint 变化只重建 dependent；
   - layout reactive 在同一 snapshot 前触发布局；
   - 同一 reactive 被 Paint 与 Layout consumer 同时读取时，各自得到正确失效；
   - AutoSize `UIText` 的 bound text/font size 变化在同一 snapshot 前重新 measure；
   - reactive 先析构、widget 后析构；
   - widget detach/析构后 reactive set 不访问悬空对象；
   - clean frame snapshot digest 稳定；
   - clean tree 改变 `offset` / `uiScale` 后，sprite/text/clip 使用新 context；
   - resolver generation 改变后不复用旧 texture segment；
   - ancestor clip/context 改变后 descendants 不复用不兼容 segment。

#### 完成门禁

- 有一组可重复的 Workbench baseline 数据。
- 后续阶段能比较 rebuilt/traversed/notify/cache invalidation 的变化，并看到 last invalidation reason。
- 生命周期测试通过。
- AutoSize binding、build context 与 inherited clip 的 correctness 测试通过。

### Phase 1A — Runtime 属性与 Binding 失效收口

#### 目标

让现有 runtime mutation 与 reactive binding 不再依靠“记得手动 mark dirty”或
`_bVolatile` 保证正确性。

#### 设计

为会在运行时变化的视觉/布局属性提供 changed-only setter：

```cpp
void setText(std::string value);       // Paint 或 Layout，取决于控件尺寸契约
void setColor(glm::vec4 value);        // Paint
void setSelected(bool value);          // Paint
void setVisibility(...);               // Paint + 可能 Layout
void setSize(glm::vec2 value);         // Layout
void setPosition(glm::vec2 value);     // Layout
```

规则：

- 值未变化时不标脏。
- setter 内部决定 dirty level，调用方不决定。
- 不一次性封装全部 authoring 字段，只迁移真实运行时写路径。
- `_bVolatile` 只保留给真正逐帧变化且不值得建立依赖的内容。

#### Property-aware binding

Reactive dirty level 必须逐步从 value-global：

```cpp
ReactiveBase::_dirtyLevel
```

迁移为 dependency/property edge 语义：

```cpp
struct ReactiveDependent
{
    UIElement* widget;
    EDirtyLevel dirtyLevel;
};
```

或者等价的最小实现。要求：

- 同一 reactive 可以同时拥有 Paint 与 Layout consumers；
- edge identity 至少包含 dependent widget 与消费语义，不能因同一 widget 的多个 property 互相覆盖；
- paint-collected dependency 重新收集时更新 edge dirty level；
- bind-time persistent dependency 与 paint-collected dependency 分开管理；
- `clearDependencies()` 只清除本次 paint 收集的 edges，不能清掉 layout/slot 等 persistent binding；
- unbind、rebind、detach 与析构必须显式移除 persistent edge；
- dirty level 由读取的 property/控件尺寸契约决定；
- AutoSize text/font/padding 等 layout input 订阅为 Layout；
- fixed-size text color/content 等只影响输出时订阅为 Paint；
- layout dependency 不依赖 paint 是否产生 draw item。

`UIText::computeDesiredSize()` 必须使用与 paint 一致的 resolved text/style，不能继续读取
与 binding 脱节的裸 `_text/_fontSize`。

`UISplitPane::bindSplitRatio()` 等现有 bind-time registration 必须先迁移为 persistent edge；
否则 Phase 2 统一基类 paint 后，`clearDependencies()` 会清掉该 Layout binding。

#### Workbench 迁移

优先迁移：

- row selected；
- row label text；
- selection highlight visibility/size/position/color；
- preview/name/color/size text；
- visible toggle text。

迁移后删除对应 `_bVolatile`，删除 presenter 内手写的 `_last*` 与 `bGeometryChanged` 补偿逻辑。

#### 完成门禁

- Workbench presenter 不直接写上述运行时视觉字段。
- 稳态帧不因 presenter 同值赋值产生 rebuild。
- interaction/smoke、CPU snapshot、GPU/offscreen parity 全部通过。
- `_bVolatile` 使用点有逐项理由，不能继续作为普通 binding 替代品。
- 同一 reactive 的 Paint/Layout mixed consumers 均正确。
- AutoSize binding 的 measure/paint 使用同一 resolved value。
- paint rebuild 不会移除 split ratio、layout、slot 等 persistent binding。
- rebind/unbind/detach/destructor 后 reactive 不保留旧 persistent dependent。

### Phase 1B — 强制 Property Mutation 契约

#### 目标

结束“公开字段和 setter 长期并存”的迁移状态，从架构上禁止新的 runtime direct write。

#### 设计

- runtime mutable visual/layout property 改为 private/protected backing field；
- 业务、事件、presenter、脚本只通过 changed-only setter/property API 写入；
- document instantiate 可走 construction/authoring path；
- reflection/editor 批量编辑通过显式 mutation transaction，结束时按最高 dirty reason 统一 invalidate；
- 不为了该阶段引入通用 property framework，优先复用现有反射与最小 transaction。

#### 迁移门禁

- Framework/GUI 外不得新增 `->_text` / `->_size` / `->_position` / `->_visibility` 等 runtime 直接赋值；
- 可用 grep/script 作为过渡门禁；
- public field 只允许仍未迁移的 authoring-only 数据，并登记清单；
- Phase 1B 完成后删除过渡清单。

### Phase 2 — 统一 Paint 模板

#### 目标

使所有 widget 都经过同一 cache/dirty/lifecycle 框架，为未来 subtree cache 留下唯一扩展点。

#### 设计

`UIElement::paint()` 保持唯一 traversal 模板：

```text
visibility check
→ count widget
→ establish reactive paint context
→ rebuild/reuse self segment
→ begin children paint context
→ paint children
→ end children paint context
```

布局 host 只定制 children context：

- `UIContainer`：可选 clip；
- `UIScrollViewport`：viewport clip；
- `UISplitPane`：每个 child 使用独立 pane clip；
- 其他控件不得完整复制 `paint()`。

优先复用现有最小 customization point：

```cpp
virtual void paintChildren(UIFrameBuilder&);
```

基类默认遍历；Container/Scroll/Split 只覆盖 children traversal/context，不复制
visibility、reactive stack、self rebuild/reuse、perf counter 流程。不引入独立
paint-policy interface。

同时在 `WidgetTree::buildSnapshot()` 建立 build-context generation/cache invalidation
入口；ancestor clip/context setter 负责 subtree invalidation。

#### 完成门禁

- `UIContainer` / `UIScrollViewport` / `UISplitPane` 不再覆盖完整 paint pipeline。
- reactive stack 必须成对恢复；优先用 scope guard/RAII 防止未来 early return 破坏栈。
- perf counter 对所有 widget 语义一致。
- clip/scissor snapshot parity 不变。
- 同一 clean tree 连续使用不同 build context 时输出正确。
- ancestor clip/context 改变后 descendant cache 输出正确。

### Phase 3 — 是否需要 Reactive 批处理

#### 启动条件

只有 Phase 0/1 数据证明以下任一项成立才实施：

- `dependentVisits` 在实际交互中是明显热点；
- 同一 reactive 或同一 widget 每帧被重复 notify 大量次数；
- style/theme 广播导致可测量 CPU 开销。

否则关闭该阶段，保留立即 notify。

#### 正确 flush 时机

```text
event/model mutation
→ flush pending invalidations
→ WidgetTree layout decision
→ paint/snapshot
```

禁止以“帧末 flush”作为默认语义。

#### 生命周期约束

- 队列不能无保护持有可能析构的 `ReactiveBase*` / `UIElement*`。
- 必须支持一个 reactive 依赖多个 WidgetTree。
- detach/reparent/destructor 必须能取消 pending invalidation。
- layout dirty 优先级高于 paint dirty；同一 widget 合并为最高 dirty level。
- 必须保留显式同步 flush，以支持同调用栈内读取更新后 snapshot 的测试/automation。

#### 停止线

如果安全队列需要引入复杂全局 scheduler、跨线程协议或大量 shared ownership，而 profile 收益有限，则不实现。

### Phase 4 — 是否需要 Subtree Paint Cache Boundary

#### 启动条件

只有 profile 证明：

- clean frame 的 `paintedWidgets`/tree traversal 是主要瓶颈；
- per-widget draw-item reuse 已不足以满足大型静态树；
- 存在稳定、高频更新局部与大型静态区域的真实页面。

#### 必须先定义

1. boundary 的 subtree segment 是否包含自身和所有 descendants；
2. descendant paint dirty 如何向最近 boundary 聚合；
3. attach/detach/reparent/z-order/visibility/clip 如何使 boundary cache 失效；
4. layout 后哪些 rect 变化使 segment 失效；
5. nested boundary 的 ownership；
6. cache swap 与 widget 析构清理；
7. clean boundary 如何真正跳过 `paintChildren()`。

#### 非目标

- 不做 GPU render-to-texture layer。
- 不做局部 layout。
- 不把 paint boundary 暴露成普遍需要手调的 widget flag；先由明确容器/API 承载。

#### 完成门禁

- boundary 内一个 child paint dirty 时，boundary 外 sibling 不重建。
- clean boundary 帧的 visited widget 数显著下降，而不仅是 rebuiltWidgets 下降。
- boundary 内 desired size 改变仍正确触发全树 layout。
- nested clip、scroll、visibility、reparent snapshot parity 全部通过。

### Phase 5 — Keyed 声明式列表 Reconciliation

#### 启动条件

Phase 1A/1B 的 setter/binding 仍不能低成本解决动态列表，且至少出现两个真实消费者需要：

- 根据 description 增删/re排 live widgets；
- 按业务 identity 保留 focus/selection/input transient state；
- 避免列表全量 detach + recreate。

#### 范围

先只做列表，不做通用 Virtual DOM。

```text
vector<RowDescription>
    → key/type match
    → create/update/move/remove live row widgets
```

#### Identity 契约

stable key 是 reconciliation 的前置条件，不是后续优化：

- key 在同一 sibling list 内唯一；
- key 表示业务 identity，不能使用当前 index；
- type 改变视为 replace；
- move 保留 live widget identity；
- remove 必须清理 focus/capture/hover/drag 与 slot；
- duplicate key 在 debug 构建 assert，并在 release 明确降级策略。

#### 与 draw-item cache 的关系

- reconciler 更新 live WidgetTree；
- setter/binding 产生 Paint/Layout dirty；
- 现有 draw-item cache 决定重建或复用；
- 不修改 `UIFrameBuilder::bindCache()` 来承担 diff。

#### 完成门禁

- insert/remove/reorder 保留未变化 row 的 live identity。
- focus/selection/hover 不因 reorder 错位到其他业务项。
- Workbench row list 不再全量 detach/recreate。
- 与全量 rebuild 输出保持 snapshot parity。

## 5. 验收矩阵

| 场景 | 必须满足 |
|---|---|
| clean frame | snapshot digest 稳定；除明确 volatile 外无 rebuild |
| 单个 paint 属性变化 | 只重建对应 widget |
| 同值 setter | 不产生 dirty transition |
| AutoSize 文本变化 | 同帧 layout，最终 rect 与文本正确 |
| AutoSize bound text/style 变化 | resolved measure 与 paint 一致，同帧 layout |
| 同一 reactive 的 mixed consumers | Paint consumer 不漏 paint，Layout consumer 不漏 layout |
| layout property 变化 | layout 一次，相关 rect 的 draw item 更新 |
| 连续多次 set | 同一 snapshot 显示最终值 |
| reactive 析构 | dependent 不保留悬空 ref |
| widget detach/析构 | 后续 reactive set 安全 |
| reparent | 旧 slot/cache/dependency 清理，新 parent 布局正确 |
| scroll/clip | cache reuse 后 clip 仍正确 |
| build context offset/uiScale 变化 | clean widget 不复用不兼容 target-pixel segment |
| resolver generation 变化 | image widget 不复用旧资源 segment |
| ancestor paint context 变化 | 受影响 descendants 的 segment 正确失效 |
| hidden/collapsed 切换 | render、hit test、layout participation 契约一致 |
| keyed insert/remove/reorder | identity 和 transient state 不错位 |
| boundary clean frame | 真正跳过 subtree traversal |
| boundary 内 layout 变化 | 不被 paint boundary 截断 |

## 6. 决策记录

### 已决定

- 不直接实现 `_bInvBoundary`。
- 不把 paint boundary 与 layout boundary 合并。
- 不把 `bindCache/cacheItems` 视为 Virtual DOM diff 接口。
- stable key 先于 keyed diff。
- Workbench 先使用 changed-only setter/binding 消除 volatile。
- Reactive dirty level 迁移为 dependency/property edge 语义。
- build context 与 inherited paint context 纳入 cache validity。
- public field + setter 只作为迁移态，最终禁止 runtime direct write。
- batching、subtree boundary、declarative reconciliation 都由 profile/真实消费者触发。
- 暂不把 Reactive 泛化为引擎公共库。

### 待实施中验证

- visibility 的 setter 是否统一 Layout dirty，还是根据 `Hidden/Collapsed` 转换精确判断。
- reflection/序列化 mutation transaction 的最小接口形态。
- build context token 由 host 显式提供，还是由 `WidgetTree` 比较 scale/offset 并附加 resource generation。
- inherited paint input 审计后，`invalidateSubtree()` 是保留、重命名还是替换为 generation。

## 7. 推荐执行顺序

```text
Phase 0 baseline/tests
→ Phase 1A runtime property/binding invalidation + Workbench migration
→ Phase 1B enforce property mutation contract
→ Phase 2 unified paint template + cache validity
→ 重新 profile
→ 按数据选择 Phase 3 batching 或 Phase 4 subtree cache
→ 出现真实动态列表复用需求后做 Phase 5 keyed reconciliation
```

Phase 3、4、5 都不是默认必做项。Phase 1A/1B/2 完成后若性能和可维护性已经达标，计划在此收口。

## 8. 跨框架能力吸收边界

本计划不以“复制某一个框架”为目标，而是按问题吸收成熟机制：

- WPF / Flutter：property 自带 Paint/Layout impact，setter 无法绕过失效协议；
- Vue / Qt Bindable Properties：动态 dependency edge、重新收集与 computed/binding 语义；
- Slate：typed invalidation reason、CPU subtree cache、volatile island 与诊断工具；
- React：单向数据流、pure description、stable identity/key 与条件式 reconciliation；
- Qt Widgets：update coalescing 与 dirty region，作为未来区域级优化候选；
- Godot：cached draw commands + explicit redraw request；
- Dear ImGui：最小化业务侧 presentation state 同步，为高动态工具 UI 保留 immediate/volatile island；
- Flutter：boundary placement 必须由数据证明，不能默认到处插边界。

逐项决策、采用阶段和停止线以 `framework-lessons.md` 为准。
