# FrameGraph 之后的独立路线图

本文件承接 FG-906，只描述后续任务，不属于当前 FrameGraph 迁移的实现范围。

## 1. DrawList / DrawPacket

当前事实：

- `RenderFrameExtractor` 已产生按材质类型分桶的 `RenderDrawItem`。
- Deferred、Forward、Shadow 各自仍直接消费 `std::vector<RenderDrawItem>`。
- 当前没有统一的 visibility、LOD、sorting、instance grouping 或 backend-neutral
  `DrawPacket`。

建议顺序：

1. 定义只读 `DrawCandidateView`，先包装现有 `RenderDrawItem`，不改变 shader 或
   resource ownership。
2. 定义 `DrawPacket`，明确 mesh/material binding、instance range、sort key 和
   skinning flag；shader-facing command 仍以生成头为事实源。
3. 为 Deferred/Forward 各选一个 consumer，建立 deterministic sort/group 测试。
4. 再把 Shadow indirect bucket 接入 packet producer，避免先重写三条 draw loop。

停止线：

- 不在 DrawList 任务中同时改 RenderGraph resource registry、材质 authoring 或
  shader layout。
- 没有第二个真实 consumer 前，不抽 public 通用 submission API。

## 2. generated ShaderParameterBlock

当前事实：

- Slang/GLSL 生成头已经是 shader-facing C++ layout 的单一事实源。
- `RGPassBindingContext` 已能按 graph handle resolve 资源并更新现有 descriptor。
- 仍没有跨 Deferred/Forward 的 generated parameter block/binder。

建议顺序：

1. 盘点生成头中 descriptor set、binding、push constant 的命名和类型元数据缺口。
2. 扩展生成脚本或 shader metadata，生成 backend-agnostic binding description。
3. 先实现内部 binder adapter，分别验证 Deferred GBuffer/Light 与 Forward PBR
   两个真实 consumer。
4. 只有两个 consumer 都稳定后，才考虑命名为 `ShaderParameterBlock` 的公共 API。

停止线：

- 不手写 UBO/SSBO/push constant mirror struct。
- 不让 generated binder 反向持有 graph resource owner 或执行 graph。

## 3. Editor Render Extension

当前事实：

- Editor 通过 `AppRenderServices` 注入 frame-state 与 presentation callback。
- presentation capture 已经是独立 graph 的 declared copy/readback pass。
- extension callback 仍可直接接触 `ICommandBuffer`，同步与资源声明边界尚未统一。

建议顺序：

1. 定义 editor extension 的 immutable frame input 和 capability descriptor。
2. 将 extension 输出改为“声明 pass + typed resource handles”，先覆盖一个
   viewport overlay consumer。
3. 把 command buffer、descriptor 和 resource lifetime 留在 runtime-owned
   binding context 内。
4. 再评估是否需要 public extension registry、ordering policy 和 editor-only
   diagnostics。

停止线：

- 不把 acquire/submit/present 交给 extension。
- 不允许 extension 在 execute callback 查询 active scene、App 或 registry。
- 不在没有第二个真实 extension consumer 前抽象完整插件协议。

## 依赖与验收

推荐依赖：

```text
DrawCandidateView -> DrawPacket -> ShaderParameterBlock binder
                                  \-> Editor Render Extension adapter
```

每个后续任务必须单独提供：

- 至少一个真实 consumer；
- deterministic structure/contract test；
- `xmake b ya-engine` 与 `xmake b ya-editor`；
- 不依赖 GUI 的 graph/resource 验收；
- 若涉及视觉行为，再单独补 GUI automation baseline。
