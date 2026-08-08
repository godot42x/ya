# File Ownership Baseline

> 生成日期：2026-08-08  
> 作用：为 Phase 1-4 的文件搬迁提供“主职责 / 允许依赖层 / 现状越界”三类标注。
> 规则：模块归属按目录前缀判定（最长前缀优先）；允许依赖层见
> `dependency-baseline.md` §6 与计划目标依赖图；越界 = include 指向不在
> 允许层内的模块。

## 统计

| 模块 | 文件数（h+cpp） |
|---|---|
| ya-editor | 65 |
| ya-foundation-core | 103 |
| ya-foundation-rhi | 40 |
| ya-foundation-rhi-backend | 55 |
| ya-gameplay-ecs | 76 |
| ya-gui-runtime | 15 |
| ya-host | 57 |
| ya-physics | 3 |
| ya-render-3d | 137 |
| ya-render-graph | 9 |
| ya-resource | 48 |
| ya-scene-3d | 2 |

## 模块明细

图例：`[越界]` 表示该文件包含允许层之外的模块头（括号内为越界目标）。

### ya-foundation-core（Core）

**`./`**

- `Api.h`
- `Base.cpp`
- `Base.h`
- `Delegate.h`
- `Event.h`
- `FName.cpp`
- `FName.h`
- `KeyCode.h`
- `Log.cpp`
- `Log.h`
- `MessageBus.cpp`
- `MessageBus.h`
- `Object.h`
- `ResourceRegistry.cpp`
- `ResourceRegistry.h`
- `SingletonManager.cpp`
- `SingletonManager.h`
- `Trait.h`
- `TypeIndex.h`
- `UUID.cpp`
- `UUID.h`

**`Async/`**

- `LockFreeQueue.h`
- `TaskQueue.cpp`
- `TaskQueue.h`

**`Camera/`**

- `Camera.cpp`
- `Camera.h`
- `CameraController.cpp`
- `CameraController.h`

**`Common/`**

- `AssetFuture.h`
- `AssetRef.cpp`
- `AssetRef.h`
- `DeferredDeletionQueue.cpp`
- `DeferredDeletionQueue.h`
- `FWD-std.h`
- `FWD.h`
- `Helper.h`
- `StateTransition.h`
- `Types.h`
- `Warnings.h`

**`Debug/`**

- `RenderDocCapture.cpp`
- `RenderDocCapture.h`

**`Input/`**

- `InputManager.cpp`
- `InputManager.h`
- `InputMode.h`

**`Macro/`**

- `VariadicMacros.h`

**`Manager/`**

- `ClockManager.h`
- `Facade.cpp`
- `Facade.h`
- `TimerManager.h`

**`Math/`**

- `AABB.h`
- `GLM.h`
- `Geometry.cpp`
- `Geometry.h`
- `Math.h`
- `Ray.h`
- `ScreenUtil.h`

**`Module/`**

- `Module.h`
- `ModuleManager.cpp`
- `ModuleManager.h`
- `ModuleManifest.cpp`
- `ModuleManifest.h`
- `PluginDescriptor.cpp`
- `PluginDescriptor.h`
- `ProjectDescriptor.cpp`
- `ProjectDescriptor.h`

**`Profiling/`**

- `Instrumentor.cpp`
- `Instrumentor.h`
- `PerfKeys.h`
- `PerfState.cpp`
- `PerfState.h`
- `Profiling.h`
- `StaticInitProfiler.cpp`
- `StaticInitProfiler.h`
- `StaticInitProfilerEnd.cpp`
- `StaticInitProfilerStart.cpp`

**`Profiling/MSVC/`**

- `End.cpp`
- `Start.cpp`

**`Reflection/`**

- `ContainerProperty.h`
- `ContainerTraits.h`
- `DeferredInitializer.cpp`
- `DeferredInitializer.h`
- `InstanceRef.h`
- `MetadataSupport.h`
- `MethodReflection.h`
- `PropertyExtensions.h`
- `Reflection.h`
- `ReflectionCopier.cpp`
- `ReflectionCopier.h`
- `ReflectionHelper.h`
- `ReflectionSerializer.cpp`
- `ReflectionSerializer.h`
- `UnifiedReflection.deprecated.h`

