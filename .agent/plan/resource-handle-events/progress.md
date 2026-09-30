# Progress

## 2026-09-30 立项（未开工）

- 调研结论与方向写进 `plan.md` §1–§4；用户确认的五项决策：
  共享资产槽 / 范围含 Terrain 与 EnvironmentLighting / `get()` 未就绪返回空、材质显式回退 /
  统一编辑入口 + entt 信号、dev 审计 / 旧 API 直接删除不留兼容层。
- 调研中确认的事实：
  - 反序列化 ref 时已立即发起异步加载（`ReflectionSerializer.cpp` 调 `resolveAssetRef`），之后靠轮询发现完成。
  - `loadTexture` 的 `onReady` 在游戏线程回调（`dispatchToGameThread` → `AppLifecycle.cpp` 的 frame task sink），`TextureRef` 从不注册。
  - 帧序：`tickLogic` → `tickRender`（含 prepare）→ `TaskQueue::processMainThreadCallbacks()`。
  - 热重载 `onAssetFileChanged` 只重载 SRGB 变体。
  - 离屏任务完成集中在 `finalizeSubmittedOffscreenJobs`，适合挂 `onFinished`。
  - 检查器编辑通知是零散的：材质靠 PropertyProjection change hook 调 `onPropertyChanged`，Skybox/EL 自增 `authoringVersion`，其余靠扫描发现。
- 保留 / 未完成：全部 checkpoint 未开始；本轮只落盘计划，未提交。

## 2026-09-30 H1 贴图资产槽 + ref 句柄化

- 目标：贴图从"每帧 `resolve()` 轮询 + epoch 比对"改为"ref 在写路径时绑定共享槽，加载在游戏线程更新（update）"；边界见 plan §5 H1 行，模型 / 材质推送 / Terrain / EL 事件化不在本轮。
- 完成：
  - `Core/Common/AssetSlot.h`：`AssetSlot<T>{state, resource, generation}` 与 `AssetHandle<T>`。
  - `AssetTextureManager` 重写：条目按请求身份（规范化路径 | colorSpace）存槽；`loadSerial` 丢弃过期完成；失败（含 GPU 上传失败）落在槽上，删 `_failedTextureLoads` / `hardFailure`；无渲染后端返回共享 Failed 槽、不缓存不解码；`unload` / `clear` 把仍被持有的槽置 Failed；`reload` 原地重填所有 colorSpace 变体。
  - `TextureRef` / `TextureSlot` / `TilesetRef`：删 `resolve / isStale / invalidate / set`；`AssetRefBase::rebind()` 在 `setPath` / `setPathWithoutNotify` / 路径构造 / 反序列化（`ReflectionSerializer` 统一调 `setPathWithoutNotify`）时绑定。
  - `TextureFuture` 删除；`GameplayResourceBinding` 删 UI / sprite / tilemap 分支；材质 / billboard / EL 改读槽状态。
  - 删 `ResourceTable / FResourceHandle / PathRegistry` 及其测试；`ya-resource-core-test` 门禁改用 `ResourceCoreClosureTest`。
  - `asset.unload` 接到 `AssetManager::unload(path)`（之前是空操作）。
- 验证：
  - `ya-testing`：1469 个用例，1424 过、44 跳过（窗口用例，本 shell 无 VK_KHR_surface），1 失败为既有环境问题 `GUIWindowManagerTest.DragOverlaySessionIsExemptFromFocusAndInput`（见 rpg-prototype progress）。
  - 新增 `TextureAssetSlotTest`（9 例：共享与拷贝 / 缺失文件 / 重载 generation / unload / clear / collectUnused / 按名注册 / 空路径 / 无后端）全过；`ya-resource-core-test`、`ya-resource-runtime-closure-test` 过。
  - 冒烟全部 exit 0、无新增告警：HelloMaterial 运行时截图与改动前逐像素一致；2DRpgPrototype tilemap 与精灵正常；GreedySnake 正常；编辑器 HelloMaterial 砖墙等材质贴图正常，7 个光源 billboard 全部 Ready 且共享同一个槽（临时诊断日志核实，已删）。
  - 删除检查：H1 命令只剩 `Resource/AssetRef.cpp` 里 ModelRef 的 `getResourceVersion`，按 plan 留到 H3。
- 行为变化：
  - EL 圆柱贴图加载失败时过渡失败退出，不再无限等待。
  - GPU 上传失败置 Failed，不再每帧重试。
  - tilemap / 精灵加载期间不再画棋盘格（explicit placeholder：未就绪不画）。
  - 反序列化的 ModelRef 不再在反序列化时发起加载，改为首次 resolve 时（差一帧，H3 消除）。
