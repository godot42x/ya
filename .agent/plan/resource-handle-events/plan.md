# 资源句柄化：加载状态归资产，派生工作由事件驱动

> 2026-09-30 立项。来源：rpg-prototype R0–R4 评审第 2 点（见 `../rpg-prototype/review-2026-09-30.md`）。
> 方向已与用户确认（共享资产槽 / 全范围含地形与环境光照 / 显式占位回退 / 统一编辑入口 / 旧 API 直接删除）。

## 1. 问题

每帧 `RenderDeviceState::prepareDerivedState` 依次驱动三个处理器，每个都在"戳"组件：

| 处理器 | 每帧做的事 |
| --- | --- |
| `GameplayResourceBinding` | 遍历 Static/Skinned mesh、Phong/PBR/Unlit 材质（`needsResolve` 全量扫描 + 30 帧审计 + active 重泵）、UI、billboard、sprite、tilemap（每帧 `tileset.resolve()` 做路径规范化） |
| `TerrainProcessor` | `authoringVersion` 扫描 + 120 帧审计 + active 重泵 + 按 `getResourceVersion` 比对高度图 |
| `EnvironmentLightingProcessor` | skybox / environment 两套：扫描 + 审计 + active 重泵；离屏任务靠每帧查 `phase` |
| `ModelInstantiationSystem` | 遍历 `ModelComponent` 等模型就绪 |

根因：**加载状态存在组件自己的 ref 副本里**（`TextureRef::_cachedPtr/_resolveState/_resolvedVersion`），
`AssetFuture` 只是快照，没有共享状态，于是只能每帧回头问"好了没"。而完成信号其实一直存在：
`loadTexture/loadModel` 的 `onReady` 经 `dispatchToGameThread` 在游戏线程回调，只是没人用。

轮询还带来一类回归：处理器必须记得保留"加载中重泵"循环，拆分时丢掉就静默不渲染
（`memories/terrain_processor_active_pump_regression.md`）。

## 2. 目标

1. 资产的加载状态、资源指针、代数只存一份，放在 `AssetManager` 持有的**共享资产槽**里；ref 只持有路径和槽句柄。
2. 稳态（无加载、无编辑、无热重载）时，资源准备阶段**不遍历任何组件 view**。
3. 派生工作（材质 descriptor、网格取用、模型实例化、地形网格、环境光照预处理）只由三类事件触发：
   编辑改动、资产槽变化（加载完成 / 失败 / 热重载）、GPU 离屏任务完成。
4. 删除 `resolve()` / `isStale()` / `AssetFuture` / `_resourceVersion` + epoch / 各处理器的扫描、审计（release）与 active 重泵循环。

## 3. 边界

- 不改反序列化格式：场景 JSON 里 ref 仍只存路径。
- 不改贴图/模型的解码与上传实现（`TaskQueue` worker 解码、主线程完成回调），只改完成后"写到哪、通知谁"。
- 不引入中心事件总线：通知是点对点的（槽 → 订阅者、Scene 编辑信号 → 处理器、离屏任务 → 发起者）。
- 不做 tileset JSON 热重载、不做贴图 colorSpace 按用途推断（现状：ref 统一走默认 SRGB 请求）；两者记为后续项。
- tilemap 静态实例缓冲、精灵实例化属于 2D 渲染线（review 第 6 点），不在本计划。
- 审计只在 dev 构建保留，形态是"组件状态与处理器状态一致性检查"，发现遗漏即断言，不再承担发现职责。

## 4. 设计

### 4.1 资产槽（Core）

```cpp
enum class EAssetSlotState : uint8_t { Loading, Ready, Failed };

template <typename T>
struct AssetSlot {
    EAssetSlotState     state = EAssetSlotState::Loading;
    std::shared_ptr<T>  resource;       // Ready 时非空；热重载期间保留旧资源
    uint64_t            generation = 0; // 每次填槽（首次就绪 / 失败 / 热重载替换）+1
    std::string         sourcePath;     // 规范化路径，热重载按它匹配全部变体
    AssetObservers      observers;      // RAII 订阅；只在游戏线程触发
};
template <typename T> using AssetHandle = std::shared_ptr<const AssetSlot<T>>;
```

