# SCENE ENTITY/NODE MANAGEMENT - COMPLETE CODE INDEX

## FILES ANALYZED
1. Engine/Source/Scene/Scene.h - 163 lines (FULL)
2. Engine/Source/Scene/Scene.cpp - 654 lines (key functions)
3. Engine/Source/ECS/Entity.h - 100 lines (FULL)
4. Engine/Source/Scene/SceneManager.h - 118 lines (FULL)

## CRITICAL CODE LOCATIONS

### Scene.h - Lines 22-28 (Data Members)
- Line 26: _entityMap - stores Entity wrappers
- Line 27: _nodeMap - entity handle to Node mapping
- Line 28: _rootNode - scene root

### Scene.cpp - destroyEntity() (Lines 118-138)
Lines 127-131 show orphaning behavior:
- Line 129: node->removeFromParent()
- Line 131: node->clearChildren()
Children become orphaned but still exist in registry

### Scene.cpp - onNodeCreated() (Lines 103-116)
Lines 110-114 show NO parent entity creation:
- Line 111: parent->addChild(node) - node level only
- NO parent entity link created

### Scene.cpp - duplicateNode() (Lines 621-649)
INCOMPLETE - does NOT recursively duplicate children

### Scene.cpp - createNode3D() (Lines 167-192)
Creates entity + node with onNodeCreated call

### Entity.h - No child management (Lines 16-98)
Entity has NO methods to manage child entities
All hierarchy in Node class

### SceneManager.h - Scene lifecycle (Lines 1-118)
Manages active/editor/play scenes
Tracks scene validity via _knownScenes

## KEY FINDINGS

1. HIERARCHY IS NODE-LEVEL ONLY
   - No automatic parent entity created
   - Children connected via parent->addChild(node)
   - Parent entity reference NOT in child entity

2. ORPHANING BEHAVIOR
   - destroyEntity() clears child nodes
   - Child entities remain in registry
   - But removed from parent's children array

3. INCOMPLETE DUPLICATION
   - duplicateNode() doesn't handle children
   - Must add recursion for node tree

4. ENTITY-NODE MAPPING
   - _nodeMap: entity handle -> node pointer
   - NOT every entity has a node
   - Every node has exactly one entity

5. REFLECTION-BASED OPERATIONS
   - components_to_copy list (lines 28-39)
   - Used for cloning and duplication
   - ModelComponent included

## SYSTEMS AFFECTED BY REMOVAL

- destroyNode/destroyEntity: Orphaning behavior review
- duplicateNode: MUST add recursion
- cloneScene: Simplify to node-based
- Serialization: Node hierarchy instead of entity hierarchy
- Registry queries: Only node entities remain