**`Scripting/`**

- `ScriptApiAsset.h`
- `ScriptApiRegistry.cpp`
- `ScriptApiRegistry.h`

**`Scripting/Lua/`**

- `YaLua.h`

**`System/`**

- `FileWatcher.cpp`
- `FileWatcher.h`
- `PathUtils.h`
- `System.cpp`
- `System.h`
- `VirtualFileSystem.cpp`
- `VirtualFileSystem.h`

### ya-foundation-rhi（RHI interface + platform-independent）

**`./`**

- `Render.h`
- `RenderDefines.cpp`
- `RenderDefines.h`
- `Shader.cpp`
- `Shader.h`
- `WindowProvider.cpp`
- `WindowProvider.h`

**`Core/`**

- `Buffer.h`
- `BuiltinTextureSource.cpp`
- `BuiltinTextureSource.h`
- `CommandBuffer.h`
- `DescriptorSet.h`
- `FrameBuffer.h`
- `FrameUploadArena.cpp`
- `FrameUploadArena.h`
- `Handle.h`
- `Image.h`
- `ImageResourceRef.h`
- `OffscreenJob.h`
- `Pipeline.h`
- `RenderAttachmentFormats.h`
- `RenderImage.cpp`
- `RenderImage.h`
- `RenderPass.h`
- `RenderResourceFactory.h`
- `RenderTargetCreateInfo.h`
- `RenderingInfoUtils.h`
- `ResourceStateTracker.cpp`
- `ResourceStateTracker.h`
- `Sampler.h`
- `Std140Types.h`
- `Swapchain.h`
- `Texture.h`
- `TextureCreateInfo.h`
- `TextureUploadService.cpp`
- `TextureUploadService.h`

**`Shader/`**

- `GLSLProcessor.cpp`
- `ShaderInternal.h`
- `ShaderReflection.cpp`
- `SlangProcessor.cpp`

### ya-foundation-rhi-backend（Vulkan backend + VMA/STB 单头；OpenGL 保留不构建）

**`./`**

- `DescriptorSet.cpp`
- `FrameBuffer.cpp`
- `Pipeline.cpp`
- `Render.cpp`
- `RenderPass.cpp`
- `STB.cpp`
- `Swapchain.cpp`
- `Texture.cpp`

**`OpenGL/`**

- `OpenGLBuffer.cpp`
- `OpenGLBuffer.h`
- `OpenGLCommandBuffer.cpp`
- `OpenGLCommandBuffer.h`
- `OpenGLDescriptorSet.cpp`
- `OpenGLDescriptorSet.h`
- `OpenGLPipeline.cpp`
- `OpenGLPipeline.h`
- `OpenGLRender.cpp`
- `OpenGLRender.h`
- `OpenGLRenderPass.cpp`
- `OpenGLRenderPass.h`
- `OpenGLState.cpp`
- `OpenGLState.h`
- `OpenGLSwapchain.cpp`
- `OpenGLSwapchain.h`

**`Vulkan/`**

- `VulkanBuffer.cpp`
- `VulkanBuffer.h`
- `VulkanCommandBuffer.cpp`
- `VulkanCommandBuffer.h`
- `VulkanDescriptorSet.cpp`
- `VulkanDescriptorSet.h`
- `VulkanExt.cpp`
- `VulkanExt.h`
- `VulkanFrameBuffer.cpp`
- `VulkanFrameBuffer.h`
- `VulkanImage.cpp`
- `VulkanImage.h`
- `VulkanImageView.cpp`
- `VulkanImageView.h`
- `VulkanMemoryAllocator.cpp`
- `VulkanMemoryAllocator.h`
- `VulkanPipeline.cpp`
- `VulkanPipeline.h`
- `VulkanQueue.h`
- `VulkanRender.cpp`
- `VulkanRender.h`
- `VulkanRenderPass.cpp`
- `VulkanRenderPass.h`
- `VulkanRenderResourceFactory.cpp`
- `VulkanRenderResourceFactory.h`
- `VulkanSampler.cpp`
- `VulkanSampler.h`
- `VulkanSwapChain.cpp`
- `VulkanSwapChain.h`
- `VulkanUtils.cpp`
- `VulkanUtils.h`

