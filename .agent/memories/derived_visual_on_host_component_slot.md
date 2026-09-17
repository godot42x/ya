# 派生视觉别占宿主组件槽（相机 gizmo 回归）

## 症状

给相机加「机身」时，最容易的做法是把 `StaticMeshComponent` +
`UnlitMaterialComponent` 加在相机实体自己身上。这样做的代价后来才显形：

- **`.scene.json` 被污染**：`SceneSerializer::serializeEntity` 按类型遍历所有组件，
  当时唯一的忽略表只有 `IDComponent`，于是生成物被写进场景文件，重载后被当成用户内容。
- **资产路径被钉进场景数据**：机身 mesh 写的是 `Engine:Content/Editor/Gizmos/camera.obj`，
  于是「改引擎 gizmo 几何」对旧场景无效，只能靠场景迁移。
- **排除逻辑散落五处**：序列化、克隆、删除级联、渲染提取、拾取各写一份特例，
  而且每加一个消费者就要再写一份。
- **宿主丢槽位**：相机自己的 mesh 槽被 gizmo 占掉，用户不能再给相机挂 mesh。
- **身份判定只能靠试探**：因为不落盘的推断本来就不成立，规则只好用
  「mesh 路径是否等于策略路径」来判断这个 mesh 是不是自己生成的；用户改过路径就永久失配。
- **阴影/Forward/GBuffer 各漏一处**：`isEditorOnly` 当时只在提取和 billboard 两处
  过滤，阴影 pass 直接吃 view 的 bucket，于是 gizmo 投了影。

## 根因

YA 的 ECS 里没有「组件级子对象槽」，层级只存在于 NodeTree。所以带几何、需要自己
树身份的生成物只有一条自然路径：**另开一个 entity + 子 Node，再打生成物标记**。
模型 mesh 早就是这么做的（`ModelInstantiationSystem` + `ManagedChildComponent`），
序列化的 `exclude<ManagedChildComponent>`、`Scene::clone` 的
`shouldSkipClonedNode`、删除的最深优先子树级联、`editorResolveInstanceRoot` 的
拾取归结全都已经就位。走「宿主组件槽」等于放弃这套现成契约，自己重写五遍。

## 结论

1. 生成物 = 子 entity + 子 Node + `ManagedChildComponent{host}`；归属（gizmo 还是内容、
   是否可编辑、打包类别）在宿主类型的 `CompanionSpec` 里声明一次。
2. 视角可见性用 `ERenderFeature` 位：item 声明自己属于哪组，view 声明画哪组，
   过滤收在 `RenderFrameExtractor::prepareView` 的 bucket 绑定一处。
3. 不要给 `IComponent` 加 `_intrinsicTo`/`lifetime` 这类每个组件都要背的字段，
   也不要把「运行时默认隐藏」做成组件 bool —— 全局开关就是策略，不是状态。
4. 归属判定禁止试探资产路径/名字/字段值。

详见 `.agent/skills/scene-object-boundary/SKILL.md`。
