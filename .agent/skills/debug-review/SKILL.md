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
   - 先查 `RenderingInfo::ImageSpec` 是否同时携带了 attachment owner token（shared image / imageView / retained resources）；manual dynamic rendering 路径不要只传裸指针。
   - 再查 `RenderingInfo` / attachment struct 里的指针是否指向栈对象或已 reset 容器。
   - 再查 imported `RenderImage` / `RenderGraphExecutor` / transient view 是否只活到局部 `execute()` 返回。
   - 再查 `RenderingInfo::ImageSpec.image` 与 `imageView->getImage()` 是否还是同一个对象，subresource range 是否来自同一个 view，而不是手抄第二份元数据。
   - 若问题出在 shadow / cubemap / layered depth，额外检查“本帧实际写入的 layer 集”和“descriptor 可能采样的 layer 集”是否一致；只渲染了部分 layer、却把更大 layer 范围按 sampled descriptor 暴露出去时，未写 layer 很容易仍停留在 `Undefined`。
3. 对异步加载或 offscreen 任务，短帧 smoke 不算验证；至少跑到会触发 resolve / preprocess / submit 的帧数。
4. 若日志看不出原因，优先用 `lldb --batch ... -k 'bt' -k 'thread backtrace all'` 抓真实崩溃栈，而不是只看引擎日志。
5. Codex/agent 跑大体量日志文件时，先用 `rg` / `sed` / `tail` 按模块、关键词、时间段过滤，避免整文件直读。

## 渲染视觉回归排查：固定场景 + 固定机位

适用于：
- IBL / skybox / environment prefilter / irradiance / BRDF LUT 一类视觉回归
- “资源看起来 ready 了，但画面不对”
- 想区分“真正没修好”与“验证路径本身不可靠”

优先原则：
1. 先固定场景、固定观察物、固定机位，再谈 diff。
2. 对环境反射类问题，优先选能稳定暴露 skybox 倒影的 PBR 物体；不要先拿 Phong 样例代替。
3. 若自动化截图与人工编辑器观察冲突，先把“验证口径是否可信”当成一等问题排。

### 先固定四件事

1. 场景：明确是哪一个 scene asset，不要只说“HelloMaterial 那个场景”。
2. 观察物：明确是哪一个实体/材质，不要混用 PBR 球体和 Phong 立方体。
3. 相机：明确位置和旋转；环境反射问题里，机位本身就是诊断输入，不是附带信息。
4. 截图时机：明确抓图帧号；晚帧问题不能只写 `exit-after-frame`。

缺任何一项，都容易把“验证结果不一致”误判成“修复无效”。

### 本仓库 IBL 回归的已验证观察点

- target：`HelloMaterial`
- 目标场景：`Example/HelloMaterial/Content/Scenes/HelloMaterial.scene.json`
- 目标物体：`PBR_Sphere_5_0`
- 用它观察 PBR 球体表面的天空盒/环境反射是否存在
- 不要把 `Cube_2_0` 这类 Phong 物体当成主观察点；它不适合拿来判断这次 IBL 反射是否恢复

已验证过可复用的编辑器相机参数：

```bash
--editor-camera-pos=12,12,10
--editor-camera-rot=-9,-39,0
```

这组机位用于把 PBR 球体和环境反射区稳定放进视野。类似回归里，优先复用已有“能看到症状”的机位，不要每次重新找角度。

建议把这组信息当作一个“观察基线”整体复用：

```text
scene   = Example/HelloMaterial/Content/Scenes/HelloMaterial.scene.json
target  = PBR_Sphere_5_0
camera  = pos(12,12,10) rot(-9,-39,0)
signal  = 球体表面是否能看到天空盒/环境倒影
```

### 推荐验证顺序

