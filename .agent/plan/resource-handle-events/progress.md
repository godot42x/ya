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

- 目标：贴图从"每帧 `resolve()` 轮询 + epoch 比对"改为"ref 在写路径时绑定共享槽，加载在游戏线程填槽"；边界见 plan §5 H1 行，模型 / 材质推送 / Terrain / EL 事件化不在本轮。
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
