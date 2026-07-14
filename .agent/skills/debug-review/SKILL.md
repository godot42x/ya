---
name: ya-debug-review
description: YA Engine 崩溃排查、变更自检与提交前 review 清单。
---

## 适用场景

- 运行时崩溃、访问越界、空指针、设备丢失
- 提交前自检、代码 review、diff 复盘
- 需要判断某次改动是否越界、是否引入了时序 / 生命周期风险

## 崩溃排查优先级

1. `0xC0000005`：先查空指针、悬空指针、失效引用。
2. 初始化顺序：确认 `RenderRuntime`、`IRender`、system、component resolve 顺序正确。
3. 资源生命周期：检查 `shared_ptr` / runtime state 持有链与释放时机。
4. Vulkan 问题：优先检查 frame 内资源重建、layout transition、descriptor / imageView 是否失效。
5. 若栈落在 `vkQueueSubmit`、MoltenVK encode、dynamic rendering、render pass / attachment 构造：优先怀疑“record 时合法、submit 时失效”的生命周期问题，而不是先怀疑 shader 或业务逻辑。

## 日志与观测

优先使用：
- `YA_CORE_TRACE`
- `YA_CORE_DEBUG`
- `YA_CORE_INFO`
- `YA_CORE_WARN`
- `YA_CORE_ERROR`
- `YA_CORE_ASSERT`

不要用 `std::cout` / `printf` 临时打日志。

高优先级观测点：
- job / future / async resolve 的 phase 变迁
- queue submit 前后的 resource label、image view label、render target label
- `DeferredDeletionQueue` flush 时机
- imported image / imageView 的创建与 retire 路径

冒烟默认先降噪：
- 优先用 `--log-level=warn --log-detail-level=error`，除非本次问题明确需要 `info/debug/trace`
- 如果需要保留更多上下文，先提到 `info`，不要直接放开 `trace`
- 读日志和读代码时先过滤到目标模块、目标帧段、目标关键字，再决定是否扩大范围

## Vulkan / MoltenVK 提交期崩溃排查顺序

1. 先确认崩溃发生在 record、submit、还是 fence wait 后。
2. 若栈里出现 `MVKAttachmentDescription`、`MVKRenderPass`、`beginRendering`、`vkQueueSubmit`：
   - 先查 color/depth attachment 的 `IImageView*` 是否仍然存活。
   - 先查该 view 是不是在 record 后、submit 前就被 `DeferredDeletionQueue` / 临时 owner / 局部 `shared_ptr` 释放了。
   - 再查 `RenderingInfo` / attachment struct 里的指针是否指向栈对象或已 reset 容器。
   - 再查 imported `RenderImage` / `RenderGraphExecutor` / transient view 是否只活到局部 `execute()` 返回。
   - 再查 `RenderingInfo::ImageSpec.image` 与 `imageView->getImage()` 是否还是同一个对象，subresource range 是否来自同一个 view，而不是手抄第二份元数据。
3. 对异步加载或 offscreen 任务，短帧 smoke 不算验证；至少跑到会触发 resolve / preprocess / submit 的帧数。
4. 若日志看不出原因，优先用 `lldb --batch ... -k 'bt' -k 'thread backtrace all'` 抓真实崩溃栈，而不是只看引擎日志。
5. Codex/agent 跑大体量日志文件时，先用 `rg` / `sed` / `tail` 按模块、关键词、时间段过滤，避免整文件直读。

## 生命周期专项检查单

1. 非 owning 类型是否被误当成 owning 使用。
2. 录入 command buffer 的 attachment / descriptor / view 是否跨 submit 存活。
3. 若后端在 submit/encode 阶段消费 attachment（MoltenVK 常见），就把保活边界按 `vkQueueSubmit -> fence signal` 算，不按 `cmdBuf->end()` 算。
4. `shared_ptr` 持有链是否真的覆盖到 `GpuCompleted` / fence signal，而不是只覆盖到 `Recorded`。
5. 将 owning 改 non-owning 后，是否给每条调用链补了新的 keepalive owner。
6. `RenderGraph` imported 资源是否由外层 frame/job/submission 对象保活。
7. imported resource 若引用的是已有 subresource view，确认 graph compile 看到的 mip/layer/aspect range 与实际 view 一致；优先检查 view 自身是否携带 range 元数据，避免 helper/调用点又手写出第二份不一致的 range。

## 相关 skills

- `ya-build`：先确认构建、测试、shader 生成入口是否正确
- `vscode`：问题只在 VS Code 调试链路复现时一起看
- `render-arch`：Vulkan / RenderRuntime / layout 问题时一起看
- `resource-system`：资源生命周期、descriptor、resolve 问题时一起看
- `material-flow`：材质串扰、authoring/runtime 不一致时一起看
- `cpp-style`：review 时收敛所有权、最小改动和抽象层级时一起看

## Review 检查单

1. `git diff` 只包含当前任务需要的最小改动。
2. 不直接编辑生成文件；生成结果有问题时回到脚本或 xmake 规则修。
3. 不引入平行抽象或无关重构。
4. 对状态流、resolve 流程、渲染编排，优先保持清晰分支，不做提前抽象。
5. 临时调试代码、一次性文件、无效分支在提交前清理掉。
6. 若改到 Vulkan 资源，确认没有在 frame recording 中途重建正在使用的对象。
7. 若改到 image view、descriptor、render graph import、offscreen job，确认保活边界覆盖到 queue submit / GPU completion。

## 常见高风险点

- component 与 runtime cache 双写，导致单一事实源失效
- descriptor / imageView 更新了部分 consumer，另一条管线仍拿旧资源
- 场景拓扑创建与普通 resolve 逻辑混在一起
- editor pretty name / raw field path 混用，导致属性修改链路断裂

## 退出条件

- 崩溃或异常路径已定位到明确模块
- 改动边界清晰，可解释、可回归
- review 后没有遗留生成文件误改、临时代码或明显越界修改