### ya-gui-runtime（GUI 闭包：Draw2D/Resource/Scene/Compose）

**`Runtime/`**

- `UIBase.h`

**`Runtime/Compose/`**

- `Render2DComposePass.cpp`
- `Render2DComposePass.h`

**`Runtime/Draw2D/`**

- `Render2D.cpp`
- `Render2D.h`

**`Runtime/Resource/`**

- `FontManager.cpp`
- `FontManager.h`
- `TextureLibrary.cpp`
- `TextureLibrary.h`

**`Runtime/Scene/`**

- `Node.cpp`
- `Node.h`
- `Node2D.cpp`
- `Node2D.h`
- `UISceneRenderer.cpp`
- `UISceneRenderer.h`

### ya-scene-3d（Scene3D：Node3D）

**`Scene3D/`**

- `Node3D.cpp`
- `Node3D.h`

### ya-gameplay-ecs（ECS fat module）

**`./`**

- `Component.cpp`
- `Component.h`
- `ComponentMutation.h`
- `ECSRegistry.cpp` `[越界: ya-render-3d]`
- `ECSRegistry.h`
- `Entity.cpp` `[越界: ya-render-3d]`
- `Entity.h`
- `SceneBus.cpp`
- `SceneBus.h`

**`Component/`**

- `CameraComponent.cpp`
- `CameraComponent.h`
- `DirectionComponent.h`
- `DirectionalLightComponent.h`
- `LuaScriptComponent.cpp` `[越界: ya-resource]`
- `LuaScriptComponent.h` `[越界: ya-host, ya-render-3d]`
- `ManagedChildComponent.h`
- `MirrorComponent.h`
- `ModelComponent.cpp` `[越界: ya-render-3d]`
- `ModelComponent.h` `[越界: ya-resource]`
- `PointLightComponent.h`
- `RenderComponent.h`
- `ScriptComponent.h`
- `SkeletonAnimatorComponent.h` `[越界: ya-resource]`
- `TransformComponent.cpp`
- `TransformComponent.h`

**`Component/2D/`**

- `BillboardComponent.cpp` `[越界: ya-gui-runtime, ya-render-3d]`
- `BillboardComponent.h` `[越界: ya-render-3d]`
- `UIComponent.h` `[越界: ya-render-3d]`

**`Component/3D/`**

- `EnvironmentLightingComponent.cpp` `[越界: ya-resource]`
- `EnvironmentLightingComponent.h`
- `SkyboxComponent.cpp` `[越界: ya-resource]`
- `SkyboxComponent.h`

**`Component/Material/`**

- `MaterialComponent.cpp` `[越界: ya-render-3d]`
- `MaterialComponent.h` `[越界: ya-render-3d]`
- `PBRMaterialComponent.cpp` `[越界: ya-gui-runtime]`
- `PBRMaterialComponent.h` `[越界: ya-render-3d, ya-resource]`
- `PhongMaterialComponent.cpp` `[越界: ya-gui-runtime]`
- `PhongMaterialComponent.h` `[越界: ya-render-3d, ya-resource]`
- `SimpleMaterialComponent.h` `[越界: ya-render-3d]`
- `UnlitMaterialComponent.cpp` `[越界: ya-gui-runtime]`
- `UnlitMaterialComponent.h` `[越界: ya-render-3d]`

**`Component/Mesh/`**

- `MeshComponents.cpp`
- `MeshSource.cpp` `[越界: ya-resource]`
- `MeshSource.h` `[越界: ya-resource]`
- `SkinnedMeshComponent.h` `[越界: ya-resource]`
- `StaticMeshComponent.h` `[越界: ya-resource]`

**`Component/Terrain/`**

- `TerrainComponent.cpp`
- `TerrainComponent.h`

**`System/`**

