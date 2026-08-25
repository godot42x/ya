# Phase -1A：React-style DSL 最小契约

状态：核心契约已实现并通过独立契约测试；后续仍需补齐完整生命周期/keep-alive 语义。

## 目标

定义 React-style C++ UI function、UIDescription 和 retained WidgetTree 之间的最小稳定边界。此文档只冻结语义，不承诺最终 API 拼写，也不引入 XML/CSS。公共 API 预期是控件专属 builder/factory；UIDescription 只负责承载统一的内部节点契约。

## 描述树与 live tree

render(state, props) -> immutable UIDescription -> Reconciler -> retained UIElement/WidgetTree -> layout / input / UIFrameSnapshot

UIBuilder 只是当前过渡实现里的一个名称，后续更稳妥的 API 允许不同控件暴露各自的 builder/factory；但无论外部形态如何，进入 reconcile 的都必须是统一的 description 节点。

UIDescription 是一次 render 的纯描述；它不能持有 live UIElement、WidgetTree、GPU resource 的裸指针，也不能在 command recording 期生成。Reconciler 是唯一允许根据 description 创建、更新、移动和 detach live widget 的组件。现有手写 retain API 在迁移期继续存在，但 DSL 不得绕过 Reconciler 修改同一棵树的结构。

## Identity

每个 description node 的身份是：`(parent identity, type identity, stable key)`。

- type identity 是控件类型，不能用 display name 代替。
- stable key 是同一父节点下的业务身份；列表 reorder 时状态跟随 key，不跟随 index。
- display name 只用于诊断、可访问性和调试，不参与 reconcile identity。
- 同一 parent 下重复 key 是 description 错误，必须报告稳定的节点路径和 key。
- 没有 key 的节点只允许作为明确的 positional child；第一版不为无 key 列表提供状态保留保证。
- key 作用域默认是直接 parent；跨 parent 移动视为 remove + insert，不隐式迁移 focus、capture 或 transient state。

## Lifecycle

Reconciler 顺序固定为：validate -> match -> update props -> reconcile children -> remove stale -> finalize invalidation。

- mount：首次生成 live widget。
- update：同 identity 更新 props，不得无条件重置 transient state。
- reorder：同 parent 下 child 顺序变化，keyed child 保留 live widget。
- remove：先清理 tree-owned focus/capture/hover/popup/drag，再 detach。
- unmount：widget 不再属于 tree，任何 callback/依赖不得继续访问它。

第一版不实现 keep-alive cache；只有同一 retained tree 中的 same-key reuse 保留状态。

## State ownership

| 类型 | 示例 | owner | render 是否覆盖 |
|---|---|---|---|
| 外部 model state | selected id、document value、open flag | app/project/editor model | 通过 props 更新 |
| widget transient state | focus、pressed、caret、hover、pointer capture | WidgetTree / widget | 不因普通 update 重置 |
| derived state | filtered rows、desired size、resolved visual state | description/reconciler/layout | 可由输入重新计算 |

render function 不得把 widget transient state 镜像回外部 model，除非通过明确事件或 controller API。props 更新不得重置 focus/caret/scroll 等 transient state。

## Data flow

第一版采用单向数据流：

external state -> render function -> description props -> reconciler -> widget
widget event -> callback/controller -> external state update -> next render

- render 读取 state/props，不直接修改 model；
- event callback 可以请求 model mutation，但不能在 callback 中重建整棵 WidgetTree；
- model transaction 结束后由 host 触发一次 reconcile；
- 相同值 props 不产生额外 widget mutation 或 dirty transition；
- Reactive 先复用现有依赖追踪和 invalidation，computed、自动双向 binding、异步 effect 后置。

## Appearance、事件和资源

description authored appearance 不依赖 UITheme，优先级为：explicit description prop > widget-local authored default > optional theme/style lookup > framework neutral fallback。

callback 必须捕获稳定的 model/controller handle，不得长期捕获 render 临时对象或裸 widget 指针。remove/unmount 后旧 callback 不得再被 dispatch。description 只保存可复制的 resource reference/brush data；resolved texture/font 由 snapshot build context 解析；GPU resource 由 snapshot 保持到 queue submit 完成；reconcile 不创建 GPU resource。

## 第一版契约测试

1. duplicate key 报告稳定 parent path + key；
2. same key 两次 render 复用同一 live widget identity；
3. display name change 不触发 remove/insert；
4. keyed reorder 保持 live widget identity；
5. conditional remove 清理 focus/capture/hover/popup/drag；
6. TextField focus/caret 不因 props update 丢失；
7. same-value props 不产生额外 dirty transition；
8. no theme 时 authored appearance 可独立渲染；
9. unmount 后旧 callback 不再触发；
10. description/reconcile 不在 command recording 期访问 live model。

## 进入 Phase -1B 的条件

- 本文契约完成 review；
- 上述测试至少有测试骨架和失败语义；
- 无未决的 key 作用域、state owner、remove 清理顺序问题；
- 现有 WidgetTree、GUIWorkbench、GameUIHost 测试继续通过；
- 不引入 XML/CSS parser，不迁移 Editor panel。
