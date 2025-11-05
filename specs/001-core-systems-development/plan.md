# Implementation Plan: Core Systems Development

**Branch**: `001-core-systems-development` | **Date**: 2025-11-05 | **Spec**: [spec.md](./spec.md)

## Summary

Development of five essential engine systems to transform Neon Engine into a full-featured game engine. The Material System provides unified shader/property management across backends. The Reflection System enables runtime type inspection and serialization. The Scene System manages hierarchical entity graphs. The GUI Framework delivers immediate-mode UI for games and tools. The Editor provides visual scene authoring with asset management. Implementation leverages existing ECS and render abstractions with Vulkan as the initial target, following constitution principles of platform abstraction, performance-first design, and type safety.

## Technical Context

**Language/Version**: C++20 with concepts, constexpr, and ranges  
**Primary Dependencies**:
- Vulkan SDK 1.3+ (existing)
- GLM (existing math library)
- glslang + SPIRV-Cross (shader compilation)
- nlohmann/json (serialization)
- stb_image (existing, texture loading)

**Storage**: File-based (.scene, .prefab, .mat), JSON for human-readable formats  
**Testing**: Custom test framework (existing test.cc plugin), unit + integration tests  
**Target Platform**: Windows (primary), Linux/macOS (secondary), x64  
**Project Type**: Game engine with runtime library + separate editor executable  
**Performance Goals**:
- Material switch overhead: < 1ms
- Reflection property access: < 100ns via caching
- Scene transform updates: < 2ms for 10,000 entities
- GUI rendering: 1000+ widgets @ 60fps
- Editor frame time: < 16.67ms with 1000-entity scenes

**Constraints**:
- Must maintain 60fps (< 16.67ms frame budget)
- Editor memory budget: < 2GB for medium projects
- Reflection metadata: < 1KB per registered class
- Scene entity overhead: < 256 bytes per entity
- Zero platform headers in Engine/Source/{Core,Render,Scene,GUI}

**Scale/Scope**:
- 50+ classes with reflection metadata
- 3+ material types (Unlit, Lit, PBR)
- 10,000+ entity capacity
- 10+ core GUI widgets
- 5+ editor panels

## Constitution Check

*GATE: Must pass before Phase 0. Re-check after Phase 1 design.*

### ✅ Principle 1: Platform Abstraction

**Status**: PASS  
**Verification**:
- Material system uses `IMaterial` interface, properties via `std::variant`
- Reflection system is platform-agnostic (pure C++ metaprogramming)
- Scene system built on abstract ECS, no platform dependencies
- GUI renders through `IRender` interface, no direct Vulkan calls
- Shader compiler abstracted behind `IShaderCompiler` interface

**Risks**: Initial implementation tightly couples GLSL → SPIR-V compilation  
**Mitigation**: Define `IShaderCompiler` early, implement Vulkan version, design for future HLSL/MSL backends

### ✅ Principle 2: Performance First

**Status**: PASS with monitoring required  
**Verification**:
- Material system batches descriptor updates (existing pattern)
- Reflection uses compile-time registration (zero runtime registration cost)
- Scene graph caches world transforms with dirty flagging
- GUI uses immediate-mode to minimize retained state overhead

**Concerns**: Reflection property access by string name could be O(log n) or O(n)  
**Mitigation**: Implement property ID caching (hash string once, use ID thereafter). Benchmark in Phase 0.

**Concerns**: GUI vertex generation every frame could be CPU-bound  
**Mitigation**: Profile GUI rendering, optimize with SIMD or geometry instancing if needed

### ✅ Principle 3: Type Safety

**Status**: PASS  
**Verification**:
- Material properties use `std::variant<int, float, vec3, vec4, Texture*>`
- Reflection uses templates + `std::type_index` for type safety
- Scene entities use strongly-typed component storage
- GUI builder pattern enforces type-safe widget construction

**Example**:
```cpp
// Type-safe material property
material->setProperty<glm::vec4>("albedo", {1, 0, 0, 1}); // OK
material->setProperty<int>("albedo", 42);                 // Compile error!
```

### ✅ Principle 4: Modern C++ Standards

**Status**: PASS  
**Verification**:
- C++20 concepts constrain reflection-eligible types
- `constexpr` registration macros enable compile-time metadata
- RAII for all resource lifetimes (Scene owns entities, Material owns descriptors)
- Smart pointers for shared ownership (`std::shared_ptr<Shader>`)