- **写入只在游戏线程**（完成回调已在游戏线程）；读者（提取、处理器）也在游戏线程。H1 开工先核实没有别的线程读 ref。
- H1 落地形态：槽只有 `state / resource / generation`；`sourcePath` 与重载所需的导入设置放在管理器条目里（槽不需要知道自己从哪来）；
  `observers` 推迟到 H2，随第一个订阅者（运行时 Material）一起落地，避免无消费者的接口。
- 槽取代 `Resource/Core/Handle/` 下半迁移的 `ResourceTable / FResourceHandle / PathRegistry`（唯一生产用户是贴图管理器；
  `replace()` 会让已发出的句柄失效，正好与"热重载原地换资源"相反），三者连同单测一起删除。
- **订阅者必须地址稳定**：运行时 `Material`、处理器的 per-entity 状态。**禁止 ECS 组件自己订阅**（entt 存储会搬移组件）。
  需要以实体为单位响应的，由处理器持有订阅令牌，回调只做 `enqueue(entity)`，不改 registry 结构。
- 令牌随订阅者析构自动退订；Scene 卸载时处理器 `dropWork` 析构令牌，不会回调到死场景。

### 4.2 AssetManager

- `acquireTexture(request) -> AssetHandle<Texture>` / `acquireModel(request) -> AssetHandle<Model>`：按 cacheKey 返回已有槽或新建 Loading 槽并提交解码。取代返回 `AssetFuture` 的 `loadTexture/loadModel`。
- 完成：填 `resource`、置 Ready、`generation++`、通知订阅者。失败：置 Failed、`generation++`、通知。
- 热重载 `onAssetFileChanged(path)`：对 `sourcePath == path` 的**所有变体槽**重新解码；完成后替换 `resource`，旧资源进延迟删除队列（规则 6/7），`generation++`，通知。重载期间槽保持 Ready + 旧资源，不闪烁。（现状只重载 SRGB 变体，属于顺带修复。）
- `collectUnused`：释放只被管理器持有的槽（`use_count == 1`），资源走延迟删除。
- CPU 侧批量加载（环境光照面、地形高度图）改为回调式完成：`loadTextureBatchIntoMemory(request{ onDone })`，删除 `consumeTextureBatchMemory` 轮询。
- 删除 `_resourceVersion` / `_resourceVersionEpoch` / `getResourceVersion` / `bumpResourceVersion`；需要"内容版本"的派生缓存键改用槽 `generation`。
- H1 落地形态：
  - 贴图条目按**请求身份**（规范化路径 + colorSpace；`registerTexture` 用名字）索引，而不是按导入设置拼出的 cacheKey，
    这样 meta 变化后原槽原地重填，不用搬键。`loadTexture` 保留原名，返回 `AssetHandle<Texture>`（`onReady` 保留）。
  - 失败统一记在槽上（Failed），不再有独立的失败表；GPU 上传失败以前不记失败、靠 ref 每帧重试，现在同样落 Failed。
  - 没有渲染后端（纯 GUI 宿主、无 App 的单测）时，请求得到一个共享的 Failed 槽，不建条目、不提交解码。
  - `unload(path)` 按路径强制卸载：槽置 Failed 并交出资源（顺带修掉脚本 `asset.unload` 传 `path|metaHash` 永远匹配不到的问题）。
  - `clear()`（后端销毁前）把所有槽置 Failed 并交出资源，ref 不会再拿到已销毁的 GPU 对象。
  - resourceVersion / epoch 在 H1 保留：GUI 贴图源 `epoch()`、脚本 `asset.*`、Terrain、EL 仍在读，随各自的 checkpoint 迁走，H5 删除。