- 保留 / 未完成：
  - resourceVersion / epoch 保留到 H5，`onModified` 保留到 H2，ModelRef / MeshRef / `loadModel` / `AssetFuture<Model>` 保留到 H3。
  - 材质过期检测为过渡实现（运行时材质绑定的贴图 ≠ 槽里的贴图则 invalidate），H2 换成订阅。
  - 脚本 `asset.reload` 未手测；同一路径由 `ReloadRefillsTheSameSlot` 单测覆盖到 `AssetManager` 层。
- 偏离：plan §5 H5 验收口径改为结构指标（稳态 prepare 组件访问计数为 0 + 多实体压力夹具），原因是 TownLarge 上 `prepareDerivedState` 实测只有 0.011 ms/帧（`../rpg-prototype/r4-measurements.md` §7）。

## 2026-09-30 H2 统一编辑入口 + 材质/billboard 推送

- 目标：统一编辑入口 + 材质/billboard 事件化。边界见 plan §5 H2 行；模型 / tileset 归 H3（本轮已把「tileset 并入 H3」写进 plan §4.3/§5），Terrain 归 H4，EL 归 H5。
- 完成：
  - 统一编辑入口：`Scene::notifyComponentEdited(entity, typeIndex)` → `ECSRegistry::ComponentOps<T>::notifyEdited` 蹦床 → `registry.patch<T>`（entt on_update）+ 一次 `SceneBus::onComponentEdited` 广播；创建 / 删除由 `detail_component_mutation` 漏斗广播 `onComponentAdded` / `onComponentRemoved`。信号源各只有一个。
  - 写入路径接线：检查器 change hook 与 undo/redo（`PropertyGraph` 默认 hook = `onEdit()` + 经 `_owner` 走漏斗，`PropertyProjection` 删材质专用投影）、脚本 `component.set`（反序列化后走漏斗）、tilemap undo（restore 后调 `onEdit()`）、companion `onUpdateHost` 的带外写入（光照 billboard 的 tint/path/direction）。场景加载 / 模型实例化 / 克隆都经 `addComponent` 创建漏斗，靠 `onComponentAdded` 发现。（H2 后补：脚本字段写入 `afterWrite` 也经 owner 走漏斗——写入路径普查 [子任务] 核实的最后一块孔洞；方法调用保持现状，typed setter 已做本地失效。）
  - `GameplayResourceBinding` 重写：事件驱动——SceneBus 三信号入队 + 处理器持有的每实体槽订阅（fill 回调只入队）；删 per-frame `needsResolve` 扫描、30 帧审计、active 重泵循环与 `checkTexturesStaleness`；mesh 扫描保留（H3）。场景卸载后残留 fill 回调只按指针值查工作表，不触碰已销毁 registry。
  - 显式回退：`loadingSlotFallback`（白 / 平法线，`TextureLibrary` 新增 1×1 flat-normal (128,128,255)）、`failedSlotFallback`（棋盘格 + WARN）。删 `EMaterialResolveState::Resolving`、`onPropertyChanged / summarizePropertyChanges / onModified / setPathWithoutNotify`。
  - dev 一致性审计（`BUILD_DEBUG`，120 帧一次）：凡有 Loading 槽的材质 / billboard 必须已入队或已有订阅，否则断言并自愈——覆盖重泵回归类。
- 机制偏离（相对 plan §4.4 的 entt sink 订阅）：处理器不直接连 entt sink，而订阅既有全局 `SceneBus`。原因：sink 连接绑定 per-scene registry 生命周期，处理器无法在场景析构后安全 disconnect；SceneBus 的 `onComponentRemoved` 已是「中心漏斗 + registry& 参数」的既有先例（rule 5）。单一信号源不变：edit 只经 notifyComponentEdited，构造只经 addComponent 漏斗。
- 验证：
  - `ya-testing` 1473 过、1 跳过（窗口用例，环境性）。新增 `SceneEditFunnelTest`（2 例：创建/删除经漏斗广播；notifyComponentEdited → patch on_update + 广播，未知类型/非法实体为 no-op）。
  - 冒烟 exit 0 / 无新告警：HelloMaterial 运行时（异步贴图全部 ready，无 failed/audit 断言）、GreedySnake、2DRpgPrototype、编辑器 HelloMaterial（60s 干净 teardown）。
- 行为变化：
  - 检查器改材质贴图路径 / billboard 图片在下一帧 prepare 生效（原靠扫描或专用投影回调）。
  - 检查器面板多选批量编辑时每个实例独立走漏斗。
  - 材质 Loading 期间显示语义回退图（法线槽平法线），Failed 显示棋盘格且检查器报错——不再静默空槽。