**Example**:
```cpp
template<Reflectable T>
constexpr TypeInfo registerType() { /* ... */ }
```

### ✅ Principle 5: Clean Architecture

**Status**: PASS  
**Verification**:
- **Core Layer**: `Reflection/`, `Serialization/` (no external deps)
- **Framework Layer**: `Render/Material.*`, `Scene/*`, `GUI/*` (depends on Core)
- **Platform Layer**: `Platform/Render/Vulkan/VulkanShaderCompiler.*` (implements interfaces)
- **Application Layer**: `Editor/*` (separate executable, depends on Framework)

**Dependency Flow**: Editor → Framework (Scene, GUI) → Core (Reflection) ← Platform (implements)

### ✅ Principle 6: Extensibility

**Status**: PASS  
**Verification**:
- Material system: Users subclass `Material`, register custom properties
- Reflection: Macros enable user types to opt-in to serialization
- Scene system: ECS allows custom components without modifying core
- GUI: Custom widgets via inheritance or composition
- Editor: (Future) plugin API for custom panels/tools

**Example**: User creates `WaterMaterial : public Material` with custom wave properties, editor auto-generates property UI via reflection

### Summary

**Overall**: ✅ **PASS** - All six principles satisfied  
**Action**: Proceed to Phase 0 (Research)

**Deferred Items**:
- Multi-backend shader compilation (abstract interface defined, additional backends deferred)
- Reflection performance profiling (measure in Phase 0, optimize if needed)
- Editor plugin system (marked as future feature)

## Project Structure

### Documentation (this feature)

```text
specs/001-core-systems-development/
├── spec.md          # Feature specification
├── plan.md          # This file (implementation plan)
├── research.md      # Phase 0 output (decisions & research findings)
├── data-model.md    # Phase 1 output (entities, schemas, relationships)
├── quickstart.md    # Phase 1 output (developer onboarding guide)
├── contracts/       # Phase 1 output (API contracts)
│   ├── material-api.md
│   ├── reflection-api.md
│   ├── scene-api.md
│   ├── gui-api.md
│   └── editor-commands.md
└── tasks.md         # Phase 2 output (NOT created by this command)
```

### Source Code (repository root)

```text
Engine/Source/
├── Core/
│   ├── Reflection/              # NEW: Reflection system
│   │   ├── TypeInfo.h/cpp       # Type metadata storage
│   │   ├── Property.h/cpp       # Property descriptors
│   │   ├── Method.h/cpp         # Method descriptors
│   │   ├── Registry.h/cpp       # Global type registry
│   │   └── Macros.h             # CLASS/PROPERTY/METHOD macros
│   └── Serialization/           # NEW: Serialization utilities
│       ├── Serializer.h/cpp     # Object → JSON/Binary
│       └── Deserializer.h/cpp   # JSON/Binary → Object
│
├── Render/
│   ├── Material.h/cpp           # NEW: Material system
│   ├── MaterialProperties.h     # NEW: Property value storage
│   ├── Shader.h/cpp             # EXISTING: Expand with reflection
│   └── ShaderCompiler.h/cpp     # NEW: Abstract shader compiler
│
├── Scene/                       # NEW: Scene management
│   ├── Scene.h/cpp              # Scene container
│   ├── SceneGraph.h/cpp         # Hierarchy algorithms
│   ├── Transform.h/cpp          # EXISTING: Expand with parent/child
│   ├── Prefab.h/cpp             # Prefab (scene template)
│   └── SceneSerializer.h/cpp    # Scene ↔ File I/O
│
├── GUI/                         # NEW: GUI framework
│   ├── Core/
│   │   ├── Context.h/cpp        # GUI state management
│   │   ├── Widget.h/cpp         # Base widget interface
│   │   ├── Layout.h/cpp         # Layout algorithms
│   │   └── Input.h/cpp          # Input event handling
│   ├── Widgets/
│   │   ├── Button.h/cpp
│   │   ├── Slider.h/cpp
│   │   ├── TextInput.h/cpp
│   │   ├── Tree.h/cpp
│   │   └── Dockspace.h/cpp
│   └── Render/
│       ├── GUIRenderer.h/cpp    # Vertex generation
│       └── GUIBackend.h/cpp     # Abstract render backend
│
├── ECS/
│   └── System/
│       └── MaterialSystem.h/cpp # EXISTING: Integrate new Material
│
└── Platform/
    └── Render/
        └── Vulkan/
            ├── VulkanShaderCompiler.h/cpp  # NEW: GLSL → SPIR-V impl
            └── VulkanGUIBackend.h/cpp      # NEW: GUI Vulkan backend

Editor/                          # NEW: Editor application
├── Source/
│   ├── EditorApp.h/cpp          # Editor entry point
│   ├── EditorCamera.h/cpp       # Free-look camera controller
│   ├── SelectionManager.h/cpp   # Entity selection tracking
│   ├── Gizmo.h/cpp              # Transform gizmos (translate/rotate/scale)
│   └── Panels/
│       ├── SceneHierarchyPanel.h/cpp  # Entity tree view
│       ├── InspectorPanel.h/cpp       # Property editor
│       ├── ViewportPanel.h/cpp        # 3D scene preview
│       └── AssetBrowserPanel.h/cpp    # Asset library
└── xmake.lua                    # Editor build target

Example/
└── EditorExample/               # NEW: Minimal editor usage
    ├── Source/
    │   └── main.cpp
    └── xmake.lua

Engine/Test/                     # NEW: Test structure
├── Reflection/                  # Reflection unit tests
├── Scene/                       # Scene system tests
└── GUI/                         # GUI framework tests
```