- 热重载**现状没有触发源**：`onAssetFileChanged / onMetaFileChanged` 无调用者，FileWatcher 只看 Lua；唯一入口是脚本 `asset.reload`
  （→ `invalidate`）。H1 让这三个入口都走"原地重填所有变体槽"，单测直接调用；接资产文件监听记为后续项。

### 4.3 Ref 与 TextureSlot

- `TextureRef / ModelRef / MeshRef / TilesetRef` = 路径 + 句柄。在 `set` / `setPath` / 反序列化时立即绑定。
  没有资源层的宿主（纯 GUI）绑定结果为 Failed；有资源层却在 `AssetManager` 创建前绑定属于调用错误，dev 断言。
- `get()` 只在槽 Ready 时返回资源，否则返回空。**ref 不再兜底棋盘格。**
- 删除 `resolve()` / `invalidate()` / `isStale()` / `_resolvedVersion` / `_lastCheckedEpoch` / `onModified`（编辑通知改走 4.4）。
- `TextureSlot` 只剩 `hasPath / isReady / get / isEnabledEffective`，去掉 `resolve / needsResolve / invalidate`。
- `TilesetRef` 同构：同步解析也经由槽，atlas 在解析时绑定。
- H1 落地形态：
  - `AssetRefBase` 的 `resolve()/invalidate()` 合并为 `rebind()`；`setPath` / `setPathWithoutNotify` / 带路径构造 / 反序列化都走它，
    "有路径的 ref 一定已绑定"是不变式。"是否资产引用类型"回收到 Core（四种 ref 类型本来就定义在 Core）。
  - 解析器接口收窄为"按路径取贴图槽"；没有解析器（纯 GUI 宿主）时绑定为空句柄，读作 Failed。原计划的"dev 断言"不做：
    `AssetManager` 是函数内静态单例，不存在"创建前绑定"。
  - `TilesetRef` 不另建槽：解析同步完成，结果只有"有 / 没有"，`_cached` 本身就是共享资源；状态由路径与 `_cached` 推出。
  - `ModelRef / MeshRef`、`loadModel` 与 `AssetFuture<Model>` 推迟到 H3（模型的消费者整体在 H3 事件化），H1 里 `rebind()` = 旧 `invalidate()`。
  - `onModified` 在 H1 保留（材质 / billboard / 地形靠它标脏），H2 随统一编辑入口删除。

### 4.4 统一编辑入口

- `Scene::notifyComponentEdited(entity, type_index)`：经组件注册表的类型蹦床转成 `registry.patch<T>(entity)`，
  所以处理器只需要监听 entt 的 `on_construct / on_update / on_destroy` 三个信号，信号源只有一个。
- 需要接入的写入路径：检查器 change hook、undo/redo 应用、脚本字段写入、反序列化填字段之后、模型实例化生成的组件。
  H2 开工时枚举全部写入路径（参考 `memories/component_created_without_owner.md` 的排查法），逐一接线。
- 组件自带的 `authoringVersion` 保留为"内容版本"（派生缓存键用），但不再承担发现职责。

### 4.5 各消费者的去向

| 消费者 | 之后的形态 |
| --- | --- |
| Sprite / Tilemap / UI | 提取时读 `get()`；`GameplayResourceBinding` 对应分支删除 |
| Billboard | 编辑入口入队；图片槽订阅由处理器持有 |
| Phong/PBR/Unlit 材质 | 组件编辑 → 入队同步参数与槽句柄；运行时 `Material` 订阅槽，`generation` 变化时标 descriptor 脏。Loading 时按语义回退默认图（白 / 平法线），Failed 回退棋盘格，在绑定处显式决定 |
| Static/Skinned mesh（MeshSource） | 持模型句柄；处理器按实体订阅，就绪后取 mesh |
| ModelInstantiation | 同上，就绪后实例化子实体 |
| Terrain | 编辑入口 + 高度图回调式加载 + 防抖定时器（按 tick 的最小堆，只看堆顶）；删扫描 / 审计 / 重泵 |
| Skybox / EnvironmentLighting | 编辑入口 + 贴图槽与批量加载回调 + `OffscreenJobState::onFinished`（在 `finalizeSubmittedOffscreenJobs` 触发）；删扫描 / 审计 / 重泵 |

