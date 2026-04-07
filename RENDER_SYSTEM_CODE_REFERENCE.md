# PhongMaterialSystem - Detailed Code Reference with Line Numbers

## FILE LOCATIONS
- PhongMaterialSystem.h: Engine/Source/ECS/System/Render/PhongMaterialSystem.h
- PhongMaterialSystem.cpp: Engine/Source/ECS/System/Render/PhongMaterialSystem.cpp
- MeshComponent.h: Engine/Source/ECS/Component/MeshComponent.h
- PhongMaterialComponent.h: Engine/Source/ECS/Component/Material/PhongMaterialComponent.h

## QUERY - LINE 313 (PhongMaterialSystem.cpp)

THE CRITICAL QUERY:
```cpp
auto view = scene->getRegistry().view<PhongMaterialComponent, MeshComponent, TransformComponent>();
```

What it does:
- Queries EnTT registry for all entities with ALL three components
- Returns a view object that can be iterated
- Early exit if no entities match (line 314-316)

## FIRST ITERATION - LINES 350-365 (PhongMaterialSystem.cpp)

Iterate to build sorted list:
```cpp
for (const auto& [entity, lmc, mc, tc] : view.each()) {
    entries.emplace_back(entity, &tc);
}
```

Structured binding unpacks components in query order:
- entity: EnTT entity ID
- lmc: PhongMaterialComponent (first in query)
- mc: MeshComponent (second in query)
- tc: TransformComponent (third in query)

Sort by depth (line 362-364):
```cpp
std::ranges::sort(entries, [](const auto& a, const auto& b) {
    return a.second->getWorldPosition().z < b.second->getWorldPosition().z;
});
```

## MAIN RENDER LOOP - LINES 372-428 (PhongMaterialSystem.cpp)

Loop header (line 372-374):
```cpp
for (auto& [entity, tc] : entries) {
    const auto& [lmc, meshComp, tc2] = view.get(entity);
```

Get runtime material (line 379):
```cpp
PhongMaterial* material = lmc.getMaterial();
```

Lazy update descriptor sets (lines 387-401):
```cpp
uint32_t materialInstanceIndex = material->getIndex();
DescriptorSetHandle resourceDS = _matPool.resourceDS(materialInstanceIndex);
DescriptorSetHandle paramDS = _matPool.paramDS(materialInstanceIndex);

if (!updatedMaterial[materialInstanceIndex]) {
    _matPool.flushDirty(material, _bDescriptorPoolRecreated, 
        [&](IBuffer* ubo, PhongMaterial* mat) { updateMaterialParamUBO(ubo, mat); },
        [&](DescriptorSetHandle ds, PhongMaterial* mat) { updateMaterialResourceDS(ds, mat); });
    updatedMaterial[materialInstanceIndex] = true;
}
```

Bind descriptor sets (lines 405-412):
```cpp
cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {
    _currentScenePassResources->frameDS,  // Set 0
    resourceDS,                           // Set 1
    paramDS,                              // Set 2
    skyboxDS,                             // Set 3
    depthBufferDS,                        // Set 4
});
```

Push constants (lines 415-418):
```cpp
PhongMaterialSystem::ModelPushConstant pushConst{
    .modelMat = tc->getTransform(),
};
cmdBuf->pushConstants(_pipelineLayout.get(), ..., &pushConst);
```

Draw mesh (lines 423-426):
```cpp
Mesh* mesh = meshComp.getMesh();
if (mesh) {
    mesh->draw(cmdBuf);
}
```

## COMPONENT STRUCTURE

MeshComponent (MeshComponent.h, line 48):
```cpp
struct MeshComponent : public IComponent {
    Mesh *_cachedMesh = nullptr;
    bool  _bResolved  = false;
    Mesh *getMesh() const { return _cachedMesh; }
};
```

PhongMaterialComponent (PhongMaterialComponent.h, line 40):
```cpp
struct PhongMaterialComponent : public MaterialComponent<PhongMaterial> {
    AuthoringParams _params;
    TextureSlot _diffuseSlot;
    TextureSlot _specularSlot;
    TextureSlot _reflectionSlot;
    TextureSlot _normalSlot;
};
```

## UNLIT MATERIAL SYSTEM - COMPARISON

Query (UnlitMaterialSystem.cpp, line 265):
```cpp
auto view = scene->getRegistry().view<UnlitMaterialComponent, MeshComponent, TransformComponent>();
```

Draw loop (line 293):
```cpp
for (entt::entity entity : view) {
    const auto& [umc, meshComp, tc] = view.get(entity);
```

Key difference: Direct entity iteration instead of sorting

Descriptor sets (lines 314-318):
```cpp
std::vector<DescriptorSetHandle> descSets{
    _frameDSs[getSlot()],
    paramDS,
    resourceDS,
};
```

Only 3 sets (no skybox, no shadow maps)

## SIMPLE MATERIAL SYSTEM - MOST DIFFERENT

Queries (SimpleMaterialSystem.cpp, lines 180-182):
```cpp
const auto& view1 = scene->getRegistry().view<TransformComponent, SimpleMaterialComponent, MeshComponent>();
const auto& view2 = scene->getRegistry().view<TransformComponent, DirectionComponent>();
```

TWO separate queries!

Iteration (line 224):
```cpp
view1.each([this, &cmdBuf](auto& tc, auto& smc, auto& mc) {
    // Lambda-based iteration
});
```

Descriptor sets: NONE! Only push constants used.

## SYSTEM COORDINATION

Registration (ForwardRenderPipeline.cpp, lines 243-347):
Each system created independently:
- simpleMaterialSystem (line 243)
- unlitMaterialSystem (line 255)
- phongMaterialSystem (line 267)
- debugRenderSystem (line 289)
- skyboxSystem (line 301)
- shadowMappingSystem (line 313)

Execution (ForwardRenderPipeline.cpp, lines 585-594):
```cpp
void ForwardRenderPipeline::renderScene(ICommandBuffer* cmdBuf, float dt, FrameContext& ctx,
    const stdptr<PhongScenePassResources>& phongScenePassResources) {
    simpleMaterialSystem->tick(cmdBuf, dt, &ctx);
    unlitMaterialSystem->tick(cmdBuf, dt, &ctx);
    phongMaterialSystem->as<PhongMaterialSystem>()->setScenePassResources(phongScenePassResources);
    phongMaterialSystem->tick(cmdBuf, dt, &ctx);
    debugRenderSystem->tick(cmdBuf, dt, &ctx);
    skyboxSystem->tick(cmdBuf, dt, &ctx);
}
```

Order: Simple -> Unlit -> Phong -> Debug -> Skybox