**Structure Decision**:

- **Monolithic Engine**: Core systems (Reflection, Scene, GUI) remain in `Engine/Source/` for simplified dependency management during active development. This avoids complex inter-project linking and improves iteration speed.

- **Separate Editor Executable**: `Editor/` is a distinct application linking against Engine, ensuring zero editor code in runtime builds. This enforces clean separation per Constitution Principle 5 (Clean Architecture).

- **Platform Isolation**: Platform-specific implementations (Vulkan shader compiler, Vulkan GUI backend) reside in `Engine/Source/Platform/Render/Vulkan/`, maintaining abstraction boundaries per Constitution Principle 1.

- **Test Structure Mirrors Source**: Tests organized by subsystem (`Test/Reflection/`, `Test/Scene/`, etc.) for clarity and parallel development.

**Rationale**: Monolithic structure reduces link-time overhead and simplifies CMake/xmake configuration. Editor separation prevents runtime bloat. Platform directory preserves cross-platform portability.

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations**

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| N/A | No violations | All constitution checks passed |

---

## Phase 0: Outline & Research

**Goal**: Resolve all technical unknowns and document architectural decisions in `research.md`.

### Research Tasks

#### 1. Reflection Implementation Strategy
- **Question**: Macro-based vs template-based vs code generation?
- **Research**:
  - Macro-based (Unreal style): `CLASS(MyClass) PROPERTY(int, x) END_CLASS()`
  - Template-based (Modern C++): `Register<MyClass>(Property{"x", &MyClass::x})`
  - Code generation (External tool): Parse headers, generate registration code
- **Evaluate**: Compile time impact, ergonomics, tooling requirements
- **Deliverable**: Decision matrix in `research.md` with recommendation

#### 2. Shader Compilation Pipeline
- **Question**: Integrate glslang library or use external tools (glslc, DXC)?
- **Research**:
  - glslang library: In-process compilation, hot-reload friendly
  - glslc CLI: External tool, simpler build but harder to integrate
  - DXC: Future HLSL support consideration
- **Evaluate**: Build complexity, error reporting quality, hot-reload support
- **Deliverable**: Shader compilation architecture diagram in `research.md`

#### 3. Serialization Format
- **Question**: JSON (human-readable) vs binary (performance) vs hybrid?
- **Research**:
  - JSON: Easy debugging, version control friendly, slower parse
  - Binary: Fast parse, smaller files, opaque to humans
  - Hybrid: JSON in editor, binary in shipped builds
- **Benchmark**: Parse 1000-entity scene, measure time & file size
- **Deliverable**: Format recommendation with benchmarks in `research.md`

#### 4. GUI Framework Foundation
- **Question**: Build from scratch or adapt Dear ImGui patterns?
- **Research**:
  - Dear ImGui architecture: Immediate-mode, vertex buffer generation
  - Identify reusable patterns: ID stacks, layout algorithms, input handling
  - Legal considerations: ImGui is MIT licensed, can study but not copy
- **Deliverable**: GUI design patterns document in `research.md`