- `ComponentLinkageSystem.cpp` `[职责混合: linkage framework + material/light rules；仍越界 ya-host]`
- `ComponentLinkageSystem.h` `[职责混合: linkage framework + material/light rules；仍越界 ya-host/render-3d]`
- `JSScriptingSystem.cpp` `[越界: ya-render-3d]`
- `JSScriptingSystem.h`
- `LuaScriptingSystem.cpp` `[越界: ya-host, ya-render-3d]`
- `LuaScriptingSystem.h`
- `ModelInstantiationSystem.cpp` `[越界: ya-gui-runtime, ya-host, ya-render-3d, ya-resource, ya-scene-3d]`
- `ModelInstantiationSystem.h`
- `RayCastMousePickingSystem.cpp` `[越界: ya-host, ya-render-3d, ya-resource]`
- `RayCastMousePickingSystem.h` `[越界: ya-host]`
- `ResourceResolveSystem.Detail.h`
- `ResourceResolveSystem.Environment.cpp` `[越界: ya-host, ya-render-3d]`
- `ResourceResolveSystem.Shared.cpp` `[越界: ya-host]`
- `ResourceResolveSystem.Skybox.cpp` `[越界: ya-host, ya-render-3d]`
- `ResourceResolveSystem.cpp` `[越界: ya-host, ya-render-3d]`
- `ResourceResolveSystem.h` `[越界: ya-host, ya-render-3d, ya-resource]`
- `ScriptingSystem.cpp`
- `ScriptingSystem.h`
- `TransformSystem.cpp` `[越界: ya-gui-runtime, ya-host, ya-render-3d, ya-scene-3d]`
- `TransformSystem.h`
- `WidgetRenderSystem.h`

**`System/CameraController/`**

- `FreeCameraController.cpp`
- `FreeCameraController.h`
- `OrbitCameraController.h`

**`System/Render/`**

- `IMaterialSystem.cpp`
- `IMaterialSystem.h`
- `IRenderSystem.cpp` `[越界: ya-host, ya-render-3d]`
- `IRenderSystem.h`

### ya-physics（Physics）

**`./`**

- `PhysicsBodyComponent.h`
- `PhysicsSystem.cpp` `[越界: ya-render-3d]`
- `PhysicsSystem.h` `[越界: ya-host]`

### ya-resource（Resource）

**`./`**

- `AssetManager.cpp` `[越界: ya-host]`
- `AssetManager.h`
- `AssetManagerTypes.h`
- `AssetRef.cpp`
- `EngineGeometryNormalizer.cpp`
- `EngineGeometryNormalizer.h`
- `EngineMeshData.h`
- `Mesh.cpp`
- `Mesh.h`
- `Model.cpp`
- `Model.h`
- `Skeleton.cpp`
- `Skeleton.h`
- `SkeletonAnimationSampler.cpp`
- `SkeletonAnimationSampler.h`

**`Handle/`**

- `PathRegistry.h`
- `ResourceHandle.h`
- `ResourceTable.h`

**`Manager/`**

- `AssetModelManager.cpp`
- `AssetModelManager.h`
- `AssetTextureManager.cpp`
- `AssetTextureManager.h`

**`Mesh/`**

- `PrimitiveGeometryFactory.cpp`
- `PrimitiveGeometryFactory.h`
- `PrimitiveMeshCache.cpp`
- `PrimitiveMeshCache.h`

**`Meta/`**

- `AssetMeta.cpp`
- `AssetMeta.h`

**`Model/`**

- `Animation.h`
- `AssimpImporter.cpp`
- `AssimpImporter.h`
- `GltfImporter.cpp`
- `GltfImporter.h`
- `IModelImporter.h`
- `ImportedAnimationData.h`
- `ImportedMeshData.h`
- `ImportedModelData.h`
- `ImportedSkeletonData.h`
- `MaterialData.h`
- `ModelImporterCommon.h`
- `ModelImporterRegistry.cpp`
- `ModelImporterRegistry.h`
- `TinyGLTF.cpp`
- `TinyGLTFSupport.h`

**`Texture/`**

- `AssetTextureHelpers.cpp`
- `AssetTextureImport.cpp`
- `AssetTextureInternal.h`
- `STBImage.h`

### ya-render-graph（RenderGraph）

**`./`**

