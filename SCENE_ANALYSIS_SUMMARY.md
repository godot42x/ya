# SCENE MANAGEMENT CRITICAL FINDINGS

## Core Data Structures

**Scene._nodeMap (Line 27, Scene.h)**
- Type: unordered_map<entt::entity, shared_ptr<Node>>
- Maps Entity handles to Node shared pointers
- ONLY bidirectional lookup for entity->node relationship
- NO parent-child entity tracking

**Scene._entityMap (Line 26, Scene.h)**
- Type: unordered_map<entt::entity, Entity>
- Stores Entity wrapper objects by handle
- Entities stored by VALUE (not pointer)

**Scene._rootNode (Line 28, Scene.h)**
- shared_ptr<Node> to scene root
- Created on demand in createRootNode()

## Critical Pattern: NO Parent Entity on Child Creation

When calling:
```cpp
Scene.createNode3D("Child", parentNode, nullptr)
```

The flow (createNode3D, lines 167-192):
1. Creates new Entity (line 181)
2. Adds TransformComponent (line 185)
3. Creates Node3D wrapper (line 189)
4. Calls onNodeCreated(node, parentNode) (line 190)

In onNodeCreated (lines 103-116):
- Line 108: node registered in _nodeMap
- Line 111: node added to parentNode->addChild()
- NO parent entity created!

Result: 
- Child connected to parent at NODE level only
- Child entity has NO parent entity reference
- Hierarchy exists only in Node tree

## Operations Affected by ModelComponent Child Entity Pattern Removal

### 1. destroyNode() / destroyEntity() (Lines 195-206, 118-138)

Current behavior (destroyEntity):
```
Line 127: node->removeFromParent()        // Remove from node parent
Line 131: node->clearChildren()           // Orphan children
Line 135: _registry.destroy(handle)       // Destroy entt entity
Line 136: _entityMap.erase(handle)        // Remove from entity map
```

Children become orphaned:
- Removed from parent's children array
- But Entity remains in _registry
- But Entity remains in _entityMap
- But removed from _nodeMap

After ModelComponent removal:
- No child entities to worry about
- Behavior same (node-level orphaning only)
- May want to recursively destroy child nodes

### 2. duplicateNode() (Lines 621-649)

Current code:
```cpp
Line 629: newEntity = createEntity(entity->name + " Duplicate")
Lines 634-639: Copy components via reflection
Line 641: newNode = createNode(..., parent, newEntity)
```

Current limitations:
- Does NOT recursively duplicate children
- Does NOT copy child nodes
- Does NOT copy ModelComponent nested entities

After removal:
- MUST recursively duplicate child nodes
- Otherwise node tree structure lost
- Currently doesn't call duplicateNode on children

### 3. Scene Cloning (Lines 363-619)

Two implementations:

cloneScene() (lines 495-550):
- Creates new entities with createNode3D
- Copies components via reflection
- Has TODO comment: "Clone Node hierarchy" (line 545)
- Currently incomplete for child relationships

cloneSceneByReflection() (lines 552-619):
- Iterates all entities from registry
- Uses reflection to copy components
- Calls cloneReferencedNodeTree() for hierarchy
- Does NOT handle ModelComponent child entities

After removal:
- Must ensure node tree properly cloned
- Can simplify to pure node-based approach

### 4. getNodeByEntity() (Lines 210-225)

Current code:
```cpp
Line 220: auto it = _nodeMap.find(handle)
Line 222: return it->second.get()
```

Status: UNAFFECTED
- Simple lookup in _nodeMap
- Works with or without child entities

### 5. Scene::clear() (Lines 291-298)

Current code:
```cpp
_entityMap.clear()
_nodeMap.clear()
_rootNode.reset()
_registry.clear()
```

Status: MINIMALLY AFFECTED
- All maps cleared regardless
- Simplifies slightly (fewer orphaned entities)

## Key Insights for Removal

### Pattern 1: Hierarchy is Node-Level Only
- createNode3D() creates entity + node
- Parent entity is NOT created for node parents
- This is ALREADY the case
- ModelComponent violates this by creating child entities

### Pattern 2: Orphaning on Destruction
- When parent destroyed, children orphaned
- Child entities remain valid in registry
- But removed from parent's children array
- After removal: same behavior, just no child entities

### Pattern 3: Reflection-Based Cloning
- cloneSceneByReflection() already handles complex copying
- Uses ECSRegistry to discover all components
- Can be adapted to node-based approach

### Pattern 4: Duplication is Incomplete
- duplicateNode() does NOT handle recursion
- This MUST be fixed when child entities removed
- Otherwise hierarchy lost on duplication

## Files That Need Changes

1. **Scene.cpp - duplicateNode() (Lines 621-649)**
   - Add recursive duplication of child nodes
   - Currently only duplicates single node

2. **Scene.cpp - destroyEntity() (Lines 118-138)**
   - May add recursive child destruction
   - Currently orphans children

3. **Scene.cpp - cloneScene() (Lines 495-550)**
   - Complete the node hierarchy cloning
   - Has TODO at line 545

4. **Scene.cpp - cloneSceneByReflection() (Lines 552-619)**
   - Ensure node tree properly rebuilt
   - Already uses cloneReferencedNodeTree()

## Summary Table

Operation             | Current Status        | After Removal
---------------------|----------------------|------------------------
createNode3D()        | Works correctly       | No change needed
destroyNode()         | Orphans children      | May add recursion
destroyEntity()       | Orphans children      | May add recursion
duplicateNode()       | INCOMPLETE            | MUST add recursion
getNodeByEntity()     | Correct               | No change
Scene::clear()        | Correct               | Simplified
cloneScene()          | INCOMPLETE            | Must complete
cloneSceneByRefl()    | Works correctly       | Verify node cloning
Serialization         | Entity-based          | Becomes node-based
Registry queries      | Find all entities     | Find node entities only
