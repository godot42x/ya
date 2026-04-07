# Mesh Component Architecture Analysis

## CORE FILES & LINE NUMBERS

### Components
1. **MeshComponent** - Engine/Source/ECS/Component/MeshComponent.h (lines 48-160)
2. **ModelComponent** - Engine/Source/ECS/Component/ModelComponent.h (lines 47-143)  
3. **PhongMaterialComponent** - Engine/Source/ECS/Component/Material/PhongMaterialComponent.h (lines 40-213)
4. **MaterialComponent (base)** - Engine/Source/ECS/Component/Material/MaterialComponent.h (lines 38-134)
5. **TransformComponent** - Engine/Source/ECS/Component/TransformComponent.h (lines 30+)

### GPU Resources
6. **Mesh** - Engine/Source/Render/Mesh.h (56 lines), Mesh.cpp (71 lines)
   - Mesh::draw() at line 61-67 in cpp
7. **Vertex** - Engine/Source/Core/Math/Geometry.h (lines 59-65)
8. **Model** - Engine/Source/Render/Model.h (lines 229-313)
9. **MaterialData** - Engine/Source/Render/Model.h (lines 103-179)

### Systems
10. **ModelInstantiationSystem** - 
    - Header: Engine/Source/ECS/System/ModelInstantiationSystem.h (50 lines)
    - Implementation: Engine/Source/ECS/System/ModelInstantiationSystem.cpp (229 lines)
    - createMeshNode() at line 115-152

11. **ResourceResolveSystem** -
    - Header: Engine/Source/ECS/System/ResourceResolveSystem.h (50 lines)
    - Implementation: Engine/Source/ECS/System/ResourceResolveSystem.cpp (200+ lines)
    - resolvePendingMeshes() at line 123-133
    - resolvePendingMaterials() at line 135-167

12. **PhongMaterialSystem** -
    - Header: Engine/Source/ECS/System/Render/PhongMaterialSystem.h (360 lines)
    - Implementation: Engine/Source/ECS/System/Render/PhongMaterialSystem.cpp (550+ lines)
    - onRender() at line 293-444

### Runtime Materials
13. **PhongMaterial** - Engine/Source/Render/Material/PhongMaterial.h (100+ lines)

---

## MeshComponent Details (Single Mesh Per Entity)

### Fields
```
Serializable:
- EPrimitiveGeometry _primitiveGeometry
- std::string _sourceModelPath  
- uint32_t _meshIndex

Runtime:
- Mesh *_cachedMesh
- bool _bResolved
```

### Two Modes
1. Primitive: Set via setPrimitiveGeometry(), resolved from PrimitiveMeshCache
2. Model Mesh: Set via setFromModel() by ModelInstantiationSystem

---

## Mesh GPU Structure

```cpp
struct Mesh {
    std::shared_ptr<IBuffer> _vertexBuffer;
    std::shared_ptr<IBuffer> _indexBuffer;
    uint32_t _indexCount;
    uint32_t _vertexCount;
    AABB boundingBox;
    
    void draw(ICommandBuffer *cmdBuf) const {
        cmdBuf->bindVertexBuffer(0, _vertexBuffer.get(), 0);
        cmdBuf->bindIndexBuffer(_indexBuffer.get(), 0, false);
        cmdBuf->drawIndexed(_indexCount, 1, 0, 0, 0);
    }
};
```

---

## Vertex Format (44 bytes)
```
Offset 0: glm::vec3 position
Offset 12: glm::vec2 texCoord0  
Offset 20: glm::vec3 normal
Offset 32: glm::vec3 tangent
```

---

## PhongMaterialComponent

### Serializable Fields
- EMaterialResolveState _resolveState
- AuthoringParams _params (ambient, diffuse, specular, shininess)
- TextureSlot _diffuseSlot, _specularSlot, _reflectionSlot, _normalSlot

### Inherited (from template base)
- PhongMaterial* _material
- bool _bSharedMaterial

---

## Model Asset Structure

```cpp
struct Model {
    std::vector<stdptr<Mesh>> meshes;  // GPU resources
    std::vector<MaterialData> embeddedMaterials;
    std::vector<int32_t> meshMaterialIndices;  // mesh[i] -> material[j]
};
```

---

## Rendering Pipeline (PhongMaterialSystem)

### Descriptor Sets
- Set 0: Frame UBO, Light UBO, Debug UBO
- Set 1: Diffuse, Specular, Reflection, Normal textures (per-material)
- Set 2: Material params UBO (per-material)
- Set 3: Skybox cubemap
- Set 4: Shadow maps

### Vertex Attributes
- Location 0: position (vec3, offset 0)
- Location 1: texCoord0 (vec2, offset 12)
- Location 2: normal (vec3, offset 20)
- Location 3: tangent (vec3, offset 32)

### Per-Frame Render Loop
```
1. Query: view<PhongMaterialComponent, MeshComponent, TransformComponent>
2. Sort by Z (painter's algorithm)
3. For each entity:
   - Bind pipeline
   - Bind 5 descriptor sets
   - Push model matrix
   - mesh->draw(cmdBuf)
```

---

## System Pipeline

### Frame 1: ModelInstantiationSystem
- Detects unresolved ModelComponent
- Creates child entity per mesh
- Adds MeshComponent + PhongMaterialComponent
- Builds shared material instances

### Frame 2: ResourceResolveSystem  
- Resolves MeshComponent.resolve()
- Resolves PhongMaterialComponent.resolve()

### Frame 3+: PhongMaterialSystem
- Renders all entities with components
- Per-entity: update + draw

---

## Key Insights

### What Data Renderer Needs Per Mesh
1. Geometry: Vertex buffer, index buffer, index count
2. Transform: Model matrix (from TransformComponent)
3. Material: Colors + 4 textures (from PhongMaterialComponent)
4. Frame: View/proj, lights, shadows, skybox (global)

### Material Sharing
- ModelInstantiationSystem::buildSharedMaterials() creates one PhongMaterial per material in model
- Stored in ModelComponent._cachedMaterials[materialIndex]
- Multiple meshes reference same material instance

### Entity Explosion Problem
- Model with 10 meshes = 1 parent + 10 child entities = 11 total
- Each child has full ECS component set (cache inefficient)
- Each child = separate draw call (no batching)

### Current Strengths
- Clean separation: mesh geometry / material params / transform
- Component-oriented: standard ECS patterns apply
- Material sharing: multiple meshes -> single material
- Serialization: child entities marked dontSerialize

---

## StaticMeshComponent Design Implications

### Should Support
1. Multiple mesh sections per entity
2. Each section: Mesh* + Material* reference
3. Material sharing (sections reference shared material instances)
4. Transform hierarchy (parent affects all sections)
5. Direct serialization (no child entity recreation)

### Must Handle
1. Rendering: Iterate sections, not entities
2. Material resolution: Still per-component
3. Backward compatibility: Existing serialized data
4. Editor: Can assign StaticMeshComponent directly

