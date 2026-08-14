# GUI 数据驱动与最小闭环：响应式数据绑定 + 性能管线

> 建立日期：2026-08-15
> 输入工件：`audit.md`（能力盘点与差距分析）
> 状态：调研完成；首版 plan 经独立评审后修订（修订记录见文末 §8）。

## 0. 结论摘要

调研收敛出的架构定案：

```
Immediate API（便捷层：ImGui 式，一行一个控件；留口子，本计划不实现）
      ↓ 「声明式描述片段 → diff 复用」   ← 本计划要立的地基
Retain UI（性能底座：WidgetTree + UIFrameSnapshot）
      ↓
渲染（Render2D）
```

第一刀是 **响应式数据绑定**，采用 **事件驱动（Vue 语义）** 而非「每帧全量收依赖」：数据变 → 只重跑依赖它的 widget，非 dirty widget 的 draw items 复用上一帧。

## 1. 目标与非目标

### 1.1 目标

1. 建立**事件驱动的响应式数据绑定**：`Reactive<T>` 的 set → 标记依赖它的 widget dirty → 下一帧只重跑 dirty widget。
2. snapshot 构建从「每帧全量」改为「增量复用」：非 dirty widget 沿用上一帧 draw items。
3. dirty 粒度分级：paint（仅重画自身）/ arrange（仅重排）/ measure（重测+重排）。
4. 设计之初带性能管线；集合 reactive 留接口（TreeView 前置）。

### 1.2 非目标（本计划不做）

- 不实现 immediate mode API（留口子）。
- 不实现 Styles/StyleSet、TreeView、Grid、虚拟化列表。
- 不实现完整 CSS Flexbox / 声明式动画 / 保留层缓存（离屏纹理）。
- 不实现 touch / gamepad / 方向键 navigation。
- computed（派生状态）只留接口，不做完整实现。

## 2. 当前事实（引用 audit.md §1）

- `WidgetTree::buildSnapshot()` 每帧全量遍历，无 diff 复用。
- `UIElement` 属性靠手动 set；`UISlot` 已有 `invalidateMeasure/invalidateArrange` 两级失效，但绘制无复用。
- 无数据绑定；无集合数据源抽象。
- 渲染当前为同步消费（command recording 在帧内完成），snapshot 每帧独立重建。

## 3. 设计：事件驱动响应式数据绑定

### 3.1 核心对象：`Reactive<T>`（单值）

```cpp
template <typename T>
class Reactive {
public:
    Reactive(T value);
    const T& get() const;      // 读值 + 把「当前构建的 widget」记入 dependents
    void set(const T& value);  // 写值 + 遍历 dependents 标记 dirty（事件驱动）
    // operator T() / operator= 便捷转换
};
```

- **依赖收集发生在 widget 构建时**（只对正在构建的 widget），不是每帧全量遍历。首次构建全量，之后构建是增量的（只重跑 dirty widget）。
- **事件驱动**：`set()` 直接标记 dependents dirty，加入「待重建集合」，下一帧只重跑这些 widget。不做「每帧全量收依赖」。
- 线程假设：**GUI 单线程**（与 WidgetTree 现状一致），set/get 不加锁；跨线程 set 不在本计划（留断言）。

### 3.2 dependents 所有权与悬垂清理

- dependents 存 `UIElement*` 裸指针（与现有 `_parent` 一致），但必须配 **detach 清理**：widget 销毁/detach 时，从它读过的所有 ref 的 dependents 里移除自己。
- 复用现有 `WidgetTree` 的 detach 路径（`clearTransientInputState` 同级的清理钩子），避免 use-after-free。
- `Reactive` 自身析构时清空 dependents（或断言为空）。

### 3.3 dirty 粒度（三级，沿用引擎已有失效分级）

| 级别 | 触发 | 重做范围 |
|---|---|---|
| paint-dirty | 纯绘制属性变（Text.text / color / Button.enabled） | 只重画自身 widget，subtree 不动 |
| arrange-dirty | 位置/尺寸分配变（split ratio / box padding） | 重 arrange，不 measure |
| measure-dirty | desired size 变（文本内容变导致尺寸变） | 重 measure + arrange |

- 对应引擎现有 `UISlot::invalidateMeasure/invalidateArrange`，新增 `invalidatePaint`。
- `Reactive::set` 由「读它的 widget 声明自己关心哪一级」决定：绑定 paint 属性 → paint-dirty；绑定布局属性 → arrange/measure-dirty。

### 3.4 snapshot 增量复用与内存生命周期

- 目标：非 dirty widget 的 draw items 沿用上一帧，dirty widget 重算并替换自己的那一段。
- **内存生命周期（关键修正）**：draw items 段由「本帧 snapshot」持有，非 dirty 段是**上一帧段的引用**。为保 immutable 契约：
  - 当前渲染为**同步消费**（帧内 command recording 完成），可接受「上一帧段在本帧复用」。
  - 明确写出假设，并用**双缓冲**（draw items 池按帧轮换）隔离：本帧写入新缓冲，引用上一帧缓冲，渲染完成后上一帧缓冲归还池。
  - 若未来渲染异步化，双缓冲升级为多缓冲/ref-count。
- 保留层缓存（离屏纹理）是后续优化，但 dirty 按 widget 分段已能支撑它。

### 3.5 集合 reactive（TreeView 前置，留接口 + 最小实现）

