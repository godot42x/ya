# Feature Specification: Core Systems Development

**Feature ID**: 001  
**Feature Name**: Core Systems Development  
**Created**: 2025-11-05  
**Status**: Planning

## Overview

Development of essential engine systems to transform Neon Engine from a rendering prototype into a full-featured game engine. This includes the Material System, Reflection System, Scene System, GUI Framework, and Editor infrastructure.

## Goals

### Primary Goals
1. **Material System**: Unified material abstraction supporting custom shaders, properties, and multi-backend rendering
2. **Reflection System**: Runtime type information and serialization for engine objects
3. **Scene System**: Hierarchical scene graph with efficient spatial queries and serialization
4. **GUI Framework**: Immediate-mode UI system for in-game UI and editor interfaces
5. **Editor**: Visual scene editor with asset management and live preview

### Success Criteria
- Material system supports at least 3 material types (Unlit, Lit, PBR) across Vulkan backend
- Reflection system enables property inspection and serialization for all engine objects
- Scene system handles hierarchical transforms with 10k+ entities at 60fps
- GUI framework renders ImGui-style interfaces with custom styling
- Editor allows scene creation, entity manipulation, and material assignment

## Requirements

### Functional Requirements

#### FR-1: Material System
- **FR-1.1**: Define abstract material interface independent of graphics API
- **FR-1.2**: Support shader compilation and reflection (GLSL → SPIR-V)
- **FR-1.3**: Material properties bindable to uniform buffers and textures
- **FR-1.4**: Material inheritance and property overrides
- **FR-1.5**: Hot-reload shaders in editor mode

#### FR-2: Reflection System
- **FR-2.1**: Register classes, properties, and methods at compile time
- **FR-2.2**: Query type information at runtime (type name, size, properties)
- **FR-2.3**: Serialize/deserialize objects to JSON or binary format
- **FR-2.4**: Invoke methods via string names for scripting support
- **FR-2.5**: Property metadata (min/max, editor hints, categories)

#### FR-3: Scene System
- **FR-3.1**: Scene graph with parent-child relationships
- **FR-3.2**: Local and world transform calculations
- **FR-3.3**: Spatial queries (raycast, frustum culling, nearest neighbors)
- **FR-3.4**: Scene serialization to file format (.scene, .prefab)
- **FR-3.5**: Runtime scene loading and unloading

#### FR-4: GUI Framework
- **FR-4.1**: Immediate-mode API for UI construction
- **FR-4.2**: Core widgets (button, slider, text input, tree view, dockspace)
- **FR-4.3**: Custom rendering backend using engine's render API
- **FR-4.4**: Input handling and event propagation
- **FR-4.5**: Skinning and theming support

#### FR-5: Editor
- **FR-5.1**: Scene hierarchy panel showing entity tree
- **FR-5.2**: Inspector panel displaying entity properties
- **FR-5.3**: Viewport panel with scene preview and camera controls
- **FR-5.4**: Asset browser for meshes, textures, materials
- **FR-5.5**: Entity manipulation (create, delete, parent, transform gizmos)

### Non-Functional Requirements

#### NFR-1: Performance
- Material system: < 1ms overhead per material switch
- Reflection: < 100ns property access via reflection
- Scene system: Support 10,000 entities with < 2ms transform update
- GUI: Render 1000+ widgets at 60fps without frame drops
- Editor: Maintain 60fps with typical scene (1000 entities)

#### NFR-2: Memory
- Reflection metadata: < 1KB per registered class
- Scene graph: < 256 bytes overhead per entity
- GUI: < 50MB for typical editor UI state
- Editor: Total memory budget < 2GB for medium projects

#### NFR-3: Usability
- Material system: API similar to Unity/Unreal for familiarity
- Reflection: Macro-based registration, minimal boilerplate
- Scene system: Intuitive parent-child API
- GUI: Ergonomic immediate-mode API like Dear ImGui
- Editor: Standard editor controls (W/E/R for translate/rotate/scale)

#### NFR-4: Maintainability
- All systems follow Platform Abstraction principle
- Comprehensive unit tests (> 80% coverage)
- API documentation with usage examples
- Clean separation between editor and runtime code

## Technical Approach

### Material System Architecture
```
IMaterial (interface)
  ├── Material (base implementation)
  │   ├── Properties (name-value map)
  │   ├── Shader (IShader*)
  │   └── Descriptor Sets
  └── Subclasses: UnlitMaterial, PhongMaterial, PBRMaterial

MaterialSystem (ECS)
  ├── Material Database
  ├── Shader Compiler
  └── Descriptor Pool Management
```