#### 5. Scene Serialization Format
- **Question**: Custom format vs glTF-inspired vs YAML?
- **Research**:
  - Custom JSON: Full control, no external dependencies
  - glTF: Industry standard, tooling support, heavier spec
  - YAML: Human-friendly, less common in game engines
- **Evaluate**: Extensibility, tooling ecosystem, performance
- **Deliverable**: Scene format specification in `research.md`

#### 6. Editor Threading Model
- **Question**: Single-threaded vs multi-threaded (editor UI + render)?
- **Research**:
  - Single-threaded: Simpler, potential UI lag during heavy rendering
  - Multi-threaded: Complex synchronization, better responsiveness
  - Hybrid: Async asset loading but synchronous rendering
- **Analyze**: Unity/Unreal threading models, stability vs complexity
- **Deliverable**: Threading architecture decision in `research.md`

### Best Practices Research

#### 1. Material Property Binding Patterns
- **Research**: How Unity/Unreal bind material properties to GPU resources
- **Study**: Descriptor sets (Vulkan), root signatures (D3D12), uniform buffers
- **Output**: Property → GPU binding strategy (push constants vs UBO vs SSBO)

#### 2. Reflection Metadata Optimization
- **Research**: Techniques to minimize memory overhead
- **Study**: String interning, property ID hashing, metadata compression
- **Output**: Memory optimization checklist

#### 3. Scene Graph Performance Optimization
- **Research**: Cache-friendly scene update patterns
- **Study**: Dirty flagging, SoA vs AoS layouts, spatial partitioning
- **Output**: Scene update optimization plan

#### 4. GUI Rendering Performance
- **Research**: Vertex batching and draw call minimization
- **Study**: Texture atlasing, geometry instancing, index buffer reuse
- **Output**: GUI rendering optimization checklist

### Phase 0 Deliverables

**File**: `research.md`

**Required Sections**:
1. Reflection Implementation (decision + rationale)
2. Shader Compilation Pipeline (architecture + diagrams)
3. Serialization Format (benchmarks + recommendation)
4. GUI Framework Design (patterns + rendering strategy)
5. Scene Format Specification (schema + examples)
6. Editor Threading Model (decision + trade-offs)
7. Best Practices Summary (material binding, reflection optimization, scene performance, GUI rendering)

**Success Criteria**: No NEEDS CLARIFICATION items remain, all architectural decisions documented with rationale.

---

## Phase 1: Design & Contracts

**Prerequisites**: `research.md` complete with all decisions finalized

### Task 1: Data Model Design

**Output**: `data-model.md`

**Content**: Entity schemas, relationships, validation rules, state machines

(See separate section below for detailed data model)

### Task 2: API Contracts

**Output**: `/contracts/*.md` files

**Content**: Code examples, function signatures, usage patterns

(See separate section below for detailed contracts)

### Task 3: Quickstart Guide

**Output**: `quickstart.md`

**Content**: Setup instructions, "Hello World" examples, common workflows

### Task 4: Agent Context Update

**Action**: Run `.specify/scripts/powershell/update-agent-context.ps1 -AgentType copilot`

**Expected Changes**:
- Add reflection macro patterns to code completion
- Add material system API examples
- Add scene hierarchy manipulation patterns
- Add GUI immediate-mode examples
- Preserve existing render abstraction context

### Phase 1 Deliverables Checklist

- [ ] `data-model.md` created with all entities, relationships, validation rules
- [ ] `contracts/material-api.md` documents Material API with examples
- [ ] `contracts/reflection-api.md` documents Reflection API with examples
- [ ] `contracts/scene-api.md` documents Scene API with examples
- [ ] `contracts/gui-api.md` documents GUI API with examples
- [ ] `contracts/editor-commands.md` documents Editor operations
- [ ] `quickstart.md` provides developer onboarding (setup, first steps, examples)
- [ ] Agent context updated via script
- [ ] Constitution Check re-evaluated (confirm no violations introduced by design)

---

## Phase 1 Complete - Next Steps

**After Phase 1 completion**:
1. Review all generated contracts with team
2. Run `/speckit.tasks` command to generate `tasks.md` (Phase 2)
3. Begin implementation following task breakdown

**Phase 2 Preview** (not executed by this command):
- Generate atomic tasks from contracts
- Establish dependency ordering
- Estimate effort for each task
- Define milestones and deliverables

---

**Command Status**: ✅ Phase 0 research outline generated, Phase 1 structure defined

**Next Action**: Execute Phase 0 research tasks, document findings in `research.md`

