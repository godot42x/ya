# Session Checklist — dockspace-node-tree

## 开工步骤

1. 读 `.agent/plan/dockspace-node-tree/plan.md` 确认当前阶段与目标。
2. 读 `progress.md` 确认上一轮完成内容与剩余风险。
3. 确认当前 main 上 P7 三栏实现可用：`xmake b GUIWorkbench` 通过。
4. 环境确认：关闭验证层干扰（如 86% 显存下 vkCreateGraphicsPipelines 崩溃，属环境问题非代码 bug）；优先用 headless + `--scenario-dump-dir` 验证。

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
   - 树结构变更后显式 markLayoutDirty()
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

# dump 结构检查
python3 -c "import json; d=json.load(open('build/dock_dump/dock_initial.json')); ..."

# 窗口模式真机截图
xmake run GUIWorkbench --start-page Dock
```
