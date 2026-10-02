# 资源线遗留

从 `resource-handle-events` 归档时原样搬出。不在那条计划里。

- tileset JSON 热重载触发源（H3 资产化后 `invalidate/reload` 语义随 AssetManager 统一，仍缺文件监听接线）
- ref 按用途选择 colorSpace（现状统一 SRGB）
- 派生资源 LRU（`gcDerivedResources` / `touchDerivedResourceUsage`）是否改引用计数
- 资产文件热重载没有触发源：`onAssetFileChanged` / `onMetaFileChanged` 无调用方，只有脚本 `asset.reload` 能走到 `invalidate`；需要接文件监听
- HelloMaterial 场景的 Skybox 实体带一个无材质的 `StaticMeshComponent` 立方体，编辑器视角下在原点画成洋红棋盘格回退材质（与 H1 无关）