- 保留 / 未完成：mesh 扫描 / ModelRef 轮询 / TilesetRef 自缓存归 H3；resourceVersion / epoch 归 H5；实体 token 在组件删除与场景卸载之间的短暂滞留依赖 SlotSubscription 持句柄保活（见 H3/H5 清理项——不需要，句柄保活即安全）。
- 偏离：审计从「组件状态 vs 处理器状态全量一致性」收窄为「Loading 槽必须有订阅或入队」；参数编辑的「改了但没通知」目前不可低成本检测，依赖写入路径清单（本轮已逐一接线并在本条列出）。

## 2026-10-01 H3 模型/tileset 资产槽 + MeshSource/ModelInstantiation 事件化

- 目标：模型槽 + tileset 资产化；`loadModel` / `acquireTileset` 返回槽；`ModelRef` / `MeshRef` / `TilesetRef` 句柄化；删 `AssetFuture` 与 ModelRef 轮询；MeshSource / ModelInstantiation 事件化。边界见 plan §5 H3 行；Terrain 归 H4，EL 归 H5，全局 `resourceVersion` 删除归 H5。
- 完成：
  - `Core/Common/AssetRef.h`：`ModelRef` / `MeshRef` 重写为 `AssetHandle` 句柄型（默认拷贝共享槽）；删 `resolve/invalidate/set/_cachedPtr/_resolveState/_resolvedVersion` 与 `EAssetResolveResult` / `EAssetResolveState::Dirty`；`IAssetRefResolver` 改三接口 `acquireTexture/acquireModel/acquireTileset`（删 `resolveAssetRef`）。`AssetFuture.h` 删除。
  - `Resource/Manager/AssetModelManager` 重写为槽制（镜像 AssetTextureManager）：`ModelEntry{slot,filepath,loadSerial,readyCallbacks}`、`SlotUpdate`、`updateSlotLocked/dispatchSlotUpdate`；`loadModel` 返回槽（Loading 挂 onReady，settle 立即派发）；失败路径（解码 / 无渲染后端 / GPU 网格创建）全部落槽；`unload/invalidate/evictCachedAsset`（Failed+gen++、DDQ retire、删条目）、`collectUnused`（use_count==1 且非 Loading 才回收）、`fillStats` 只数 Ready。删 `isModelLoadPending`。
  - 新建 `Resource/Manager/AssetTilesetManager`：`acquireTileset` 同步解析入槽（锁外 VFS 读 + `parseTilesetJson`，锁内填槽、锁外派发）；`registerTileset` 镜像 `registerTexture`；`invalidate` 删条目（下次 acquire 重解析，持有者保旧槽）。
  - `AssetManager`：`loadModel` 返回槽、tileset 公有接口（`acquireTileset/registerTileset/isTilesetLoaded/getTileset`）、`clearCache/collectUnused/unload/invalidate/onMetaFileChanged/onAssetFileChanged` 接入 tileset 分支。
  - `TilesetRef`：`AssetHandle<Tileset> _handle`；删进程级 weak 缓存 / mutex / `clearCache`；`getResolveState` 修为按槽态映射（此前 `_handle ? Ready` 会让 Failed 槽漏检 `PropertyAccessor` 审计）。
  - `MeshSource`：`_ownerModel` → `_modelHandle`；`resolve()` 首次绑 `loadModel` 槽，Loading 由订阅覆盖（GRB mesh 脏队列驱动 `subscribeMeshSlotObserver`），Failed / 无 meshIndex 为 WARN 终态。
  - `ModelInstantiationSystem` 事件化：SceneBus 三信号入队 / 退订；Loading 持 model 槽订阅（fill 回调只入队，每次 pump 重建 token 处理路径编辑换槽）；`seedSceneWork` 覆盖先于订阅存在的组件；稳态不触碰组件视图。
  - `GameplayResourceBinding`：mesh 入 watched（`isMeshType` 静态/蒙皮）、`dirtyMeshQueue/Set`、`SlotSubscription.handle` 泛化 `shared_ptr<const void>`、`EDirtyWork{Material,Billboard,Mesh}`；dev 审计加 `anyMeshSlotLoading` 分支。
- 验证：
  - `ya-testing` 1494 过、1 跳过（窗口用例，环境性）。新增 `ModelAssetSlotTest`（6 例：共享与拷贝 / 缺失文件 onReady / invalidate 删条目换新槽 / unload / clear / collectUnused）、`TilesetAssetSlotTest`（5 例：缺失文件立即 Failed / 注册槽 Ready 与回收 / invalidate 重解析 / unload 通知 observers / 空路径）、`ModelInstantiationEventTest`（2 例：seed 与 bus 两条入队路径 + 缺失模型 childless 终态 + 路径编辑重入队；删组件丢弃 pending work 后槽填充惰性）。
  - `xmake b -g test` 全闸门绿；`ya-resource-runtime-closure-test` 22/22。
  - 冒烟全部干净退出、无新增告警：HelloMaterial runtime（模型异步解码正常、teardown 干净）、HelloMaterial editor、2DRpgPrototype editor（tilemap 无错误）、GreedySnake runtime。
