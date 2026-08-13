# GUI 架构收敛 TODO

> 更新时间：2026-08-13
> 作用：把 `plan.md` 里的阶段目标展开成当前可执行事项。这个文件优先服务日常推进，要求持续收口，不保留已经失效的行动项。

## 当前优先级

当前默认只允许同时推进一个 phase 的一个最小切片。若工作区中已经存在未完成切片，先收口再开新项。

当前激活切片：`Phase 0 / render2d_flush_scissor_diagnostics`

## P0 — Phase 0 渲染正确性基线

### 进行中

- [x] 固定一个最小 workbench 页面：静态文本 + 单个 button + 单个 image 占位，作为 render correctness 页面
- [~] 补最小渲染 debug overlay：已落地坐标轴 / render target 标记 / clip bounds；batch 边界仍未接入
- [x] 建立单帧渲染 smoke 入口，支持 agent 无手操验证一帧结果
- [x] 固定 ScrollSplit snapshot 取证：CPU 侧标题/副标题/divider/左右 pane clip 坐标已落盘，可作为 Render2D 排障基线
- [x] 修复 `--start-page=Editor` 诊断入口顺序问题，确保内建 Editor 页可直接起跑
- [~] 接通 Render2D session/clip/flush 诊断日志，准备对 ScrollSplit 做 scissor / batch / compose 分层取证
- [x] host pointer bootstrap：静止鼠标首击一致性由 SdlEventSource 正式路径保证（mouse focus + SDL_GetMouseState + enter/leave）
- [x] scenario resize host path：start-page + resize + checkpoint + capture 已跑通
- [x] 修复 presentation readback image usage：swapchain bEnableTransferSrc + import usage 加 TransferSrc，resize scenario 0 VUID

### 待做

- [ ] 压稳 resize：窗口缩放后不崩溃、不闪屏、不丢 RT 绑定
- [ ] 压稳坐标：确认 GUI 逻辑坐标始终左上角原点，底层 reverse viewport 不外泄
- [ ] 压稳文本：文字方向、baseline、clip/scissor 正确
- [ ] 压稳 Render2D 多批次 flush：后续内容不被前序 batch/RT 切换污染
- [ ] 用 GPU shot + flush/scissor 日志判定问题归属：scissor / batch cursor / compose / resource lifetime / present
- [ ] 建立 windowed/headless/offscreen 三路径同帧一致性检查
- [~] 确认 Vulkan / MoltenVK validation 零错误：capture/readback 路径已 0 VUID；全应用门禁待补

### 完成标准

- [ ] 最小页面在 resize 后稳定
- [ ] 至少一张 overlay 截图或 dump 证明坐标/clip/batch 正确（当前已具备坐标/clip 观测面，batch 边界仍待补齐）
- [ ] 至少一条自动化 smoke 可重复运行

## P1 — Phase A 主链路与 owner 收口

### 待做

- [ ] 产出 `AppKernel -> GUIApp -> GUIWindowHost -> WidgetTree` 职责图
- [ ] 盘点现有 `App / Host / GUIAppHost / Surface / Panel` 类型，整理成保留/改名/过渡/删除四类表
- [ ] 明确 app/window/tree 各自拥有的状态：focus、hover、capture、modal、popup、drag overlay
- [ ] 明确 automation 基底的最终挂载层级，以及 GUI/game/runtime editor 的扩展点
- [ ] 补一页 owner checklist：谁创建、谁销毁、谁 restore、谁 dump

### 完成标准

- [ ] 文档能直接回答“哪个是真正主循环、哪个拥有窗口、哪个拥有 tree”
- [ ] 同名/近名类型已给出明确去向
- [ ] automation 不再被描述成多个平行入口

## P2 — Phase B Layout / Slot 内核

### 待做

- [ ] 定义 `UILayout` 基类：measure/arrange/invalidation 合同
- [ ] 定义 `UISlot` 基类：parent-owned edge object 合同
- [ ] 定义 `UIBoxLayout / UIBoxSlot` 的最小 public API
- [ ] 规定 child 获取 slot、parent 获取 layout 的访问面
- [ ] 规定 reparent / detach / destroy 时 slot 生命周期
- [ ] 规定 slot 改动对应的 invalidation 粒度
- [ ] 选择一个最简单 vertical box 页面，迁到新 layout/slot
- [ ] 选择一个 nested box 页面，验证横竖组合与 reparent

### 完成标准

- [ ] 至少一个页面完全不依赖 UIContainer 旧 box 字段
- [ ] slot 不在 detach/reparent 后悬空
- [ ] layout/slot dump 能看到 box 布局结果

## P3 — Phase C 事件路径与状态模型

### 待做

- [ ] 定义 pointer state / pointer path / focus path 数据结构
- [ ] 拆分 hit test 求 target/path 与 route phase 执行
- [ ] 定义 preview/tunnel -> target -> bubble 三阶段合同
- [ ] 把 capture / modal / popup / drag session 归类为 route policy
- [ ] 收敛“当前鼠标位置”为 tree/window 级持续状态，不再作为业务层接口参数反复传递
- [ ] 为 menu/button/scroll/split/dragdrop 制定最小事件语义表
- [ ] 增加 route dump、hover/focus overlay、自动化路径断言

### 完成标准

- [ ] 一次 mouse move / press / release 可以打印明确 path
- [ ] menubar 横向 hover 切换有结构证据
- [ ] button 状态回落有自动化断言

## P4 — Phase D Workbench 与测试迁移

### 待做

- [ ] 明确 Workbench 页面分层：基础交互页、布局页、树/属性页、overlay/debug 页
- [ ] 先迁 menu / popup / button 三类页面
- [ ] 再迁 scroll / split 页面
- [ ] 最后迁 tree / inspector 页面
- [ ] 每个 feature page 至少补一条 scenario + 一张 golden

### 完成标准

- [ ] Workbench 成为 feature gallery + regression app
- [ ] demo 页不再回流到 Framework 代码中
- [ ] 高频体验缺陷具备结构断言与视觉基线

## P5 — Phase E/F specialized layout 与多窗口留口

### 待做

- [ ] 收 split 到 `UISplitLayout`
- [ ] 收 scroll 到 `UIScrollLayout`
- [ ] 明确单 child 内容布局抽象
- [ ] 先按“组合控件优先、专用 layout 兜底”评估 menu/tree/inspector
- [ ] 文档化多窗口 owner 边界
- [ ] 文档化 cross-window dragdrop 的 source/target/commit/cancel 语义
- [ ] 文档化 per-window vs whole-app modal 语义
- [ ] 文档化 active window / activation order / focus path 切换规则

### 完成标准

- [ ] 不改 layout 内核就能解释未来多窗口与 docking
- [ ] dragdrop / modal / focus 默认语义已经写清

## 暂不推进

- [ ] `.yaui / UIDocument` 作为当前主 authoring 入口
- [ ] 完整 docking 系统
- [ ] editor shell 全量替换
- [ ] 主题系统大扩张
- [ ] RHI 全面重写
