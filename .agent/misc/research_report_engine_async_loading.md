# 商业引擎异步资源加载与 Pending 资源处理机制研究报告

## 摘要

本报告调研了 Unreal Engine 5、Unity、Godot 4 以及 CryEngine 在异步资源加载方面的设计，重点分析了四个维度：完成通知机制（回调 vs 轮询）、pending 资源的渲染处理（占位符/fallback 策略）、GPU 资源创建的线程模型、以及软引用/延迟加载的设计模式。研究结论为 Neon 引擎的异步加载方案提供了跨引擎的参考依据。

---

## 一、Unreal Engine 5

### 1.1 异步加载架构

UE5 的异步加载系统分为三层。顶层是 `FStreamableManager`，提供 `RequestAsyncLoad` 接口；中层是 `FStreamableHandle`，用于跟踪单个请求的状态和完成回调；底层是 `FAsyncLoadingThread` 配合 `LoadPackageAsync`，在独立的后台线程中执行 Package 的反序列化。

完整流程是：调用 `RequestAsyncLoad` 后创建 `FStreamableHandle`，绑定用户的 `FStreamableDelegate` 回调。系统调用 `StartHandleRequests` 遍历资源列表，对每个资源执行 `StreamInternal` —— 如果已在内存中则直接标记就绪，否则调用 `LoadPackageAsync` 发起后台加载。后台线程通过 `FLinkerLoad` 反序列化 Package 数据，完成后触发内部回调 `AsyncLoadCallback`，回到游戏线程执行 `CheckCompletedRequests`。只有当一个 Handle 关联的所有资源（包括递归依赖项）全部加载完成后，才会执行用户回调。

### 1.2 完成通知：回调为主，轮询为辅

UE5 主要采用**回调模式**。`FStreamableDelegate` 支持绑定 Lambda、UObject 成员函数或静态函数，保证在**游戏主线程**执行。同时也支持轮询：开发者可以通过 `FStreamableHandle::IsActive()` 或 `GetAsyncLoadPercentage()` 查询进度。

关键设计点在于：回调是在游戏线程的帧更新中被触发的，本质上是"加载线程完成 → 结果进入完成队列 → 游戏线程在帧更新中 flush 队列并触发回调"。所以 UE5 的回调机制底层仍然是**主线程 poll 完成队列**的模式，只是对外暴露为回调 API。

### 1.3 Pending 资源渲染处理

UE5 使用**默认材质替代**策略。引擎启动极早期就通过 `UMaterialInterface::InitDefaultMaterials()` 为所有材质域（Surface、Deferred Decal、Light Function、UI 等）创建了默认材质，并通过 `AddToRoot()` 防止被垃圾回收。当纹理或材质缺失时，显示紫/黑棋盘纹理作为错误指示。对象不会被跳过渲染，而是使用占位符材质继续渲染，真实资源就绪后自动替换。

纹理流送方面，UE5 使用虚拟纹理系统，Mipmap 根据屏幕距离动态加载/卸载，流送池预算管理纹理内存。低分辨率 Mip 先显示，高分辨率后续流入。

### 1.4 GPU 资源创建

CPU 端反序列化在 `FAsyncLoadingThread` 中完成，创建 UObject（如 `UTexture2D`）。实际的 GPU 资源（VkImage、D3D12Resource 等）通过 `ENQUEUE_RENDER_COMMAND` 宏向渲染线程发送命令来创建。渲染线程处理完命令后 GPU 资源才真正可用。游戏线程不会被阻塞。

### 1.5 软引用：TSoftObjectPtr

`TSoftObjectPtr` 只存储资源路径字符串，不会在初始化时加载资源。开发者可以通过 `LoadSynchronous()` 同步加载（阻塞），或配合 `FStreamableManager::RequestAsyncLoad` 异步加载。`IsPending()` 用于检查资源是否正在加载中。与硬引用 `TObjectPtr` 相比，软引用在初始化时只占极小的路径字符串内存。