- `RenderGraph.cpp`
- `RenderGraph.h`
- `RenderGraphExecutor.cpp`
- `RenderGraphExecutor.h`
- `RenderGraphImportUtils.cpp`
- `RenderGraphImportUtils.h`
- `RenderGraphPassBinding.cpp`
- `RenderGraphResourceRegistry.cpp`
- `RenderGraphResourceRegistry.h`

### ya-render-3d（Render3D）

**`./`**

- `BVH.h`
- `Octtree.h`
- `RenderFrameData.h`
- `RenderRuntime.Resources.cpp` `[越界: ya-host]`
- `RenderRuntime.cpp` `[越界: ya-host]`
- `RenderRuntime.h`
- `RenderRuntimeFrame.cpp` `[越界: ya-host]`
- `RenderRuntimeViewportDebug.cpp` `[越界: ya-host]`
- `RenderRuntimeViewportSnapshot.cpp`
- `Scene.cpp` `[越界: ya-host]`
- `Scene.h`
- `SceneManager.cpp`
- `SceneManager.h`
- `SceneSerializer.cpp`
- `SceneSerializer.h`

**`Common/`**

- `EntityIdViewportPass.cpp`
- `EntityIdViewportPass.h`
- `IRenderPipeline.h`
- `IRenderRuntimeServices.h`
- `PostProcessingStage.cpp`
- `PostProcessingStage.h`
- `PostProcessingState.h`
- `RenderOverlay.cpp`
- `RenderOverlay.h`
- `RenderTargetCatalog.h`
- `RenderViewportSnapshot.h`
- `RenderViewportUtils.h`

**`Common/Shadow/`**

- `ShadowFrameResources.cpp`
- `ShadowFrameResources.h`
- `ShadowGraphOutputs.h`
- `ShadowStage.cpp`
- `ShadowStage.h`
- `ShadowTypes.h`

**`Common/Shadow/BasicShadowMap/`**

- `BasicShadowMapTechnique.cpp`
- `BasicShadowMapTechnique.h`
- `BasicShadowPayload.h`
- `DirectionalShadowPass.cpp`
- `DirectionalShadowPass.h`
- `PointShadowBufferUtils.h`
- `PointShadowCullPass.cpp`
- `PointShadowCullPass.h`
- `PointShadowIndirectRenderer.cpp`
- `PointShadowIndirectRenderer.h`
- `PointShadowPass.cpp`
- `PointShadowPass.h`

**`Common/Shadow/Common/`**

- `DirectionalShadowMath.cpp`
- `DirectionalShadowMath.h`
- `ShadowDrawHelper.cpp`
- `ShadowDrawHelper.h`
- `ShadowMapResources.cpp`
- `ShadowMapResources.h`
- `ShadowRuntimeState.h`
- `ShadowSettingsConfig.cpp` `[越界: ya-host]`
- `ShadowSettingsConfig.h`
- `ShadowViewBuilder.cpp`
- `ShadowViewBuilder.h`

**`Debug/`**

- `PhysicsDebugDraw.cpp`
- `PhysicsDebugDraw.h`

**`Deferred/`**

- `DeferredAttachmentFormats.h`
- `DeferredFrameGraphOrchestrator.cpp`
- `DeferredFrameGraphOrchestrator.h`
- `DeferredFrameGraphPasses.cpp`
- `DeferredFrameGraphPasses.h`
- `DeferredFrameGraphResources.h`
- `DeferredFrameResourceSet.cpp`
- `DeferredFrameResourceSet.h`
- `DeferredGBufferResources.h`
- `DeferredPipelineDebugViews.h`
- `DeferredRenderPipeline.cpp` `[越界: ya-host]`
- `DeferredRenderPipeline.h`
- `DeferredViewportResources.h`
- `GBufferStage.cpp`
- `GBufferStage.h`
- `LightStage.cpp` `[越界: ya-host]`
- `LightStage.h`
- `SSAOStage.cpp` `[越界: ya-host]`
- `SSAOStage.h`
- `ViewportOverlayStage.cpp`
- `ViewportOverlayStage.h`

**`Forward/`**

