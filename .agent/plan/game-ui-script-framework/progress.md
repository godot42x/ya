# Progress

## 2026-09-28 — 立项

完成：

- 盘点硬编码与缺口，结果写入 `plan.md` §1（H1–H17 与游戏侧弥补代码）。
- 核对的关键事实：
  - `App::_bPause` 全仓库无写入点，暂停不存在；暂停时 `tickLogic` 整段跳过（含输入状态、文件监视、模块）。
  - `LuaScriptingSystem::onUpdate` 按 EnTT 存储顺序遍历，`onInit` 在同一循环里懒调用。
  - UI `update` 位于 `RuntimeRenderContext::buildGameRenderFrame`，依赖 display root 存在。
  - 按钮动作只在 `GameUIHost::setMountedRoots` 时绑定一次。
  - `UICanvasLayout::resolveChildRect` 点锚点轴取整个父尺寸作为对齐区域。
  - `game-ui-authoring` 已约定：不引入 UUserWidget 平行类型、不造中心事件总线、设计器预览不 tick。
- 确定方向：脚本实例与宿主解耦；GUI 只存不透明行为描述；预览不激活；帧顺序成为契约。

未开始：F0–S7 全部。

待确认：`plan.md` §6 决策门 D1–D9。F0 依赖 D1、D2、D5；S3 依赖 D4、D8；S4 依赖 D3；S5 依赖 D6、D7、D9。

下一步：用户确认决策门后开始 F0。

## 2026-09-28 — 决策门 D1 / D2 / D5 确认

用户确认：

- D1：UI 更新移到 GameLogic 之后的 UILogic；接受 UI 脚本读到上一帧布局，句柄提供显式立即布局；
  `setPresentation` 在 UILogic 之前确定。
- D2：修正原建议「系统整组受暂停」——`_systems` 里同时有玩法模拟（Physics、SkeletonAnimation）
  与引擎维护（ModelInstantiation、Transform、LinkageFramework）。改为系统显式声明类别，只有玩法
  模拟受暂停；模块 onLogic、TaskManager、自动化、文件监视、输入状态更新不受暂停；`TimerManager`
  在 F0 审计后决定是否拆时钟；暂停 API 单一写入口、计数语义。
- D5：顺序键 `(executionOrder, 场景树前序, 实体内脚本下标)`；脚本声明默认值、实例可覆盖；
  增加 `onStart`，每帧 onInit → onStart → onUpdate 三遍，onUpdate 中新建的实例下一帧进入。

计划同步：`plan.md` §3 帧顺序与生命周期表、§4 F0 条目与测试、S3 句柄、§6、§8；
`feature_matrix.json` decisions 与 F0 features。

仍待确认：D3、D4、D6、D7、D8、D9（不阻塞 F0）。

下一步：开始 F0。