### Reflection System Design
```cpp

// // Runtime usage
// TypeInfo* type = Reflection::GetType("Entity");
// Property* prop = type->GetProperty("name");
// prop->SetValue(entity, "MyEntity");

// Property mark, use lib-clang or self manage tokenizer to detect

[[ya::class]]
class A{

    [[ya::prop]]
    int a;

    [[ya::func]]
    std::vector<int> arr;

};

// Then generate the reflecting codes
registry(A)  
  .property(int )
  PROPERTY(std::string, name, Category="Basic", Hint="EntityName")
  PROPERTY(Transform, transform, Category="Transform")
  METHOD(void, SetActive, bool active)
END_CLASS()

```

### Scene System Structure
```
Scene
  ├── Root Entity (implicit)
  │   ├── Camera Entity
  │   ├── Light Entity
  │   └── Model Entity
  │       ├── Child Mesh 1
  │       └── Child Mesh 2
  └── Systems (ECS context)
```

### GUI Framework Layers
```
GUI Layer 1: Core (Widget tree, layout, input)
GUI Layer 2: Rendering (Vertex generation, texture atlas)
GUI Layer 3: Integration (Engine render API backend)
GUI Layer 4: Widgets (Button, Slider, Tree, Dockspace)
```

### Editor Architecture
```
Editor (Application subclass)
  ├── EditorGUI (manages panels)
  │   ├── SceneHierarchyPanel
  │   ├── InspectorPanel
  │   ├── ViewportPanel
  │   └── AssetBrowserPanel
  ├── EditorCamera (free-look camera)
  ├── SelectionManager (tracks selected entities)
  └── GizmoRenderer (transform handles)
```

## Dependencies

### Internal Dependencies
- **Core**: Base, Log, Event, VirtualFileSystem
- **ECS**: Entity, Component, System
- **Render**: All render abstractions (Buffer, Texture, Pipeline, etc.)
- **Platform**: Vulkan backend (initially)

### External Dependencies
- **SPIRV-Cross**: Shader reflection
- **GLM**: Math library (already in use)
- **stb_image**: Texture loading (already in use)
- **ImGui** (optional): Reference for GUI design patterns

## Constraints

- Must compile with C++20 on Windows, Linux, macOS
- Vulkan backend must be prioritized (DX12/Metal later)
- Editor must be separable from runtime (no editor code in shipped games)
- Reflection must work without external code generation tools
- All systems must pass constitution checks (platform abstraction, performance, type safety)

## Out of Scope

- Advanced material features (subsurface scattering, advanced PBR models)
- Visual shader editor (node-based shader authoring)
- Animation system (deferred to future feature)
- Physics integration (deferred to future feature)
- Networking/multiplayer support
- VR/AR support

## Risks & Mitigation

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Reflection system performance overhead | High | Medium | Benchmark early, optimize hot paths, consider code generation if needed |
| Material system complexity explosion | High | High | Start with minimal feature set, add complexity incrementally |
| GUI framework scope creep | Medium | High | Limit to essential widgets, use ImGui as reference but don't replicate all features |
| Editor stability issues | Medium | Medium | Separate editor thread from render thread, robust error handling |
| Cross-platform differences | Low | Medium | Abstract platform APIs early, test on multiple platforms regularly |

## Success Metrics

- **Material System**: 3+ material types implemented, hot-reload working
- **Reflection**: 50+ classes registered, serialization tested
- **Scene System**: 10k+ entity performance target met
- **GUI Framework**: Editor UI fully functional with all panels
- **Editor**: Can create and manipulate a basic scene with entities

## Timeline Estimate

- Phase 0 (Research): 1-2 weeks
- Phase 1 (Design & Contracts): 2-3 weeks
- Phase 2 (Implementation): 8-12 weeks
  - Material System: 2-3 weeks
  - Reflection System: 2-3 weeks
  - Scene System: 2-3 weeks
  - GUI Framework: 2-3 weeks
  - Editor: 2-3 weeks (parallel with GUI)

**Total**: 11-17 weeks

## References

- [Neon Engine Constitution](../../.specify/memory/constitution.md)
- [Unity Material System](https://docs.unity3d.com/Manual/Materials.html)
- [Unreal Reflection System](https://docs.unrealengine.com/5.0/en-US/reflection-system-in-unreal-engine/)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [The Machinery Blog](https://ourmachinery.com/post/) (ECS and editor design patterns)
