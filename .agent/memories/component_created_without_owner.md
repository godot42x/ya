# Deserialized components had no owner, so a loaded camera rendered from the origin

## 症状

加载 `Example/HelloMaterial` 后选中 `Camera`（entity 55）：机身材质在 authored 位置，
但预览内容和相机视锥线框一起停在世界原点附近（默认 orbit 视角，
`lookAt(vec3(0,0,_distance) + _focusPoint, _focusPoint, up)`）。三样东西互相不一致，
而只有 mesh 是对的。运行时新建的相机（脚本 / 预设）正常，只有从 scene.json 载入的坏。

## 根因

`IComponent::_owner` 只在两处被写：

1. `Entity::addComponent<T>()`（typed funnel）在 `emplace` 之后手动 `setOwner(this)`；
2. `Scene::clone` / `duplicateNode` 复制完 `setOwner`。

而 `SceneSerializer::deserializeEntity` 走的是**另一条** funnel：
`ECSRegistry::get().addComponent(FName(typeName), registry, handle)` —— 按名字的类型擦除创建，
它从不知道 `Entity*`，于是 `_owner` 保持 `nullptr`。

后果不是「少了功能」而是静默读错位姿：`CameraComponent::resolveOwnerWorldPose()` 在
`!owner` 时返回 false，`getFreeView()` 落到 orbit 默认分支。视锥线框用的是同一个 view，
所以线框和预览一起错、且错得一致，只有读 world matrix 的 mesh 是对的。

## 修法

不要把 `_owner` 当成「创建后补一下」的字段，而是当成创建的**参数**：

- `detail_component_mutation::addComponent(registry, entity, Entity* owner, ...)` 里 `emplace` 后立即
  `setOwner(owner)`；这是唯一 emplace 组件的地方，所以「创建出没有 owner 的组件」不再可表达。
- `IComponentOps::create` / `ECSRegistry::addComponent(typeIndex|FName, ...)` 都多一个 `Entity* owner`，
  类型擦除路径也必须说出归属。
- `Entity::addComponent` 传 `this`；`Scene::addComponent` 用 `getEntityByEnttID` 解析 owner（Scene 是唯一能
  把 handle 换成 `Entity*` 的地方）；`SceneSerializer` 传它刚创建的那个 `entity`；`component.add` script API
  同样传 entity。
- `Entity::addComponentByName` 的「get or create」两条分支都归位 owner：拿到的实例也属于这个 entity。
- `_owner` 加默认初始化 `= nullptr`（原来是不定值）。

## 边界

- 这类 bug 只在**换写者**时暴露：typed API 一直是对的，所以「新建相机没问题」不能推出
  「载入相机没问题」。查这类问题时先枚举**所有** emplace 路径（`Entity::addComponent`、
  `Scene::addComponent`、`ECSRegistry::addComponent(FName)`、`ComponentOps<T>::clone`），
  再逐条问「它知道 owner 吗」。
- 回归测试：`SceneSerializerTest.LoadedCameraViewAndWireframeUseItsAuthoredPose`（save→load 后 view eye、
  wireframe eye、world matrix 位置三者一致）与
  `SceneNodeLifecycleTest.CreatingAComponentOnAnEntityAssignsThatEntityAsItsOwner`（三条 funnel 都验）。