## 5. Checkpoint

每个 checkpoint 一个提交，计划文件与代码同一提交。开工前先在 profile 构建量出基线（写进 H1 的 progress）。

| ID | 目标 | 验收 |
| --- | --- | --- |
| H1 | 贴图资产槽 + ref 句柄化：`AssetSlot`、`loadTexture` 返回槽、完成/失败/重载填槽、`collectUnused`；`TextureRef / TextureSlot / TilesetRef` 删 `resolve/isStale/invalidate`；`TextureFuture` 删除，全部调用方迁移（EL / Terrain 此时仍按原状态机逐帧查句柄；材质过期检测暂用"绑定的贴图 ≠ 槽里的贴图"）；sprite/tilemap/UI 分支删除；删 `ResourceTable / FResourceHandle / PathRegistry` | 槽生命周期单测（就绪 / 失败 / 重载 generation / 拷贝共享 / 无后端 Failed / clear / collectUnused / unload）；HelloMaterial、2DRpgPrototype、GreedySnake 冒烟；脚本 `asset.reload` 手测 |
| H2 | 统一编辑入口 + 材质/billboard 推送：`notifyComponentEdited`、全部写入路径接线；运行时 Material 订阅槽；删材质扫描与 active 重泵；审计降为 dev 一致性检查；删 `onModified` | 编辑入口单测（每条写入路径都触发 on_update）；检查器改贴图路径下一帧生效；缺失文件显示棋盘格且检查器报错 |
| H3 | 模型槽 + MeshSource / ModelInstantiation 事件化：`loadModel` 返回槽、`ModelRef / MeshRef` 句柄化、删 `AssetFuture` | 模型场景冒烟；运行时加组件、改模型路径生效 |
| H4 | Terrain 事件化：回调式高度图加载、防抖定时器、删扫描/审计/重泵 | HelloMaterial 截图自动化到 stable（`hasPendingTerrainResolve` 探针）；改高度图参数生效 |
| H5 | EnvironmentLighting 事件化：离屏任务 `onFinished`、删扫描/审计/重泵；`GameplayResourceBinding` 收成最终形态（剩余职责不足以成类则并入处理器）；重写 `skills/resource-system`；更新 terrain 回归 memory（该类回归随重泵循环一起消失） | IBL 视觉基线（`memories/ibl_visual_regression_baseline.md`）；稳态 prepare 的组件访问计数为 0（结构指标，计数器断言）+ 多实体压力夹具验证规模无关；帧时间不退化；`rg` 检查见 session_checklist |

验收口径修正（H1 开工时实测，见 `../rpg-prototype/r4-measurements.md` §7）：TownLarge 上 `prepareDerivedState` 只有 0.011 ms/帧，
"耗时接近 0"不构成可区分的验收，本计划的价值是结构性的（去掉轮询与重泵这一类回归），所以 H5 改用结构指标。

## 6. 风险

- **回调重入**：订阅回调只入队，不在回调里改 registry 结构、不录制命令。
- **一帧延迟**：完成回调在 `tickRender` 之后的 `processMainThreadCallbacks`，下一帧 prepare 消费，与现状相同。
- **遗漏写入路径**：H2 的 dev 一致性审计兜住"改了但没通知"，断言即暴露，不静默自愈。
- **测试需要 App 上下文**：反序列化 ref 会立即绑定，涉及 ref 的测试沿用现有 App fixture。
- **并发写入者**：`AssetManager`、`GameplayResourceBinding`、EL、Terrain 可能被其它线同时修改，开工前查 `git log`。
