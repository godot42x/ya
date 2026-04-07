# Scene Entity/Node Management Analysis - Executive Summary

## Overview

This analysis examines the Scene system's entity and node management, specifically focusing on operations affected by removing ModelComponent's child entity pattern. Complete source code with line numbers has been provided.

## Files Analyzed

1. **Engine/Source/Scene/Scene.h** (163 lines) - FULL
2. **Engine/Source/Scene/Scene.cpp** (654 lines) - Key functions
3. **Engine/Source/ECS/Entity.h** (100 lines) - FULL
4. **Engine/Source/Scene/SceneManager.h** (118 lines) - FULL

## Critical Finding

**The Scene system already separates entity management from node hierarchy.**

### Current Pattern (Lines 103-116 - onNodeCreated())
```cpp
void Scene::onNodeCreated(stdptr<Node> node, Node *parent)
{
    _nodeMap[node->getEntity()->getHandle()] = node;
    
    if (parent) {
        parent->addChild(node.get());  // NODE level only!
    }
}
```

**Key insight:** When a child node is added, NO parent entity is created. The parent-child relationship exists ONLY at the Node level, not at the Entity/ECS level.

## Core Data Structures (Scene.h, Lines 22-28)

```cpp
std::string    _name;
entt::registry _registry;                      // ECS registry
unordered_map<entity, Entity> _entityMap;      // Entity wrappers
unordered_map<entity, Node*> _nodeMap;         // Entity -> Node mapping
shared_ptr<Node> _rootNode;                    // Scene root
```

**Critical:** NO parent-child entity relationship mapping exists.

## Impact Analysis

### CRITICAL - duplicateNode() (Lines 621-649)

**Problem:** Does NOT recursively duplicate child nodes

**Current code:**
- Creates new entity
- Copies components via reflection
- Creates new node
- **MISSING:** No recursion for children

**After ModelComponent removal:**
- MUST add recursive child duplication
- Otherwise node hierarchy is completely lost
- Currently incomplete implementation

**Required fix:**
```cpp
for (Node* child : node->getChildren()) {
    duplicateNode(child, newNode);
}
```

### HIGH - destroyEntity() (Lines 118-138)

**Current behavior:**
- Line 131: `node->clearChildren()` - orphans child nodes
- Children removed from parent but survive in registry
- Becomes dangling nodes with orphaned entities

**After ModelComponent removal:**
- May want recursive destruction instead
- Need to review desired behavior
- Currently undefined for child entity handling

### HIGH - Scene Cloning (Lines 495-550, 552-619)

**Two implementations:**

1. **cloneScene()** (lines 495-550)
   - Entity-based, incomplete (TODO at line 545)
   - Does not fully rebuild node hierarchy

2. **cloneSceneByReflection()** (lines 552-619)
   - Reflection-based, complete and functional
   - Uses cloneReferencedNodeTree()

**After ModelComponent removal:**
- Must ensure node tree properly cloned
- Can simplify to pure node-based approach
- Consider using cloneSceneByReflection as primary

### UNAFFECTED OPERATIONS

- **getNodeByEntity()** (lines 210-225) - Simple lookup, no change
- **Scene::clear()** (lines 291-298) - Works same before/after
- **createNode3D()** (lines 167-192) - Already correct pattern
- **onNodeCreated()** (lines 103-116) - Already shows correct pattern

## Priority Action Items

| Priority | Function | Lines | Action |
|----------|----------|-------|--------|
| CRITICAL | duplicateNode() | 621-649 | Add recursive duplication |
| HIGH | destroyEntity() | 118-138 | Review orphaning behavior |
| HIGH | cloneScene() | 495-550 | Complete or replace |
| IMPORTANT | Serialization | - | Shift to node-based format |
| IMPORTANT | Tests | - | Validate with hierarchies |

## Key Patterns Identified

1. **Entity-Node 1:1 Mapping**
   - Every Node has exactly one Entity
   - Not every Entity has a Node
   - Bidirectional lookup via _nodeMap

2. **No Parent Entity on Child Creation**
   - This pattern ALREADY EXISTS in the code
   - ModelComponent violates it by creating child entities
   - Removing ModelComponent makes this consistent

3. **Orphaning on Parent Destruction**
   - Children removed from parent's children array
   - Child entities survive in registry
   - Becomes dangling nodes

4. **Reflection-Based Operations**
   - components_to_copy list (lines 28-39)
   - Used for duplication and cloning
   - ModelComponent included (line 38)

## Document Organization

### SCENE_ANALYSIS_PART1.md
- Scene.h complete structure
- Data members and API declarations
- Lines 1-139 with explanation

### SCENE_ANALYSIS_PART2.md
- Entity.h complete structure
- Scene.cpp key implementations
- destruyEntity(), duplicateNode(), clear()

### SCENE_ANALYSIS_SUMMARY.md
- Critical findings and patterns
- Impact analysis table
- Removal impact assessment
- File locations for modification

### COMPLETE_CODE_INDEX.md
- Quick reference index
- All code locations with line numbers
- Summary of key findings

## Recommendations

1. **Immediately:** Fix duplicateNode() with recursive child duplication
2. **Soon:** Review destroyEntity() orphaning behavior and decide on approach
3. **Soon:** Complete or verify Scene cloning implementation
4. **Later:** Update serialization to node-based format
5. **Ongoing:** Add comprehensive tests for all hierarchy operations

## Conclusion

The Scene system is already partially prepared for removing ModelComponent's child entity pattern. The main issue is duplicateNode() which MUST be fixed. Secondary issues involve reviewing orphaning behavior and completing scene cloning. The analysis provides complete code references with line numbers for all required modifications.

---

**Analysis completed:** April 6, 2026
**Total documentation:** 535 lines across 4 markdown files
**Code references:** Full line numbers for all critical operations
