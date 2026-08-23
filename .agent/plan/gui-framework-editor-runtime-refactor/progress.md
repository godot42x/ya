# GUI Framework / Editor / Game UI 重构进度

## 已完成

- [x] 完成当前 theme/style 系统 review。
- [x] 确认 UITheme 为可选 framework/tooling 机制。
- [x] 确认 Game UI 允许 project/application authored appearance。
- [x] 确认未来 Editor 移除 ImGui，统一使用 WidgetTree framework。

## 未开始

- [ ] Phase -1：React-style 函数式 DSL、stable key、reconciler 与数据流契约。
- [ ] Phase 0：基线、契约与迁移护栏。
- [ ] Phase 1：visual property 与 invalidation。
- [ ] Phase 2：Brush 与基础视觉原语。
- [ ] Phase 3：统一控件 visual state。
- [ ] Phase 4：yaui authored appearance。
- [ ] Phase 5：Editor 控件与迁移准备。
- [ ] Phase 6：ImGui 移除与三宿主收敛。

## 当前结论

WidgetTree、布局、输入、snapshot、compose 和 host 边界可继续作为长期基础；主要重构对象是视觉属性契约、Brush 绘制能力、控件状态、文档序列化和 Editor 控件覆盖，而不是强制扩展 UITheme。

## 下一步

当前新增决策：第一阶段先做 React-style 函数式 DSL。UI 函数返回 UIBuilder/UIDescription，reconciler 应用到 retained WidgetTree；不先做 XML，也不让 DSL 替代 WidgetTree。

实施策略进一步收敛为四个小步：-1A 契约冻结、-1B 最小 reconciler、-1C 一个有状态控件、-1D 最小数据流。每一步都必须独立编译、自动化测试和可回滚，再进入下一步。

在实现 Phase 0 前，应先完成 Phase -1 的 DSL、stable key、状态保留、数据流和生命周期决策；Phase 0 的测试矩阵应覆盖 DSL reconcile，而不只是手写 retain builder。
