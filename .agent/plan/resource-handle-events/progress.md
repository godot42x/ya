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
  - 写入路径接线：检查器 change hook 与 undo/redo（`PropertyGraph` 默认 hook = `onEdit()` + 经 `_owner` 走漏斗，`PropertyProjection` 删材质专用投影）、脚本 `component.set`（反序列化后走漏斗）、tilemap undo（restore 后调 `onEdit()`）、companion `onUpdateHost` 的带外写入（光照 billboard 的 tint/path/direction）。场景加载 / 模型实例化 / 克隆都经 `addComponent` 创建漏斗，靠 `onComponentAdded` 发现。
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