---

## 二、Unity

### 2.1 异步加载架构

Unity 有两套资源管理系统。传统的 `AssetBundle` 通过 `AssetBundle.LoadAssetAsync` 返回 `AssetBundleRequest` 对象。现代的 Addressables 系统通过 `Addressables.LoadAssetAsync<T>` 返回 `AsyncOperationHandle<T>`，支持自动依赖管理、引用计数和热更新。

### 2.2 完成通知：三种方式并存

Unity 同时支持三种完成通知方式，全部在**主线程**执行：

第一种是**回调事件**：`handle.Completed += callback`，操作完成后立即触发。第二种是**协程**：`yield return handle`，利用 `IEnumerator` 接口自动等待。第三种是 **async/await**：`await Addressables.LoadAssetAsync<T>()`，利用 C# 的异步语法。

特别值得注意的是 `AsyncOperation.completed` 事件的延迟调用保证：如果在创建操作的同一帧注册事件处理程序，即使操作能同步完成，回调也会被推迟到下一帧执行。这保证了帧逻辑的一致性。

Unity 6 引入的 `Awaitable` 进一步优化了性能——避免了 `Task` 捕获 `SynchronizationContext` 的开销，且如果调用线程和恢复线程一致，恢复是即时的，无需等待下一帧。

### 2.3 Pending 资源渲染处理

当材质或纹理缺失时，Unity 使用 Fallback Shader 机制，缺失的资源显示为**品红色（Magenta）**。渲染不会被跳过，而是使用 `DrawingSettings.fallbackMaterial` 应用默认材质继续渲染。开发者可以在 ShaderLab 中通过 `FallBack` 指令指定回退着色器。

### 2.4 GPU 资源上传：异步上传管道

Unity 的异步上传管道（Async Upload Pipeline）是一个亮点。构建时，元数据写入 `.res` 文件，二进制数据写入单独的 `.resS` 文件。运行时仅在单帧内加载元数据，通过环形缓冲区在**多帧内以时间切片方式**将二进制数据流式传输到 GPU。

两个关键配置：`QualitySettings.asyncUploadTimeSlice` 控制每帧用于上传的 CPU 时间（毫秒），`QualitySettings.asyncUploadBufferSize` 控制环形缓冲区大小（2-2047 MB）。上传在渲染线程进行。

符合异步上传的条件：纹理未启用读/写功能、不在 Resources 文件夹中、Android 平台启用了 LZ4 压缩；网格无 BlendShapes、无动态批处理、无骨骼权重、拓扑非四边形。

### 2.5 资源引用：AssetReference

Addressables 的 `AssetReference` 支持 Inspector 拖拽分配，内部使用引用计数管理资源生命周期。加载时自动加载所有依赖，卸载时依赖项在引用计数归零后自动卸载。与直接引用相比，`AssetReference` 支持异步加载和热更新。

---

## 三、Godot 4

### 3.1 异步加载架构

Godot 4 提供三个核心函数：`ResourceLoader.load_threaded_request(path)` 启动后台线程加载，立即返回；`load_threaded_get_status(path, progress)` 获取加载状态（返回 `THREAD_LOAD_IN_PROGRESS`、`THREAD_LOAD_LOADED`、`THREAD_LOAD_FAILED` 等枚举）；`load_threaded_get(path)` 获取已完成的资源。

### 3.2 完成通知：纯轮询模式

Godot 采用纯**轮询模型**，没有内置的完成信号。开发者需要在 `_process()` 中手动检查状态，并在确认 `THREAD_LOAD_LOADED` 后手动 emit 自定义信号。文档明确建议在不同帧调用 `get_status` 而非循环中连续调用。如果在资源未完成时调用 `load_threaded_get()`，主线程会被阻塞直到加载完成。

Godot 3.x 使用了不同的 API（`ResourceInteractiveLoader`），通过 `poll()` 方法每帧推进加载，通过 `get_stage()` / `get_stage_count()` 计算进度。

