# PhongMaterialSystem Rendering Pipeline Analysis

## Executive Summary

The PhongMaterialSystem renders entities using EnTT's view system to query entities with multiple components:

```cpp
auto view = scene->getRegistry().view<PhongMaterialComponent, MeshComponent, TransformComponent>();
```

This retrieves all entities that have ALL THREE components.

## 1. COMPONENT QUERY PATTERN

### PhongMaterialSystem Query (Line 313)
File: Engine/Source/ECS/System/Render/PhongMaterialSystem.cpp

```cpp
auto view = scene->getRegistry().view<PhongMaterialComponent, MeshComponent, TransformComponent>();
if (view.begin() == view.end()) {
    return;
}
```

### UnlitMaterialSystem Query (Line 265)
File: Engine/Source/ECS/System/Render/UnlitMaterialSystem.cpp

```cpp
auto view = scene->getRegistry().view<UnlitMaterialComponent, MeshComponent, TransformComponent>();
```

### SimpleMaterialSystem Query (Lines 180-184)
File: Engine/Source/ECS/System/Render/SimpleMaterialSystem.cpp

```cpp
const auto& view1 = scene->getRegistry().view<TransformComponent, SimpleMaterialComponent, MeshComponent>();
const auto& view2 = scene->getRegistry().view<TransformComponent, DirectionComponent>();
```

## 2. THE PHONG MATERIAL SYSTEM DRAW LOOP

### onRender() - Lines 293-444

Complete draw loop structure:

Line 313: Query entities
Line 318: Bind pipeline
Line 335-336: Set viewport and scissor
Line 339: Update frame data UBO

Lines 348-365: Sort entities by Z depth
- Line 350/358: Iterate through view with structured binding
- Line 362-364: Sort by world position Z

Lines 372-428: Main render loop
- Line 374: Get components via view.get(entity)
- Line 379: Get runtime material instance
- Lines 387-401: Update material descriptor sets if dirty
- Lines 405-412: Bind 5 descriptor sets
- Lines 415-418: Push model matrix as push constant
- Lines 423-426: Draw mesh

## 3. HOW IT GETS MESH/MATERIAL/TRANSFORM

### Component Unpacking (Line 350/358)
```cpp
for (const auto& [entity, lmc, mc, tc] : view.each()) {
    // Automatically unpacks all components
    // lmc = PhongMaterialComponent
    // mc = MeshComponent
    // tc = TransformComponent
}
```

### Get Components for Specific Entity (Line 374)
```cpp
const auto& [lmc, meshComp, tc2] = view.get(entity);
```

### Get Mesh Pointer (Line 423)
```cpp
Mesh* mesh = meshComp.getMesh();
```

## 4. DESCRIPTOR SET BINDING (Lines 405-412)

Five descriptor sets bound per draw call:

Set 0: Frame data (projection, view, camera)
Set 1: Material resources (textures)
Set 2: Material parameters (colors, shininess)
Set 3: Skybox cubemap
Set 4: Shadow maps (directional + point)

## 5. MULTIPLE RENDER SYSTEMS COORDINATION

### Execution Order in renderScene()
File: Engine/Source/Runtime/App/ForwardRender/ForwardRenderPipeline.cpp, Lines 585-594

```cpp
simpleMaterialSystem->tick(cmdBuf, dt, ctx);
unlitMaterialSystem->tick(cmdBuf, dt, ctx);
phongMaterialSystem->tick(cmdBuf, dt, ctx);
debugRenderSystem->tick(cmdBuf, dt, ctx);
skyboxSystem->tick(cmdBuf, dt, ctx);
```

Sequential execution:
1. SimpleMaterialSystem - Basic colored geometry
2. UnlitMaterialSystem - Unlit textured geometry
3. PhongMaterialSystem - Lit geometry with shadows
4. DebugRenderSystem - Debug visualization
5. SkyboxSystem - Skybox rendering

## 6. KEY FINDINGS

YES - Component query pattern: registry.view<MaterialComponent, MeshComponent, TransformComponent>()

All three material systems use this pattern for querying entities.

YES - Multiple render systems are coordinated sequentially.

Each system queries its own entities and renders them completely before the next system runs.

NO - There is no RenderComponent.

Mesh and Material are separate components. They are queried together but stored independently.

Material + Mesh binding retrieves components via view.get(entity):
- material->getMaterial() gets the runtime material instance
- mesh.getMesh() gets the mesh pointer
- transform.getTransform() gets the model matrix

## COMPLETE DRAW LOOP PATTERN

1. Query: view = registry.view<PhongMaterialComponent, MeshComponent, TransformComponent>()
2. Sort entities by depth (optional)
3. For each entity:
   a. Get all three components via view.get(entity)
   b. Update descriptor sets if material is dirty
   c. Bind 5 descriptor sets
   d. Push model matrix as push constant
   e. Draw mesh

