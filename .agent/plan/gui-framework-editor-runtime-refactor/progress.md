# GUI Framework / Editor / Game UI 重构进度

## 已完成

- [x] 完成当前 theme/style 系统 review。
- [x] 确认 UITheme 为可选 framework/tooling 机制。
- [x] 确认 Game UI 允许 project/application authored appearance。
- [x] 确认未来 Editor 移除 ImGui，统一使用 WidgetTree framework。

## 进行中 / 未开始

- [x] Phase -1A：React-style DSL 最小契约与测试骨架。
- [x] Phase -1B：最小 reconciler。
- [x] Phase -1C：一个有状态控件。
- [x] Phase -1D：最小数据流。
- [ ] Phase 0：基线、契约与迁移护栏。
- [ ] Phase 1：visual property 与 invalidation。
- [ ] Phase 2：Brush 与基础视觉原语。
- [ ] Phase 3：统一控件 visual state。
- [ ] Phase 4：yaui authored appearance。
- [ ] Phase 5：Editor 控件与迁移准备。
- [ ] Phase 6：ImGui 移除与三宿主收敛。

## 当前结论

WidgetTree、布局、输入、snapshot、compose 和 host 边界可继续作为长期基础；主要重构对象是视觉属性契约、Brush 绘制能力、控件状态、文档序列化和 Editor 控件覆盖，而不是强制扩展 UITheme。

补充结论：DSL 不能继续朝单一肥 UIBuilder 扩张。公共 API 应拆成控件专属 builder/factory，底层才保留统一的描述节点和 reconcile contract；这样 Game UI / Editor UI 都能在同一套 tree/renderer 上演进，而不是被一个大而全的 builder 锁死。

## 下一步

当前新增决策：第一阶段先做 React-style 函数式 DSL。UI 函数返回 UIBuilder/UIDescription，reconciler 应用到 retained WidgetTree；不先做 XML，也不让 DSL 替代 WidgetTree。

已完成：Phase -1A 契约文档落盘，新增契约测试骨架 DeclarativeContractTest.cpp，并通过独立目标 ya-gui-declarative-contract-test（7/7）。

已完成：Phase -1B 最小 reconciler（same-key reuse / insert / remove / reorder / detach）。

已完成：Phase -1C 状态保留控件验证（TextField focus / Button remove 清理）。

实施策略进一步收敛为四个小步：-1A 契约冻结、-1B 最小 reconciler、-1C 一个有状态控件、-1D 最小数据流。每一步都必须独立编译、自动化测试和可回滚，再进入下一步。

其中 -1A 现在要先把 builder 的分层定清：公共 node contract、控件专属 builder、共享属性注入点，而不是先把一个通用 builder 字段集补到全量。

2026-08-24 checkpoint：完成 DSL builder 分层第一步。外层新增 UIContainerBuilder/UIPanelBuilder/UITextBuilder/UIButtonBuilder/UITextFieldBuilder，各自只暴露对应语义；reconciler 统一接收 UIDescription。独立目标 ya-gui-declarative-contract-test 构建通过，7/7 通过。

2026-08-24 checkpoint：完成 Phase -1D 最小数据流。新增 UIRenderController，支持 render function、显式 invalidate、batch begin/end、单次 flush；新增两条测试验证外部 state 回写和批量更新，独立目标共 9/9 通过。

2026-08-24 checkpoint：Phase 0 外观基线第一批完成。新增 no-theme / authored-color / theme-only 三种 appearance mode 测试；验证无 theme 可渲染、显式 authored color 可独立生效、theme-only 可由 UITheme 提供外观。独立目标共 10/10 通过。

2026-08-24 checkpoint：补齐 declarative identity 诊断。UIReconciler 在 reconcile 前验证同一 parent 下重复 stable key，并返回包含路径与 key 的稳定错误；新增契约测试，独立目标共 11/11 通过。

2026-08-24 checkpoint：Phase 0 snapshot/host 基线取证。ya-gui-declarative-contract-test 12/12 通过，新增重复 build 的 snapshot 稳定性断言；ya-gui-headless-host-test 构建并运行 2/2 通过。ya-gui-closure-test 当前仍被既有 DockNodeTest.cpp 使用不存在的 FDockTreeModel::getRoot() 阻塞，未归因于本次改动。

2026-08-24 checkpoint：standalone windowed smoke 基线修复并通过。GUIWorkbench 原 smoke 硬编码 tabs[7]，新增 Gallery/Interactions/Dock/Theme 页面后 Editor 页已迁移到动态索引；改用 getEditorPageIndex() 后真实 Vulkan windowed smoke 通过，完成初始化、交互、Editor 切换、teardown。

2026-08-24 checkpoint：offscreen/headless parity 取证。GUIWorkbench windowed 与 headless 的 ScrollSplit snapshot JSON 均成功生成，但结构不一致：windowed 101 draw items，headless 53 draw items，structural/semantic digest 均不同。因此 validation.snapshot 继续保持未完成，下一步先修复 host 首帧/字体/shell 构建时序，再进入 offscreen-diff 绿灯。

2026-08-24 checkpoint：修复 FontManager::registerFont 的 DPI-qualified cache key。headless 预注册字体现在可被 getFont() 命中，ScrollSplit snapshot 从 53 draw items 恢复到 100（windowed 101），文本从 0 恢复到 47；剩余差异为 synthetic font metrics 与真实字体度量差异，exact digest parity 仍未通过。ya-gui-headless-host-test 2/2、ya-gui-declarative-contract-test 12/12 通过。

2026-08-24 checkpoint：为 FontManager 预注册字体增加 DPI cache 回归测试；ya-gui-declarative-contract-test 13/13 通过。后续 exact windowed/headless parity 仍需解决 synthetic font metrics 差异，当前不标记 validation.snapshot 完成。

2026-08-24 checkpoint：补齐 GUIWorkbench headless synthetic font sizes（9/10/11/12/13/14/15/16/20/24/32/40）。ScrollSplit windowed/headless snapshot 现在均为 101 draw items、48 text items；剩余仅为 synthetic font metrics 导致的几何/digest 差异，validation.snapshot 仍保持 in_progress。

2026-08-24 checkpoint：明确 cross-host parity 分层。新增 UIFrameSnapshotTest 回归断言：text 的字体度量变化会改变 structural digest，但不改变 semantic digest；semantic digest 作为 windowed/headless 结构门禁，windowed/offscreen 仍要求 exact parity。完整 ya-gui-closure-test 仍被既有 DockNodeTest.cpp 的 getRoot() 编译错误阻塞。

2026-08-24 checkpoint：收敛 macOS convergence 脚本的 parity 断言。gui_convergence_macos_validation.py 现在比较 windowed/headless 的 semanticDigest 与 draw-item 数量，不再错误要求 synthetic font 与真实字体的 structural JSON 完全相同；脚本通过 py_compile，已用现有 ScrollSplit evidence 验证 semantic parity PASS。

在实现 Phase 0 前，应先完成 Phase -1 的 DSL、stable key、状态保留、数据流和生命周期决策；Phase 0 的测试矩阵应覆盖 DSL reconcile，而不只是手写 retain builder。
