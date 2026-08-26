# Phase -1A：Retained 基础与静态强类型 DSL 最小契约

状态：核心契约已实现并通过独立契约测试。2026-08-26 起默认构建路径改为 **DSL 直接物化 live widget**；下文中与 Description/reconcile 相关的条款只约束那条可选边界路径，不再约束静态 DSL。

## 目标

定义 retained runtime、Slate/EUI-NEO 风格静态强类型 builder 与 live WidgetTree 之间的最小稳定边界。此文档只冻结语义，不承诺最终 API 拼写，也不引入 XML/CSS。静态 builder 不依赖 component、脚本、JSON、反射或 `UIDescription`。

## 描述树与 live tree

**默认：** static typed builder → 直接物化 retained UIElement/WidgetTree → layout / input / UIFrameSnapshot。

**边界（document/script）：** 外部 schema → 短生命周期 typed spec → registry 工厂实例化一次 → 同一棵 live tree。此后更新与静态路径相同（绑定 + live API）。

**可选（变长 keyed 集合）：** 列表/repeater 控件读取 `ReactiveList`，自己拥有行的创建/复用/销毁。应用层不维护一棵与 live 树平行的 Description。

每个控件暴露自己的 typed builder；builder 的产品是对应的 `UIElement` 子类，不是通用 Description 节点。`UIDescription` 不是 GUI 基础对象。

`UIDescription` / `UIReconciler` / `UIRenderController` 已删除。静态 DSL 直接 `attach`。结构变化走 live API 或列表控件，不存在平行 Description 树。

## Identity

Live widget 的身份是 tree membership + type +（可选）`_stableKey`。列表行的 key 属于**拥有这些行的列表控件**，不是全局 Description 身份体系。

Document/script 实例化只发生一次；实例之后的身份与静态路径相同。

- type identity 是控件类型，不能用 display name 代替。
- stable key 是同一父节点下的业务身份；列表 reorder 时状态跟随 key，不跟随 index。
- display name 只用于诊断、可访问性和调试，不参与 identity。
- 同一 parent 下重复 key 是错误，必须报告稳定的节点路径和 key。
- 没有 key 的节点只允许作为明确的 positional child；第一版不为无 key 列表提供状态保留保证。
- key 作用域默认是直接 parent；跨 parent 移动视为 remove + insert，不隐式迁移 focus、capture 或 transient state。

## Lifecycle

直接构建路径：Construct → `attach`/`reparent` → 使用中靠 setter/绑定失效 → `detach` 清理 tree-owned focus/capture/hover/popup/drag。

- mount：Construct 后 `attach`。
- update：setter / `Reactive` 绑定；不得无条件重置 transient state。
- reorder：已知结构用 live child API；变长集合由列表控件内部 keyed reuse。
- remove：先清理 tree-owned focus/capture/hover/popup/drag，再 detach。
- unmount：widget 不再属于 tree，任何 callback/依赖不得继续访问它。

第一版不实现 keep-alive cache；只有同一 retained tree 中的 same-key reuse 保留状态。

## State ownership

| 类型 | 示例 | owner | 构建是否覆盖 |
|---|---|---|---|
| 外部 model state | selected id、document value、open flag | app/project/editor model | 通过 `Reactive<T>` 或显式 setter |
| widget transient state | focus、pressed、caret、hover、pointer capture | WidgetTree / widget | 不因绑定/setter 重置 |
| derived state | filtered rows、desired size、resolved visual state | widget/layout | 可由输入重新计算 |

静态构建不得把 widget transient state 镜像回外部 model，除非通过明确事件。绑定更新不得重置 focus/caret/scroll 等 transient state。

## Data flow

默认单向数据流（无 Description）：

external state (`Reactive<T>`) → widget paint/layout 读取并登记依赖
widget event → 写回 external state / `Reactive::set()` → 仅依赖该值的 widget 失效

- 构建函数读初始 state，物化 live 树；之后不重跑。
- event callback 可以请求 model mutation，但不能在 callback 中重建整棵 WidgetTree。
- 相同值 `Reactive::set` 不产生额外 dirty transition。
- command recording 期不读取 live model，只读 snapshot。

禁止恢复 `UIRenderController` / per-frame `render()` / Description diff 循环。

## Appearance、事件和资源

authored appearance 不依赖 UITheme，优先级为：explicit property > widget-local authored default > optional theme/style lookup > framework neutral fallback。

callback 必须捕获稳定的 model/`Reactive` handle，不得长期捕获构建临时对象或裸 widget 指针。Construct/事件期可以把 live widget 作为参数注入；remove/unmount 后旧 callback 不得再被 dispatch。GPU resource 由 snapshot 保持到 queue submit 完成；构建不在 command recording 期创建 GPU resource。

## 第一版契约测试

静态直接构建（新增方向）：

1. `ui::build`（或等价 API）不经 Description 能 attach column/text/button；
2. `bindText` 的 `set()` 更新可见文本且不重建 widget；
3. detach/切页后旧 callback 不再触发。

Live-tree 契约（Description 路径已删除）：

1. `ui::build` 创建的 widget 携带 registry typeId；
2. same-value setter 不产生额外 dirty transition；
3. no theme 时 authored appearance 可独立渲染；
4. TextField `setText` 不丢失 focus；
5. detach 清理 focus/capture，旧 callback 不再触发；
6. 构建与绑定不在 command recording 期访问 live model。

## 当前出口

G1.5 与 Description 删除已完成：

- `ui::build` 是静态 DSL 的唯一出口；
- Workbench DSL 页走直接构建 + `Reactive`；
- `UIDocument::instantiate()` 仍是 document/script 边界；
- 不引入 XML/CSS parser。G2 补 `UIScreen` unmount detach 与 parent-scoped mount。
