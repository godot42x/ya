# Scene Entity/Node Management Analysis - Part 2: Implementation

## 4. ENTITY.H - Entity Structure & API

### Entity Class (Lines 16-35)
```cpp
16  struct Entity
17  {
18    private:
19      entt::entity _entityHandle = {entt::null};
20      Scene       *_scene        = nullptr;
```

## 5. SCENE.CPP - destroyEntity() (Lines 118-138) - CRITICAL

```cpp
118 void Scene::destroyEntity(Entity *entity)
119 {
120     if (isValidEntity(entity))
121     {
122         auto handle = entity->getHandle();
123         auto nodeIt = _nodeMap.find(handle);
124         if (nodeIt != _nodeMap.end()) {
125             auto *node = nodeIt->second.get();
126             node->removeFromParent();
127             node->clearChildren();
128             _nodeMap.erase(nodeIt);
129         }
130         _registry.destroy(handle);
131         _entityMap.erase(handle);
132     }
133 }
```

KEY: Children are orphaned but NOT deleted (lines 127-128)

## 6. SCENE.CPP - duplicateNode() (Lines 621-649)

```cpp
621 Node *Scene::duplicateNode(Node *node, Node *parent)
622 {
623     if (!node) { return nullptr; }
624     Node   *newNode   = nullptr;
625     Entity *newEntity = nullptr;
626
627     if (Entity *entity = node->getEntity())
628     {
629         newEntity = createEntity(entity->name + " Duplicate");
630         auto  srcEntt  = entity->getHandle();
631         auto  newEntt  = newEntity->getHandle();
632         auto &registry = getRegistry();
633
634         refl::foreach_in_typelist<components_to_copy>([&](const auto &T) {
635             using type = std::decay_t<decltype(T)>;
636             if (registry.all_of<type>(srcEntt)) {
637                 copyComponent<type>(registry, srcEntt, registry, newEntt);
638             }
639         });
640     }
641     newNode = createNode(node->getName() + " Duplicate", parent, newEntity);
642     return newNode;
643 }
```

CRITICAL LIMITATION: Does NOT recursively duplicate children!

## 7. SCENE.CPP - Scene::clear() (Lines 291-298)

```cpp
291 void Scene::clear()
292 {
293     _entityMap.clear();
294     _nodeMap.clear();
295     _rootNode.reset();
296     _registry.clear();
297     _entityCounter = 0;
298 }
```

Simple wipe of all maps and registry

## 8. SCENEMANAGER.H - Scene Lifecycle

File: Engine/Source/Scene/SceneManager.h (118 lines)

Members:
- Line 29: stdptr<Scene> _activeScene
- Line 30: stdptr<Scene> _editorScene  
- Line 31: stdptr<Scene> _playScene
- Line 33: unordered_map<entt::registry*, Scene*> _reg2scene
- Line 34: unordered_set<const Scene*> _knownScenes

Methods:
- Line 63: bool loadScene(const string& path)
- Line 65: bool unloadScene()
- Line 87: stdptr<Scene> cloneScene(Scene* scene)

SceneManager tracks scene lifecycle and validity
