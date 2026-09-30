# TODO

- [x] 基线：profile 构建量 TownLarge / HelloMaterial 的 `ResourceResolve/*` 与帧时间（`../rpg-prototype/r4-measurements.md` §7）
- [x] H1 贴图资产槽 + ref 句柄化，删 `TextureFuture` / 贴图 `resolve`，sprite/tilemap/UI 分支删除
- [x] H2 统一编辑入口 `notifyComponentEdited`；材质 / billboard 推送；删材质扫描与重泵；审计降为 dev 一致性检查
- [x] H3 MeshSource / ModelInstantiation 事件化；删 ModelRef 的 `getResourceVersion` 轮询与 `AssetFuture`（含模型 / tileset 槽与 `ModelRef` / `MeshRef` / `TilesetRef` 句柄化）
- [x] H4 Terrain 事件化（回调式高度图、防抖定时器）
- [ ] H5 EnvironmentLighting 事件化（离屏任务 `onFinished`）；`GameplayResourceBinding` 定形；重写 resource-system skill

## 已记录、不在本计划

- 脚本方法调用（`callMethod`）不走统一编辑漏斗：typed setter 已做组件本地失效（含 `setPath` rebind / `setModelPath` invalidate），但派生工作处理器听不到脚本驱动的 setter 调用；目前无生产脚本写模型/材质路径，潜伏态
- tileset JSON 热重载触发源（H3 资产化后 `invalidate/reload` 语义随 AssetManager 统一，仍缺文件监听接线）
- ref 按用途选择 colorSpace（现状统一 SRGB）
- 派生资源 LRU（`gcDerivedResources` / `touchDerivedResourceUsage`）是否改引用计数
- 资产文件热重载没有触发源：`onAssetFileChanged` / `onMetaFileChanged` 无调用方，只有脚本 `asset.reload` 能走到 `invalidate`；需要接文件监听
- `AssetManager::setFrameTaskSink` 在 App 关闭时从不清空，函数静态单例里留着捕获 `&app` 的悬空 sink
- HelloMaterial 场景的 Skybox 实体带一个无材质的 `StaticMeshComponent` 立方体，编辑器视角下在原点画成洋红棋盘格回退材质（与 H1 无关）
