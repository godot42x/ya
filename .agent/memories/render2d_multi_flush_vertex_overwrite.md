# Render2D 多批次 flush 顶点缓冲覆盖：GUI 只渲染最后一个批次

> 2026-08-12，GUIWorkbench 渲染异常排查时定位；为历史遗留 bug（非本次重构引入，
> 用 stash 对比 HEAD 基线复现相同症状）。

## 现象

- GUIWorkbench 窗口只剩右侧 inspector 面板（按钮 + 文字）和底部状态文字；
  左侧 toolbar、ITEMS 列表、canvas、选择高亮块、所有 panel 背景全部缺失
  （"empty dark space"）。
- 布局 / snapshot 完全正确：`GUIAppHost first snapshot: 39 draw items`，
  各 item 位置与 clip 标志正确；问题在 GPU 绘制阶段。
- 用临时 swapchain readback 把真实渲染帧 dump 成 PPM 逐像素分析确认：
  flush 序列里**第一、二次 flush 的顶点内容完全没上屏**，只有最后一次 flush
  （inspector 按钮/文字 + 状态条）可见。

## 根因（两个叠加 bug，均在 Render2D）

1. **主因：host-visible 顶点缓冲被后续 flush 覆盖**

   `FQuadRender` 每个 (pass-domain × flight) 只持有一个 `CpuToGpu` 顶点缓冲，
   每次 `flush()` 后把写入指针重置回偏移 0。GUI compose pass 每帧会 flush 多次
   （每个带 clip 的 widget 一次：滚动列表行、name field 文本等）。

   - CPU 在录制命令时依次写入各批次顶点（都从偏移 0 开始，互相覆盖）；
   - GPU 只在整条 command buffer 录制并 submit 之后才读取该缓冲；
   - 于是所有 draw 都读到**最后一帧 flush 的数据**——之前 flush 的内容
     （toolbar、列表、canvas、高亮、标题）被静默丢弃。
   - 关键证据：临时给 `flush()` 打日志，帧内 flush 序列为
     `verts=148(clip1)` → `4` → `16` → `24` → `52` → `112(clip2)` → `16` → `596`，
     屏幕上只出现最后 `596` 对应内容；关闭 clip scissor 后症状不变，
     排除 scissor 是主因。

2. **次因：clip 与 flush 的先后顺序颠倒**

   - `pushClipRect` 先把新 clip 压栈再 flush → 新 clip 之外的待绘制内容被
     新 scissor 裁掉（flush 时 `clipStack.back()` 已是新 clip）。
   - `popClipRect` 先出栈再 flush → clip 内的内容逃出裁剪。
   - 修复后若只有该 bug，内容会"画错位置"而非消失，所以是叠加因素。

## 修复（提交 dbe6d5a3 之前的 GUI Workbench 重构提交）

- 顶点缓冲改为**批内游标模型**：每次 flush 写入缓冲的**不同区域**
  （游标只前进不回头），`drawIndexed` / `draw` 用对应的
  `vertexOffset` / `firstVertex` 定位批次；缓冲容量 = `MaxVertexCount * kFrameFlushSlots`
  （`kFrameFlushSlots = 4`），超限 `YA_CORE_ASSERT`（fail loudly，不再静默丢内容）。
  `FQuadRender`（screen + world）与 `FLineRender` 一并修复。
- `pushClipRect` / `popClipRect` 改为**先 flush 再改栈**：push 在 clip 变化时用
  旧 scissor 冲掉待绘制内容，pop 用内层 scissor 冲掉 clip 内内容。

## 验证

- 临时 readback 像素分析：修复后 toolbar、列表 + 选中行、canvas + 蓝色高亮、
  inspector 标题等全部正确渲染。
- `GUIWorkbench --smoke-actions` → PASS；`ya-gui-closure-test`（68，含 clip 测试）、
  `ya-gui-widgets-test`（62）、`ya-gui-workbench-workspace-test`（6）全过。
- 无 Vulkan validation 报错；GUIFrameworkSmoke / HelloMaterial 构建运行正常。

## 预防

1. **单帧多 flush 时，host-visible 顶点缓冲绝不能回绕/复用偏移 0**：
   CPU 录制期写入的数据要活到 GPU 执行完，必须每批次独立区域 +
   `vertexOffset` 定位（规则同 AGENTS.md "命令录制期引用的资源必须活到
   queue submit 完成" 的顶点侧版本）。
2. 排查"UI 只显示部分内容"时先确认 flush 次数与批次归属：
   `flush()` 打顶点数 + 当前 clip + 版本号日志，配合 swapchain readback
   dump 像素，比截图更精确。
3. clip 栈改动与 flush 的顺序是强约定：**先 flush 再改栈**（push/pop 都是），
   否则 pending 批次会使用错误 scissor。
