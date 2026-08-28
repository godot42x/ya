# Retained DetailsView：PropertyGraph / Projection / Binding 重构计划

## 目标

在不保留 ImGui 兼容设计的前提下，为 GameEditor 建立 retained DetailsView。反射自动 UI、手写语义 section 和 `UICompoundWidget` 必须共享同一套属性访问与编辑事务边界。

## 不采用的方案

- 不把 UE `IDetailCustomization` 直接照搬为唯一模型。
- 不让 `DetailsView` 持有按组件类型增长的手写黑名单。
- 不让自动 renderer 直接依赖 ECS/Scene/EditorLayer。
- 不把 ImGui `TypeRenderer` 搬进 WidgetTree。
- 不恢复 `UIDescription -> reconciler -> controller`。

## 目标分层

```text
SelectionSet
    ↓
PropertyGraph / PropertyBinding
    ↓
PropertyProjection
    ├─ Custom compound section
    └─ Auto property editor
    ↓
WidgetTree
```

### PropertyGraph / PropertyBinding

- 由反射 `Class` / `Property` 构建稳定属性路径。
- 支持单选、多选、mixed value、read-only、InstanceEditable、metadata。
- 提供统一 get/set、reset、dirty 和事务入口。
- 不依赖 ImGui、WidgetTree、具体 ECS 组件或编辑器控件。

### PropertyProjection

- 根据属性类型和 metadata 选择 editor kind。
- custom projection 可以 consume/hide/replace/insert 属性。
- 未消费属性必须自动落到 generic property row。
- 不维护“手写类型排除列表”。

### Custom section / UICompoundWidget

- 只表达跨字段布局和产品语义。
- 通过 PropertyBinding 读写，不直接操作 Entity/Component 裸指针。
- 只拥有局部 retained 子树和交互状态。

### Auto property editor

- 负责 scalar、enum、string、vector/color、asset、nested struct、container 的通用编辑。
- 自动 editor 不知道组件归属和 editor host 生命周期。

## Custom 与 Auto 的优先级

```text
显式隐藏 → 类型级 custom projection → 字段级 custom projection → auto renderer → 只读摘要
```

custom projection 只消费它声明的属性路径；同一组件中未消费的新反射字段自动出现。

## 实施顺序

1. `PropertyHandle` 收口为中性反射 binding，先覆盖 `glm::vec3` 和 single/multi instance 语义。
2. `EditorTransformSection` 改用 binding，并补 mixed/read-only/写回 contract。
3. 新增 `PropertyGraph`，从 `Class::propertyOrder` 生成字段节点和 metadata。
4. 新增 retained `UIPropertyRow` / `UIAutoPropertyEditor`，先支持 float/bool/string/vec3。
5. 新增 projection registry；Transform 作为第一个 custom projection，移除 handwritten type blacklist。
6. 迁移 Material、Light、Skybox 等 editor-specific section；通用字段走 auto。
7. 删除 GameEditor 的 ImGui DetailsView / TypeRenderer 路径。

## 每个 checkpoint 的硬验收

- 有真实 consumer，不提交纯抽象或 placeholder。
- contract test 先于实现，覆盖 identity、读写、多选或 mixed 语义。
- 代码、测试和本计划/进度在同一个 feature checkpoint 提交。
- 未完成能力必须明确记录，不得宣称完成整条 DetailsView 迁移。
