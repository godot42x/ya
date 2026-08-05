# RenderGraph Point Shadow Indirect Cull Regression

适用场景：

- 开启 `Point Shadow -> Indirect Draw + Indirect Cull` 后出现 viewport 间歇闪烁
- viewport 看起来卡在旧帧，但 gizmo / camera input 仍然更新
- 日志出现 `RenderGraph compile failed`，且 pass dump 指向 `Point Shadow Cull Upload`
- 容易与旧的 Vulkan image layout 问题混淆时

## 2026-08-04 回归结论

这次问题和更早那类 “image layout 不对” 不是同一类根因，实际拆成两层：

1. `RenderGraph` 资源契约不一致，导致 graph compile fail，整帧停在旧画面
2. point shadow indirect cull 的 buffer 数据流不干净，导致开启 cull 后容易出现 1-3 帧一次的间歇闪烁

### 层 1：为什么会出现“画面锁在一帧”

症状：

- viewport 像冻结在旧帧
- 但 gizmo / 相机输入还能更新
- 日志里会有 `RenderGraph compile failed`

根因：

- point shadow cull 重构成 upload / exec 双 buffer 后，底层 buffer create info 与 graph import usage 没有同时改对
- 典型例子：
  - 底层 `PointShadowCull.Frustums.Upload` buffer 已经带了 `TransferSrc`
  - 但 `RenderGraph::importBuffer()` 时仍声明成 `StorageBuffer`
- `RenderGraph` compile 校验看的是 graph 中的 usage contract，不是底层 VkBuffer 的真实 flags
- 因此 `Point Shadow Cull Upload` copy pass 被判定为 invalid usage
- graph compile 失败后，这一帧后续 deferred pass 都不会执行，于是视觉上像“卡在上一帧”

### 层 2：为什么会出现“隔 1-3 帧闪一次”

旧路径把同一批 `CpuToGpu` buffer 同时拿来做：

- CPU 写 frustum / draw command template / visible instance list
- GPU compute cull 写回
- GPU indirect draw 读取

这类 mixed-role host-visible buffer 在 Vulkan / MoltenVK 路径上很容易变成间歇性不稳定，尤其当：

- compute / indirect / host write 混在同一帧
- per-flight ring reuse 比较紧
- 某一条 barrier / usage / graph dependency 只差一点点就会开始“不是每帧都错”

更稳的模型是单向数据流：

- upload buffer：CPU only
- exec buffer：GPU copy / compute / indirect only
- graph 显式表达：`upload -> compute -> raster`

## 和旧 image layout 问题如何区分

如果是这类 regression，优先顺序应该是：

1. 先看是不是 `RenderGraph compile failed`
2. 再看是不是 resource usage / dependency contract 错
3. 再看 compute / indirect / upload buffer 数据流是否干净
4. 最后再回到 image layout / texture transition

不要一看到 viewport 不更新，就先默认是 image layout。

尤其当：

- gizmo / camera 输入还能动
- 画面停在“上一帧的完整结果”
- log 明确打印了 `RenderGraph compile failed`

这更像 graph 没跑完，而不是 image layout 导致采样异常。

## 未来避免方案

### A. 保持一份单向数据流，不复用“多职责 host-visible buffer”

对所有类似：

- host upload
- gpu compute writeback
- indirect / shader read

混在一起的路径，优先拆成：

- upload resource
- execution resource

不要为了省一个 buffer，把 CPU staging 与 GPU execution 角色揉在一起。

### B. 在 graph compile 阶段更早失败，而且报更具体

这类错误本来就应该在 graph compile 阶段被拦住；当前问题不是“没有拦住”，而是：

- compile 已经拦住了
- 但开发者容易只盯底层 VkBuffer usage，以为自己修过了
- 没意识到 graph import/create usage 也是另一层单独的 contract

建议后续增强 compile 期诊断：

1. 对 imported buffer / texture，在 `debugDump` 里同时打印：
   - graph-declared usage
   - imported backing resource usage
   - 哪个 pass 以什么 access 使用了它

2. 对 `InvalidUsage`，报错里直接指出：
   - “graph import usage missing TransferSrc/TransferDst”
   - 而不是只说 “unsupported usage flags”

3. 在 `importBuffer()` / `importTexture()` helper 层补更强的断言或辅助 API，减少手填 usage：
   - 例如 copy-source / copy-destination 专用 helper
   - 或者从 pass intent 反向推导 minimum required usage 后做一致性校验

4. 如果 imported resource 的 graph usage 是该类回归高发点，可以考虑在 compile 前做 dedicated validation pass：
   - imported backing usage 是否覆盖所有 pass-declared usage
   - initial/final state 与 usage 是否自洽

### C. 对“卡旧帧但交互还在动”的症状建立固定排查分流

后续再见到这类症状，优先分流：

1. 有没有 `RenderGraph compile failed`
2. 有没有 imported resource usage / dependency error
3. 当前帧是否根本没完成主 graph execute
4. 只有这些都排除后，再去查 image layout / transition

## 落地提醒

- 改 buffer/image create info 时，必须同步改 graph import/create usage
- 改 backend usage 但不改 graph contract，compile 仍然会失败
- 改 graph contract 但不改 backend usage，runtime validation / backend copy 仍然会失败
- 这两层必须一起对齐