```cpp
template <typename T>
class ReactiveList {           // 命名可在实现时定
public:
    size_t size() const;       // get 记录依赖
    const T& get(size_t i) const;
    void push(const T&);       // 通知 dependents dirty
    void removeAt(size_t);
    void clear();
};
```

- 本计划只做最小实现（size/get/push/removeAt/clear + dirty 通知），**不做 TreeView 渲染**。
- 这是 TreeView（P0 后续）的数据源地基，避免第一刀把第二块砖的地基挖歪。

### 3.6 性能评估管线（设计一等公民）

| 指标 | 实现位置 |
|---|---|
| update / layout / paint 三段耗时 | WidgetTree 各阶段计时 |
| 每帧重建的 widget 数（dirty 传播结果） | buildSnapshot 计数 |
| draw item 数 / Render2D flush 次数 | Render2D 计数 |
| 帧耗时 vs 帧预算 | host 层已有，接入 |

暴露方式：新增轻量 `GuiPerfStats`，供 dump / scenario 断言读取。

## 4. 里程碑

### M0 — 性能基线（先有测量）

- WidgetTree/Render2D 加最小性能计数（三段耗时 + 重建 widget 数 + draw item 数）。
- GUIWorkbench 页面建基线。
- **验收**：`ya-gui-closure-test` 通过；能打印 Workbench 性能计数。

### M1 — 响应式内核 + 最小局部复用（合并原 M1+M2，避免纯负收益中间态）

- `Reactive<T>`：事件驱动依赖追踪（构建时收集 + set 时 dirty 通知）+ 三级 dirty 粒度 + dependents 悬垂清理。
- snapshot 增量复用：非 dirty 段沿用、dirty 段重算，双缓冲内存生命周期。
- `Text.text` 绑定 Reactive 做最小验证（覆盖 paint-dirty）。
- **验收**：改 ref → 只有依赖 widget 重建（rebuild 计数），非 dirty widget 的 draw item 计数不变；golden 与改造前零差异；`ya-gui-closure-test` 无回归；单测覆盖「条件读取切换 ref」+「widget detach 后 set 不悬垂」。

### M2 — 控件绑定泛化 + 布局/样式属性绑定 + 集合接口

- Button.enabled（paint-dirty）、split ratio（arrange-dirty）、文本内容变尺寸（measure-dirty）各验一条。
- `ReactiveList<T>` 最小实现 + 接口（不做 TreeView 渲染）。
- computed 只定义接口。
- **验收**：三类绑定各自触发正确 dirty 粒度；性能计数显示 rebuild 数从「整树」降到「依赖子集」；scroll/split 页回归通过。

### M3 — 性能管线闭环 + 声明式口子内嵌

- 性能计数可 scenario 断言（结构证据）。
- 「声明式描述片段 → diff 复用」的输入形态在 M1/M2 的复用接口中**内嵌体现**（复用接口接受「描述片段」，而非空口定义），为 immediate API 留口子。
- **验收**：能用 dump/scenario 回答「改一处数据触发多少 rebuild/draw item/耗时」；`GUIWorkbench` 闭包不被回灌。

## 5. 验收标准（完成定义）

1. 改一个 `Reactive` 数据，只有依赖它的 widget 重建（rebuild 计数）。
2. 非 dirty widget 的 draw items 复用上一帧（draw item 计数不变）。
3. 三级 dirty 粒度各自正确（paint/arrange/measure）。
4. 视觉与改造前等价（golden / GPU shot 零差异）。
5. dependents 无悬垂（detach 清理 + 单测）。
6. 性能管线可观测；声明式 diff 复用接口已内嵌（留口子）。
7. `GUIWorkbench` / `ya-gui-closure-test` 全绿。

## 6. 风险与停止线

- **依赖收集开销本身可能比整帧 rebuild 还慢**（最大风险）：事件驱动下，dependents 遍历 + 三级 dirty 判定有常数开销。M0 建基线，M1 后对比「增量复用 vs 整帧 rebuild」的实际耗时，若增量更慢则回退整帧。
- **依赖收集完整性**：条件读取切换 ref（`if (cond) read(a) else read(b)`）必须在每次构建时重新收集（构建是增量的，dirty widget 重跑时重新收集自己的依赖），否则漏标 dirty。
- **draw items 复用一致性**：复用段与重算段顺序/数量严格对齐，否则渲染错位。双缓冲 + golden + scroll/split 回归把关。
- **use-after-free**：dependents 裸指针 + detach 清理必须成对，M1 单测覆盖。
- **跨线程 set**：本计划 GUI 单线程假设，用断言守住。
- **停止线**：若增量复用复杂度失控或收益为负，回退「整帧 rebuild + 只留性能计数」，不强行 diff。停止线挂到 M1 验收（M1 后必须拿出「增量 vs 整帧」的耗时对比再决定是否进 M2）。

## 7. 执行顺序（当前下一刀）

M0 性能基线 → M1 响应式内核+局部复用 → M2 绑定泛化+集合 → M3 管线闭环+口子。

## 8. 修订记录

- **2026-08-15 首版评审修订**（独立 agent 评审）：①「每帧全量收依赖」改为「事件驱动 + 增量构建」；② dirty 粒度三级化（paint/arrange/measure）；③ snapshot 分段复用明确双缓冲内存生命周期；④ 补集合 reactive、dependents 悬垂清理、computed 接口、跨线程假设；⑤ M1/M2 合并避免纯负收益中间态；⑥ 口子内嵌 M1/M2 而非独立 M3；⑦ 风险补「依赖收集开销可能比整帧慢」并挂停止线到 M1。