### 3.3 Pending 资源渲染处理

Godot 没有内置的自动 fallback/默认资源机制。开发者需要自行实现：检测加载失败后手动提供默认资源，或使用条件渲染展示加载中 UI。Godot 4 提供了 `InstancePlaceholder` 用于场景级别的占位加载（如大型 3D 世界的选择性加载）。

### 3.4 GPU 资源创建

`RenderingServer` 的调用不是线程安全的。GPU 资源创建必须在主线程进行。后台线程只能做 CPU 端数据准备，然后通过 `call_deferred()` 将 GPU 创建请求排队到主线程。

---

## 四、CryEngine 5

### 4.1 两阶段回调模型

CryEngine 的流式系统是最精细的。`IStreamCallback` 接口定义了两个回调：`StreamAsyncOnComplete()` 在异步回调线程执行，用于 CPU 密集的数据处理（如解压、解码）；`StreamOnComplete()` 在主线程执行，用于引擎状态更新和资源释放。

完整的线程流水线为：IO 线程 → 解压线程 → 异步回调线程（数量取决于平台，部分专用于几何体和纹理）→ 主线程。这种两阶段设计充分利用了多核 CPU，将 CPU 密集工作分离到异步线程，只把轻量的状态更新留给主线程。

---

## 五、跨引擎模式对比

### 5.1 完成通知机制

| 引擎 | 主要方式 | 回调执行线程 | 底层实现 |
|------|---------|------------|---------|
| UE5 | Lambda 回调（FStreamableDelegate） | 游戏主线程 | 主线程帧更新 flush 完成队列 |
| Unity | 回调/协程/async-await 三选一 | 主线程 | AsyncOperation 事件系统 |
| Godot 4 | 纯轮询 + 手动信号 | 主线程（_process） | 每帧 check status |
| CryEngine | 两阶段回调 | 异步线程 + 主线程 | IO → 解压 → 异步回调 → 主线程 |

核心发现：**所有引擎的用户回调最终都在主线程执行**（CryEngine 的第一阶段除外）。UE5 虽然对外暴露回调 API，但底层仍然是主线程 poll 完成队列。区别只在于对开发者暴露的接口风格不同。

### 5.2 Pending 资源渲染策略

| 引擎 | 纹理未就绪 | 网格未就绪 | 材质未就绪 |
|------|----------|----------|----------|
| UE5 | 低分辨率 Mip 先显示，流送高分辨率 | 对象保留在渲染队列，使用默认材质 | 紫/黑棋盘纹理 |
| Unity | 品红色显示 | 使用 fallbackMaterial 继续渲染 | 品红色 Fallback Shader |
| Godot | 无内置 fallback | 不渲染（开发者自行处理） | 无内置 fallback |
| CryEngine | 低 Mip 占位 | 低 LOD 占位 | 默认材质 |

### 5.3 GPU 资源创建线程模型

所有引擎遵循同一模式：后台线程处理 IO 和 CPU 计算 → 主线程或渲染线程创建 GPU 资源。UE5 通过 `ENQUEUE_RENDER_COMMAND` 发送到渲染线程；Unity 通过异步上传管道在渲染线程以时间切片方式上传；Godot 和 CryEngine 都要求在主线程创建 GPU 资源。

---

## 六、对 Neon 引擎的启示

基于以上调研，Neon 引擎当前的架构选择——**主线程 poll 完成队列**模式——与 UE5 的底层实现和 Godot 的显式 API 完全一致。这不是简化方案，而是业界主流实践。

具体建议：

**完成通知**：采用"主线程每帧 poll 完成队列"是正确的选择。UE5 对外暴露的 `FStreamableDelegate` 回调底层就是这个模式。对于 Neon 的 ECS 架构来说，`ModelInstantiationSystem` 已经是 poll 模式，保持一致性最重要。如果后续需要回调 API，可以在 poll 层之上包装一个 delegate 接口。