1. 先用低噪音 smoke 跑目标场景。
2. 再用固定机位观察目标物体。
3. 先做人工编辑器冒烟，确认肉眼是否能看到目标症状。
4. 再决定自动化截图是否能作为同一问题的可信证据。
5. 若要和 `origin/main` 对比，必须保持同一 scene / 同一 target / 同一 camera / 同一 frame gate。

推荐命令模板：

```bash
make r t=HelloMaterial r_args="--exit-after-frame=1500 --screenshot-frame=1500 --screenshot-target=editor --editor-camera-pos=12,12,10 --editor-camera-rot=-9,-39,0 --log-level=warn --log-detail-level=error"
```

若需要落盘截图，再补：

```bash
--screenshot=/tmp/ibl-check.png
```

若要和 `origin/main` 做同口径对照，直接复用同一套参数，只切换代码版本；不要一边换提交，一边换观察点或机位。

### 帧数规则

1. 对 environment preprocess / offscreen resolve / 异步资源完成有依赖的问题，短帧 smoke 不算验证通过。
2. 这类问题至少跑到“确实完成环境预处理”的帧数，再下结论。
3. 本次 IBL 反射回归里，`1500` 帧量级比 `300` 帧更接近真实可见结果；不要只拿早期帧截图判死刑。
4. `--exit-after-frame=1500` 只控制退出时机，不会自动把截图推迟到 1500 帧；若要在晚帧抓图，必须同步设置 `--screenshot-frame=1500` 或 automation 配置里的 `screenshot.frame`。
5. 一旦显式设置 `screenshot.frame`，它会优先于默认 warmup 语义；不要再把“1500 帧 gate”理解成“1500 + warmup 再截图”。

### 常见误判

1. 把 Phong 物体当成 PBR 环境反射的主观察点，导致判断失焦。
2. 自动化截图路径和人工编辑器看到的状态不一致，却继续把自动化结果当唯一真相。
3. 只看“资源 ready / descriptor 已更新”的日志，就默认最终视觉一定正确。
4. 看到画面仍不对，就直接怀疑 light pass；环境贴图生成内容、导入视图元数据、keepalive 链都要一起看。

### 最小复用模板

遇到同类视觉回归，先把下面四行补全，再开始跑：

```text
scene:
target entity/material:
camera pos/rot:
expected visual signal:
```

若这四项写不清，就先不要急着比较截图或下结论。

### 这类问题的高价值对照项

1. `origin/main` 是否在同一场景、同一机位、同一观察物上能稳定看到症状差异。
2. PBR 球体是否真的是 `PBRMaterialComponent`，不要靠肉眼猜材质类型。
3. skybox cubemap、irradiance、prefilter、BRDF LUT 是“未生成”，还是“生成了但内容错/生命周期错”。
4. 若工作区人工冒烟已恢复，而自动化截图仍显示失败，优先调查截图路径、触发时机、目标 source image，而不是立刻推翻修复本身。

## 生命周期专项检查单

1. 非 owning 类型是否被误当成 owning 使用。
2. 录入 command buffer 的 attachment / descriptor / view 是否跨 submit 存活。
3. 若后端在 submit/encode 阶段消费 attachment（MoltenVK 常见），就把保活边界按 `vkQueueSubmit -> fence signal` 算，不按 `cmdBuf->end()` 算。
4. `shared_ptr` 持有链是否真的覆盖到 `GpuCompleted` / fence signal，而不是只覆盖到 `Recorded`。
5. 将 owning 改 non-owning 后，是否给每条调用链补了新的 keepalive owner。
6. `RenderGraph` imported 资源是否由外层 frame/job/submission 对象保活。
7. imported resource 若引用的是已有 subresource view，确认 graph compile 看到的 mip/layer/aspect range 与实际 view 一致；优先检查 view 自身是否携带 range 元数据，避免 helper/调用点又手写出第二份不一致的 range。
8. 手写 `RenderingInfo::ImageSpec` 时，优先走能携带 shared owner 的 helper；不要在 manual attachment path 上只保留 `IImage* / IImageView*` 裸指针。

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
