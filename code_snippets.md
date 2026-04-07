# Key Code Snippets - Mesh Rendering Pipeline

## 1. MeshComponent::resolve() - Loading Mesh Resources
File: Engine/Source/ECS/Component/MeshComponent.cpp (lines 12-59)

Loads from either primitive cache or model asset, sets _cachedMesh and _bResolved

## 2. Mesh::draw() - GPU Draw Call  
File: Engine/Source/Render/Mesh.cpp (lines 61-67)

```
bindVertexBuffer(0, _vertexBuffer, 0)
bindIndexBuffer(_indexBuffer, 0, false)
drawIndexed(_indexCount, 1, 0, 0, 0)
```

## 3. ModelInstantiationSystem::createMeshNode()
File: Engine/Source/ECS/System/ModelInstantiationSystem.cpp (lines 115-152)

Creates child entity with:
- MeshComponent (references model mesh)
- PhongMaterialComponent (linked to shared material)
- setDontSerialize(true) to prevent serialization

## 4. ModelInstantiationSystem::buildSharedMaterials()
File: Engine/Source/ECS/System/ModelInstantiationSystem.cpp (lines 154-170)

Creates ONE PhongMaterial per unique material in model file
Stores in ModelComponent._cachedMaterials[matIndex]
Multiple meshes reference same instance

## 5. ResourceResolveSystem::resolvePendingMeshes()
File: Engine/Source/ECS/System/ResourceResolveSystem.cpp (lines 123-133)

Iterates all MeshComponent entities
Calls resolve() on unresolved ones

## 6. PhongMaterialSystem::onRender() - Main Render Loop
File: Engine/Source/ECS/System/Render/PhongMaterialSystem.cpp (lines 293-444)

1. Query view<PhongMaterialComponent, MeshComponent, TransformComponent>
2. Sort by Z coordinate
3. For each entity:
   - Bind pipeline
   - Bind 5 descriptor sets (frame, textures, params, skybox, shadows)
   - Push model matrix constant
   - mesh->draw(cmdBuf)

## Descriptor Sets (5 total)
- Set 0: Frame UBO, Light UBO, Debug UBO
- Set 1: Diffuse, Specular, Reflection, Normal textures
- Set 2: Material parameters (ambient, diffuse, specular, shininess)
- Set 3: Skybox cubemap
- Set 4: Directional + Point shadow maps

## Vertex Attributes (4 locations)
- Location 0: position (vec3, offset 0)
- Location 1: texCoord0 (vec2, offset 12)
- Location 2: normal (vec3, offset 20)
- Location 3: tangent (vec3, offset 32)

## PhongMaterial ParamUBO Structure
alignas(16) glm::vec3 ambient
alignas(16) glm::vec3 diffuse
alignas(16) glm::vec3 specular
alignas(4) float shininess
array<TextureParam, 4> textureParams

## Model Structure
vector<Mesh> meshes - GPU geometry
vector<MaterialData> embeddedMaterials - Material data from file
vector<int32_t> meshMaterialIndices - Mesh to material mapping