**Pending 资源处理**：Neon 已有的白色纹理 fallback 和 null mesh 跳过绘制机制，与 UE5/Unity 的"默认材质+占位符"策略思路一致。建议后续考虑增加紫/黑棋盘纹理作为"加载失败"的可视化指示，与"正在加载中"的白色纹理区分。

**GPU 资源创建**：保持在主线程（有 Vulkan context 的线程）创建 GPU 资源，与所有引擎一致。Unity 的异步上传管道（时间切片上传到 GPU）是一个可以后续考虑的优化点，但对 MVP 阶段不是必需的。

**软引用设计**：Neon 的 `TAssetRef<T>` + `EAssetResolveState` 状态机与 UE5 的 `TSoftObjectPtr` + `FStreamableHandle` 设计理念一致，都是路径存储 + 延迟 resolve 模式。当前的 `resolve()` 改为非阻塞后，这个状态机正好能完整利用。

---

## References

1. [FStreamableManager - Unreal Engine 5.7 Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FStreamableManager)
2. [UE5 资源异步加载流程分析 - BoilTask's Blog](https://boiltask.com/knowledge/ue/asset-async-load/)
3. [虚幻引擎线程渲染 - Epic Developer Community](https://dev.epicgames.com/documentation/zh-cn/unreal-engine/threaded-rendering-in-unreal-engine)
4. [UE5中TSoftObjectPtr的使用详解 - CSDN](https://blog.csdn.net/m0_45371381/article/details/147018736)
5. [UE 中默认材质初始化 - 博客园](https://www.cnblogs.com/straywriter/p/15874335.html)
6. [LoadPackageAsync - Unreal Engine 5.7 Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/CoreUObject/LoadPackageAsync)
7. [Overview of the Addressables system - Unity](https://docs.unity3d.com/Packages/com.unity.addressables@1.20/manual/AddressableAssetsOverview.html)
8. [AsyncOperationHandle - Unity Addressables](https://docs.unity3d.com/Packages/com.unity.addressables@0.7/manual/AddressableAssetsAsyncOperationHandle.html)
9. [AsyncOperation.completed - Unity Documentation](https://docs.unity3d.org.cn/6000.0/Documentation/ScriptReference/AsyncOperation-completed.html)
10. [Awaitable completion and continuation - Unity 6.0 Manual](https://docs.unity3d.com/6000.0/Documentation/Manual/async-awaitable-continuations.html)
11. [异步纹理上传 - Unity 手册](https://docs.unity3d.com/cn/2019.3/Manual/AsyncTextureUpload.html)
12. [配置异步上传管道 - Unity Documentation](https://docs.unity3d.org.cn/6000.0/Documentation/Manual/configure-asynchronous-upload-pipeline.html)
13. [Loading texture and mesh data - Unity Manual](https://docs.unity.cn/Manual/LoadingTextureandMeshData.html)
14. [Asset References - Unity Addressables](https://docs.unity3d.com/Packages/com.unity.addressables@1.20/manual/AssetReferences.html)
15. [AssetBundle.LoadAssetAsync - Unity Scripting API](https://docs.unity3d.com/2022.1/Documentation/ScriptReference/AssetBundle.LoadAssetAsync.html)
16. [ResourceLoader - Godot Engine Documentation](https://docs.godotengine.org/en/stable/classes/class_resourceloader.html)
17. [后台加载 - Godot Engine 4.x 文档](https://docs.godotengine.org/zh-cn/4.x/tutorials/io/background_loading.html)
18. [Using multiple threads - Godot Engine Documentation](https://docs.godotengine.org/en/stable/tutorials/performance/using_multiple_threads.html)
19. [后台加载 - Godot Engine 3.x 文档](https://docs.godotengine.org/zh-cn/3.x/tutorials/io/background_loading.html)
20. [CRYENGINE Streaming System Documentation](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756813/pages/23306430)
21. [Asynchronous Asset Loading - Unreal Engine Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/asynchronous-asset-loading-in-unreal-engine)