- 行为变化：
  - 模型加载失败（含反序列化路径编辑换文件）成为槽上 Failed 终态，不再每帧重试或轮询发现。
  - 反序列化的 ModelRef 在 `MeshSource::resolve` / 实例化 pump 首次绑槽（不再反序列化时轮询发起）。
  - tileset 文档首次请求时同步解析进槽；Core 不再持进程级 weak 缓存。
  - 编辑器资产审计能报出缺失 tileset（getResolveState 修正）。
- 保留 / 未完成：Terrain 归 H4；EL 离屏回调归 H5；全局 `resourceVersion/bumpResourceVersion` 归 H5；tileset JSON 文件监听触发源仍未接线（见 todo「已记录、不在本计划」）。
- 偏离：`fillStats.modelCount` 语义收窄为「Ready 条目数」（旧语义），避免前序套件残留的 Failed 条目污染 `AssetLibraryInspectsPaths`；`TextureAssetSlotTest` fixture 清场从 `clearTextures` 扩为 `clearCache`（新 manager 让 failed 模型 / tileset 条目可被 `collectUnused` 回收，绝对计数断言对测试顺序敏感）。

## 2026-10-01 H4 Terrain 事件化

- 目标：Terrain 改为编辑入口发现 + 高度图批次完成回调 + 按 tick 的防抖最小堆；删作者扫描、120 tick 审计、active 重泵。边界见 plan §5 H4 行。EnvironmentLighting 的重泵与 `onFinished`、全局 `resourceVersion` 删除归 H5。
- 完成：
  - `TextureBatchMemoryLoadRequest::onReady`：CPU 批次解码写入 ready 表后在游戏线程回调（锁外），参数是 `consumeTextureBatchMemory` 的句柄。空路径同步完成也回调。EL 不传回调，仍按原轮询消费。
  - `TerrainProcessor`：`init/shutdown` 订阅 SceneBus（add/edit 按 `rebuildNotBeforeTick` 排期，remove 丢状态）；首次 prepare 仍 seed 一次（覆盖 `registry.emplace` 与订阅前已存在的组件）。未来 tick 进最小堆，prepare 只看堆顶，serial 不匹配的条目是被更新的排期，弹出即丢。
  - 高度图：到点才 `loadTextureBatchIntoMemory`，`onReady` 只入队；回调前再 prepare 保持 `LoadingHeightMap`，不再每帧重泵。处理器持有高度图槽订阅，槽更新入队（排期未到的 tick 忽略，避免绕过堆）。换路径 / 再编辑会抬高 `batchSerial`，过期回调把已完成的批次 consume 掉丢弃。
  - 删 `sweepAuthoringDirty`、`auditResolveWork`、`active` 集合。派生缓存 GC 保留（不是组件扫描）。`getResourceVersion` 仍用作派生缓存键，删除归 H5。
- 验证：
  - `ya-testing` 1497 过、1 跳过。新增 `TerrainResolveEventTest`（2 例：堆顶未到保持 Dirty、到点 Loading、回调前第二次 prepare 仍 Loading、回调后缺失文件 Failed；add 之后的路径编辑与 remove 走 SceneBus，删除后的批次回调惰性）。`SceneDerivedStateTest` 的跨场景地形状态指针稳定仍过。
  - HelloMaterial `--screenshot` 在第 45 帧达到 stable（30 warmup + 5 stable）并写出 PNG，exit 0，日志 0 error。编辑器 HelloMaterial、2DRpgPrototype、GreedySnake `--exit-after-frame=90` 均 exit 0、无 error。
- 行为变化：
  - 运行时加 Terrain、改高度图路径或网格参数，经编辑漏斗在下一次 prepare 排期重建（`rebuildNotBeforeTick` 在未来则等到该 tick）。
  - 高度图文件重载通过槽订阅入队，不再靠 120 tick 审计比对 `getResourceVersion`。
- 保留 / 未完成：EL 扫描 / 审计 / 重泵与离屏 `onFinished` 归 H5；全局 `resourceVersion` 删除归 H5；`GameplayResourceBinding` 定形与 resource-system skill 重写归 H5。
- 偏离：防抖沿用组件上已有的 `rebuildNotBeforeTick`，没有另加固定延迟。检查器 `onEdit` 传 0，仍然立即重建，HelloMaterial 首帧加载节奏不变。