- `ForwardFrameGraphOrchestrator.cpp`
- `ForwardFrameGraphOrchestrator.h`
- `ForwardFrameGraphPasses.cpp`
- `ForwardFrameGraphPasses.h`
- `ForwardFrameGraphResources.h`
- `ForwardFrameResourceSet.cpp`
- `ForwardFrameResourceSet.h`
- `ForwardRenderPipeline.cpp`
- `ForwardRenderPipeline.h`
- `ForwardViewportAuxPasses.cpp`
- `ForwardViewportAuxPasses.h`
- `ForwardViewportLitPasses.cpp` `[越界: ya-host]`
- `ForwardViewportLitPasses.h`
- `ForwardViewportResources.h`
- `ForwardViewportStage.cpp`
- `ForwardViewportStage.h`
- `ForwardViewportUnlitPass.cpp`
- `ForwardViewportUnlitPass.h`

**`Material/`**

- `Material.cpp`
- `Material.h`
- `MaterialDescPool.h`
- `MaterialFactory.cpp`
- `MaterialFactory.h`
- `PBRMaterial.h`
- `PhongMaterial.h`
- `SimpleMaterial.h`
- `UnlitMaterial.cpp`
- `UnlitMaterial.h`

**`Pipelines/`**

- `BasicPostprocessing.cpp`
- `BasicPostprocessing.h`
- `BloomPostprocessing.cpp`
- `BloomPostprocessing.h`
- `CubeMap2PBRIrradianceMap.cpp`
- `CubeMap2PBRIrradianceMap.h`
- `CubeMap2PBRPrefilteredEnv.cpp`
- `CubeMap2PBRPrefilteredEnv.h`
- `DebugPrimitives.cpp`
- `DebugPrimitives.h`
- `DebugSkinning.h`
- `EquidistantCylindrical2CubeMap.cpp`
- `EquidistantCylindrical2CubeMap.h`
- `PBRGenerateBrdfLUT.cpp`
- `PBRGenerateBrdfLUT.h`

**`Services/`**

- `DebugRenderSystem.cpp`
- `DebugRenderSystem.h`
- `OffscreenTaskService.cpp` `[越界: ya-host]`
- `OffscreenTaskService.h`
- `RenderDiagnosticsService.cpp` `[越界: ya-host]`
- `RenderDiagnosticsService.h`
- `RenderSharedResourceProvider.cpp` `[越界: ya-host]`
- `RenderSharedResourceProvider.h`

**`Shadow/`**

- `IShadowTechnique.h`
- `ShadowSettings.h`

**`Stage/`**

- `IRenderStage.h`

**`Systems/Animation/`**

- `AnimationSystem.cpp` `[越界: ya-host]`
- `AnimationSystem.h`

**`Terrain/`**

- `TerrainMeshBuilder.cpp`
- `TerrainMeshBuilder.h`

### ya-host（Host）

**`./`**

- `App.cpp`
- `App.h`
- `AppContext.h`
- `AppEvent.h`
- `AppOptions.cpp`
- `AppOptions.h`
- `AppRenderFrameState.h`
- `AppRenderServices.cpp`
- `AppRenderServices.h`
- `AppRenderState.h`
- `AppSceneServices.cpp`
- `AppSceneServices.h`
- `AppState.h`
- `AppTaskManager.h`
- `InputRouter.cpp`
- `InputRouter.h`
- `Profiling.cpp`
- `ScriptApiAsset.cpp`
- `ScriptApiCore.cpp`
- `Switcher.h`
- `WindowManager.cpp`
- `WindowManager.h`

**`Automation/`**

- `AppAutomationControlService.cpp`
- `AppAutomationControlService.h`
- `EditorAutomationControl.h`

**`Bootstrap/`**

- `AutomationSceneBootstrapModule.cpp`
- `AutomationSceneBootstrapModule.h`

**`Config/`**

- `ConfigManager.cpp`
- `ConfigManager.h`

**`GUI/`**

- `GuiBackend.h`
- `GuiSystem.cpp`
- `GuiSystem.h`

**`GUI/ImGui/`**

- `ImGuiSystem.cpp`
- `ImGuiSystem.h`

**`GUI/ImGui/Backend/`**

- `ImGuiManager.Backend.Vulkan.cpp`

**`GUI/ImGui/ImageCache/`**

