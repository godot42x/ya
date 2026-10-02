# 资源线遗留

从 `resource-handle-events` 归档时原样搬出。不在那条计划里。

- 脚本方法调用（`callMethod`）不走统一编辑漏斗：typed setter 已做组件本地失效（含 `setPath` rebind / `setModelPath` invalidate），但派生工作处理器听不到脚本驱动的 setter 调用；目前无生产脚本写模型/材质路径，潜伏态
- tileset JSON 热重载触发源（H3 资产化后 `invalidate/reload` 语义随 AssetManager 统一，仍缺文件监听接线）
- ref 按用途选择 colorSpace（现状统一 SRGB）
- 派生资源 LRU（`gcDerivedResources` / `touchDerivedResourceUsage`）是否改引用计数
- 资产文件热重载没有触发源：`onAssetFileChanged` / `onMetaFileChanged` 无调用方，只有脚本 `asset.reload` 能走到 `invalidate`；需要接文件监听
- `AssetManager::setFrameTaskSink` 在 App 关闭时从不清空，函数静态单例里留着捕获 `&app` 的悬空 sink
- HelloMaterial 场景的 Skybox 实体带一个无材质的 `StaticMeshComponent` 立方体，编辑器视角下在原点画成洋红棋盘格回退材质（与 H1 无关）
