---
name: material-flow
description: YA Engine ECS 到材质系统到渲染管线的数据流整理与重构。适用于材质 authoring 数据、resolve 生命周期、editor 属性上报、shared material、runtime material cache、forward/deferred 上传路径、唯一数据源问题。
---

## 适用场景

- 用户要求梳理 ECS -> 材质 -> 渲染管线的数据流
- 出现材质参数、TextureSlot、descriptor、dirty/version、shared material 串扰问题
- editor 修改属性后运行时材质或渲染结果不同步
- 需要判断数据应留在 Component、runtime Material、System 还是 Pipeline

## 当前架构结论

1. `PhongMaterialComponent` 持有唯一 authoring 数据源。
2. `PhongMaterial` 只做 runtime render-ready cache，不是 editor 真相来源。
3. `TextureSlot` 持有 authoring 语义：path、enable、uv。
4. `TextureView` 只表示运行时 resolved view，不承载 authoring 真相。
5. `EMaterialResolveState` / `EAssetResolveState` 只表达 resolve 生命周期，不表达 param dirty 或 resource upload dirty。
6. 材质上传脏位采用 `paramVersion/resourceVersion` + 每个 consumer 的 last uploaded version，不再依赖全局 dirty 被某一路消费后清空。

## 系统边界

### 1. ModelInstantiationSystem

- 负责 `ModelComponent` 展开成子节点 / 子实体
- 负责建立 mesh/material child entity 拓扑
- 可创建 shared runtime materials
- 不负责为已有组件做一般资源 resolve

### 2. ResourceResolveSystem

- 只负责已有组件的运行时资源解析
- 不创建 scene topology
- 当前职责包括：Mesh / PhongMaterial / UI / Billboard resolve

### 3. PhongMaterialComponent

- 持有 authoring params（`ambient/diffuse/specular/shininess`）
- 持有每个 `TextureSlot`
- 接收 editor 属性修改上报
- 把 authoring 数据同步到 runtime material

### 4. Forward / Deferred Pipeline

- 只读取 runtime `PhongMaterial`
- 不直接回读 `PhongMaterialComponent` 内部 slot / params

## Editor 修改链路

目标：editor 只上报修改，不直接承担业务同步。

1. `DetailsView` 通过 `RenderContext` 收集修改路径。
2. 修改路径必须使用稳定 field name，而不是 prettyName。
3. `PhongMaterialComponent::onEditorPropertiesChanged()` 负责把路径分类为：
   - 参数修改
   - texture slot 资源修改
   - texture slot 非资源修改（如 enable/uv）
4. 分类后由组件内部执行：
   - `invalidate()`
   - `syncParamsToMaterial()`
   - `syncTextureSlot()` / `syncTextureSlots()`

## 路径规则

1. UI 显示继续使用 prettyName。
2. 修改追踪路径只使用原始 field name。
3. 例如：
   - `_params.shininess`
   - `_diffuseSlot.bEnable`
   - `_reflectionSlot.textureRef`
4. 禁止再写同时兼容 prettyName 和 field name 的字符串猜测逻辑。

## 单一事实源规则

1. editor 可编辑数据只能留在 Component authoring 层。
2. runtime material 中的参数、TextureView、descriptor 句柄都不是 editor 真相。
3. shader 上传数据只能从 runtime material 读取。
4. 一旦发现 pipeline 直接读取 component slot/params，优先回退并恢复到 runtime material 读取。

## Shared Material 规则

1. 当前 shared material 仍可存在，用于模型导入后的同材质共享。
2. 未完成正式 material/material-instance 设计前，不要继续扩展临时 clone-on-write 逻辑。
3. 若局部编辑语义不明确，优先保持共享行为稳定，而不是偷偷实例化。

## 常见错误模式

1. `prettyName` 与 field name 混用，导致 editor 修改路径和业务分类器对不上。
2. 把 runtime `ParamUBO` 直接暴露给 editor，导致 runtime 字段被当成 authoring 数据编辑。
3. `TextureSlot` 的 enable/uv 与 runtime `textureParams` 双向写入，破坏单一事实源。
4. 某个 render pass 清掉全局 dirty，导致其他 consumer 拿到旧 descriptor 或白纹理。
5. 模型展开阶段直接 `setTextureView()`，绕开正常 slot resolve -> component sync 链路。

## 推荐排查顺序

1. editor 修改是否真的生成了稳定 raw field path
2. component 是否识别该路径并进入正确分类分支
3. runtime material 的 param/resource version 是否递增
4. MaterialDescPool 对应 consumer 是否检测到版本变化并重新上传
5. shader 是否实际读取了对应 runtime param / texture binding

## 退出条件

- 能明确回答某份数据属于 Component、System、runtime Material、还是 Pipeline
- editor 修改路径、resolve、runtime sync、GPU 上传四段链路可以一条线讲清
- 不再需要通过 prettyName/field name 双名兼容或局部补丁维持行为