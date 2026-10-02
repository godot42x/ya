# TODO

- [x] 基线：profile 构建量 TownLarge / HelloMaterial 的 `ResourceResolve/*` 与帧时间（`../rpg-prototype/r4-measurements.md` §7）
- [x] H1 贴图资产槽 + ref 句柄化，删 `TextureFuture` / 贴图 `resolve`，sprite/tilemap/UI 分支删除
- [x] H2 统一编辑入口 `notifyComponentEdited`；材质 / billboard 推送；删材质扫描与重泵；审计降为 dev 一致性检查
- [x] H3 MeshSource / ModelInstantiation 事件化；删 ModelRef 的 `getResourceVersion` 轮询与 `AssetFuture`（含模型 / tileset 槽与 `ModelRef` / `MeshRef` / `TilesetRef` 句柄化）
- [x] H4 Terrain 事件化（回调式高度图、防抖定时器）
- [x] H5 EnvironmentLighting 事件化（离屏任务 `onFinished`）；`GameplayResourceBinding` 定形；重写 resource-system skill
- [x] H5 收尾：审计按 Loading 槽计数开门；模型 seed 探针；IBL 同尺寸对比；TownLarge 帧时间

## 已记录、不在本计划

已搬到 `../../rpg-prototype/resource-leftovers.md`。
