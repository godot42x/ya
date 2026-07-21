# Vulkan Submit Lifecycle Debug

适用场景：

- 崩溃栈落在 `vkQueueSubmit`、MoltenVK encode、dynamic rendering、attachment 构造
- 怀疑是“record 时合法，submit 时失效”的 GPU 资源生命周期问题

## 排查顺序

1. 先确认崩溃发生在 record、submit 还是 fence wait 后。
2. 若栈里出现 `MVKAttachmentDescription`、`MVKRenderPass`、`beginRendering`、`vkQueueSubmit`：
   - 先查 color/depth attachment 的 `IImageView*` 是否仍然存活。
   - 先查该 view 是否在 record 后、submit 前被 `DeferredDeletionQueue`、临时 owner 或局部 `shared_ptr` 释放。
   - 先查 `RenderingInfo::ImageSpec` 是否同时携带 attachment owner token；manual dynamic rendering 路径不要只传裸指针。
   - 再查 attachment struct 里的指针是否指向栈对象或已 reset 容器。
   - 再查 imported `RenderImage`、`RenderGraphExecutor`、transient view 是否只活到局部 `execute()` 返回。
   - 再查 `RenderingInfo::ImageSpec.image` 与 `imageView->getImage()` 是否仍是同一个对象，subresource range 是否来自同一个 view。
3. 对异步加载或 offscreen 任务，短帧 smoke 不算验证；至少跑到会触发 resolve / preprocess / submit 的帧数。
4. 若日志不够，优先用 `lldb --batch ... -k 'bt' -k 'thread backtrace all'` 抓真实崩溃栈。
5. 跑大日志时先用 `rg` / `sed` / `tail` 过滤模块、关键词、时间段。

## 生命周期专项检查单

1. 非 owning 类型是否被误当成 owning 使用。
2. 录入 command buffer 的 attachment / descriptor / view 是否跨 submit 存活。
3. 若后端在 submit/encode 阶段消费 attachment，就把保活边界按 `vkQueueSubmit -> fence signal` 算。
4. `shared_ptr` 持有链是否真的覆盖到 `GpuCompleted` / fence signal。
5. 将 owning 改 non-owning 后，是否给每条调用链补了新的 keepalive owner。
6. `RenderGraph` imported 资源是否由外层 frame/job/submission 对象保活。
7. imported resource 若引用已有 subresource view，确认 graph compile 看到的 mip/layer/aspect range 与实际 view 一致。
8. 手写 `RenderingInfo::ImageSpec` 时优先走能携带 shared owner 的 helper。