- `ImGuiImageCache.cpp`

**`Lifecycle/`**

- `AppAutomation.cpp`
- `AppAutomation.h`
- `AppEventRouter.cpp`
- `AppEventRouter.h`
- `AppFrameLoop.cpp`
- `AppFrameLoop.h`
- `AppLifecycle.cpp`
- `AppLifecycle.h`

**`Network/`**

- `NetDriver.h`

**`Utility/`**

- `AppScreenshotCapture.cpp`
- `AppScreenshotCapture.h`
- `ClLIParams.h`
- `FPSCtrl.cpp`
- `FPSCtrl.h`
- `OffscreenJobRunner.cpp`
- `OffscreenJobRunner.h`
- `RenderFrameExtractor.cpp`
- `RenderFrameExtractor.h`
- `SDLMisc.h`

**`Window/`**

- `WindowsDialogWindow.cpp`
- `WindowsDialogWindow.h`

### ya-editor（Editor）

**`./`**

- `EditorCommon.h`
- `EditorLayer.cpp`
- `EditorLayer.h`
- `EditorLayerInternal.h`
- `EditorModule.cpp`
- `EditorModule.h`
- `EditorModuleEntry.cpp`
- `EditorPlaySession.cpp`
- `EditorPlaySession.h`
- `EditorProfilingSettings.cpp`
- `EditorProfilingSettings.h`
- `EditorRuntimeSettings.cpp`
- `EditorRuntimeSettings.h`
- `FileExplorer.Navigation.cpp`
- `FileExplorer.Render.cpp`
- `FileExplorer.State.cpp`
- `FileExplorer.cpp`
- `FileExplorer.h`
- `FileExplorerInternal.h`
- `FilePicker.cpp`
- `FilePicker.h`
- `IconsMaterialSymbols.h`

**`Asset/`**

- `AssetBase.h`
- `MaterialAsset.h`

**`Debug/`**

- `EditorLayer.Debug.cpp`

**`ImGui/`**

- `ImGuiHelper.h`

**`ImGui/Widgets/`**

- `ImGuiWidgets.cpp`

**`Input/`**

- `EditorInputNode.cpp`
- `EditorInputNode.h`

**`Inspector/`**

- `ContainerPropertyRenderer.h`
- `DetailsView.cpp`
- `DetailsView.h`
- `DetailsViewInternal.h`
- `ReflectionCache.cpp`
- `ReflectionCache.h`
- `TypeRenderer.cpp`
- `TypeRenderer.h`

**`Inspector/Components/`**

- `DetailsView.Components.Basic.cpp`
- `DetailsView.Components.Environment.cpp`
- `DetailsView.Components.Skybox.cpp`

**`Inspector/Reflection/`**

- `DetailsView.Reflection.cpp`

**`Inspector/Script/`**

- `DetailsView.Components.Script.cpp`

**`Interaction/`**

- `EditorLayer.Interaction.cpp`

**`Layout/`**

- `EditorLayer.Layout.cpp`

**`Panels/`**

- `AssetInspectorPanel.cpp`
- `AssetInspectorPanel.h`
- `ContentBrowserPanel.cpp`
- `ContentBrowserPanel.h`
- `RenderTargetInspector.cpp`
- `RenderTargetInspector.h`
- `RuntimeToolsPanel.cpp`
- `RuntimeToolsPanel.h`
- `RuntimeToolsPanelInternal.h`
- `SceneHierarchyPanel.cpp`
- `SceneHierarchyPanel.h`

**`Panels/RuntimeTools/`**

- `RuntimeToolsPanel.Diagnostics.cpp`
- `RuntimeToolsPanel.Profiling.cpp`
- `RuntimeToolsPanel.Rendering.cpp`
- `RuntimeToolsPanel.Session.cpp`

**`Resource/`**

- `AssetFile.h`

**`Resources/`**

- `EditorLayer.Textures.cpp`

**`Services/`**

- `NodeCreateRegistry.cpp`
- `NodeCreateRegistry.h`

**`Startup/`**

- `EditorLayer.Startup.cpp`

**`Viewport/`**

- `EditorLayer.Viewport.cpp`
