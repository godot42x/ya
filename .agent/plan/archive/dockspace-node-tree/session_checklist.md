# Session Checklist — dockspace-node-tree

## 开工步骤

1. 读 `.agent/plan/dockspace-node-tree/plan.md` 确认当前阶段与目标。
2. 读 `progress.md` 确认上一轮完成内容与剩余风险。
3. 确认当前 main 上 P7 三栏实现可用：`xmake b GUIWorkbench` 通过；先跑现有
   `dock.jsonl` 取得基线 dump，不在视觉问题未定位时开 DockNode 重构。
4. 阶段 0 已完成：WidgetTree 已具备 move / target-change /
   finish(Dropped/NoTarget/Cancelled) observer。阶段 1 只建立 pure model 与测试，
   不开始 hover-zone、floating 或现有 DockSpace visual 替换。
5. 环境确认：关闭验证层干扰（如 86% 显存下 vkCreateGraphicsPipelines 崩溃，属环境问题非代码 bug）；优先用 headless + `--scenario-dump-dir` 验证。
6. 阶段 1 已完成：纯 `FDockTreeModel` 与 model-only tests 通过；阶段 2 才开始 `UIDockSpace` projection，禁止提前把 visual widget 指针放回 model。

## 每阶段收尾步骤

1. **编译**：`xmake b GUIWorkbench` 无 error。
2. **headless 验证**：
   ```
   xmake run GUIWorkbench --start-page Dock --scenario Example/GUIWorkbench/Scenarios/dock.jsonl --headless --scenario-dump-dir build/dock_dump
   ```
   退出码须为 0；用 python 解析 dump 断言：每栏有内容、bar 高度正常（~37px 非空栏 8px）、panel 填满 zone（~100%）、拖拽后分布正确。
3. **dump 检查清单**（GUI 可用性验证纪律，不只用断言绿灯）：
   - 每栏/每区都有预期子控件
   - bar/content 高度合理（非空栏塌缩成 0/8px）
   - 跨栏/嵌套移动后各栏分布正确
   - 浮动面板存在/消失状态正确
4. **真机手感**：headless 只证明事件路径与布局数值，不证明视觉可用性；拖拽手感/标题栏拖拽请用户在窗口模式确认。
5. **护栏自查**：
   - 移动 panel widget 子树须 keepAlive shared_ptr（防 P7 use_count==1 析构崩溃）
   - UI 结构变更必须由已成功的 DockTransaction 驱动，不能由 tab callback 直接 detach
   - Node/Panel 一律用 stable id 回查，不能跨回调缓存 view/widget raw pointer
   - 树结构变更后只在 projection 结束时显式 invalidateLayout()
   - corner drop 必须对应 compound tree + visible persistent empty leaf，不能静默退化
6. **提交**：按逻辑阶段分批提交（每阶段 1 个自洽 commit，不混入无关改动）；格式 `[gui] message` + subject + body。
7. **更新工件**：progress.md 追加本轮完成内容 + 验证结果；feature_matrix.json 更新对应 item 状态。

## 环境坑（已知）

- 验证层在 86% 显存（UE4 Debug 编辑器占用）下崩溃 → 关验证层或换集显验证，代码侧已修动态态合法性。
- 僵尸 GUIWorkbench 进程（强杀残留）会持有 GPU device → 必要时 taskkill / Stop-Process。
- `xmake run ... -- --flags` 的 `--` 会被 cxxopts 当选项结束符吞掉后续参数 → 直接传参不带 `--`。

## 验证命令速查

```bash
# 构建
xmake b GUIWorkbench

# headless 跑 dock 场景（PowerShell）
xmake run GUIWorkbench --start-page Dock --scenario Example/GUIWorkbench/Scenarios/dock.jsonl --headless --scenario-dump-dir build/dock_dump

# model / closure tests（落地 DockNode 后）
xmake b ya-gui-closure-test && xmake r ya-gui-closure-test

# 窗口模式真机截图
xmake run GUIWorkbench --start-page Dock
```

## 每一阶段的最低 checkpoint

- **0**：cancel/no-target/drop 三种 finish result、同一 target 内 preview move、P7 baseline dump。
- **1**：model invariant、merge/cardinal/collapse、ratio/min extent 测试，不创建窗口。
- **2**：三 leaf projection、tab select、split divider 写回、resize content rect。
- **3**：center + 4 cardinal 的 preview/drop/cancel/resize-between-events。
- **4**：4 corner × {enabled, extent-too-small disabled}，exact tree shape + placeholder。
- **5**：tear-off/re-dock/focus/modal/z-order/normal quit。
- **6**：public-header audit、DTO/coordinator compile-only port、全量 scenario + GPU parity。
