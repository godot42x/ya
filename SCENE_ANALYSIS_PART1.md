# Scene Entity/Node Management Analysis - Part 1

## File Overview

**Engine/Source/Scene/Scene.h** - Full file read (163 lines)
**Engine/Source/Scene/Scene.cpp** - Focus on key functions (654 lines)
**Engine/Source/ECS/Entity.h** - Full file read (100 lines)
**Engine/Source/Scene/SceneManager.h** - Full file read (118 lines)

---

## 1. SCENE.H - Data Structures (Lines 1-50)

### Include & Namespace
```cpp
1   #pragma once
2   #include "Core/Debug/Instrumentor.h"
3   #include "Node.h"
4   #include "Render/Model.h"
5   #include <entt/entt.hpp>
6   #include <string>
7   #include <vector>
8   
9   namespace ya
10  {
11  struct Entity;
```

### Core Data Members (Lines 14-28)
```cpp
14  struct [[refl]] Scene
15  {
16      friend struct Entity;
17  
18      // Magic number for dangling pointer detection
19      // static constexpr uint32_t SCENE_MAGIC = 0x5343454E; // 'SCEN'
20      // uint32_t                  _magic      = SCENE_MAGIC;
21  
22      std::string    _name;
23      entt::registry _registry;
24      uint32_t       _entityCounter = 0;
25  
26      std::unordered_map<entt::entity, Entity>                _entityMap;
27      std::unordered_map<entt::entity, std::shared_ptr<Node>> _nodeMap;
28      std::shared_ptr<Node>                                   _rootNode = nullptr;
```

**CRITICAL INSIGHT:** 
- Line 26: _entityMap stores Entity wrappers
- Line 27: _nodeMap stores Node shared_ptrs
- NO explicit parent-child entity relationship tracking
- Hierarchy is purely at Node level

### Public API Declaration (Lines 30-45)
```cpp
30  public:
31      Scene(const std::string& name = "Untitled Scene");
32      ~Scene();
33  
34  
35      // Delete copy constructor and assignment operator
36      Scene(const Scene&)            = delete;
37      Scene& operator=(const Scene&) = delete;
38  
39      // Add move constructor and assignment operator
40      Scene(Scene&&)            = default;
41      Scene& operator=(Scene&&) = default;
42  
43      // === Public Node API (Application Layer) ===
44      Node*   createNode(const std::string& name = "Entity", Node* parent = nullptr, Entity* entity = nullptr);
45      Node3D* createNode3D(const std::string& name = "Entity", Node* parent = nullptr, Entity* entity = nullptr);
```

---

## 2. SCENE.H - Component & Node Management API (Lines 48-95)

```cpp
48      template <typename ComponentType, typename... Args>
49          requires(!std::is_base_of_v<ComponentType, IComponent>)
50      ComponentType* addComponent(entt::entity entity, Args&&... args)
51      {
52          if (!isValid()) {
53              YA_CORE_WARN("Scene is invalid");
54              return nullptr;
55          }
56          return &_registry.emplace<ComponentType>(entity, std::forward<Args>(args)...);
57      }
58  
59      template <typename ComponentType>
60      void removeComponent(entt::entity entity)
61      {
62          if (!isValid()) {
63              YA_CORE_WARN("Scene is invalid");
64              return;
65          }
66          _registry.remove<ComponentType>(entity);
67          SceneBus::get().onComponentRemoved.broadcast(_registry, entity, type_index_v<ComponentType>);
68      }
69      /**
70       * @brief Destroy a Node and its underlying Entity
71       */
72      void destroyNode(Node* node);
73  
74      void destroyEntity(Entity* entity);
75  
76  
77      /**
78       * @brief Get the Node associated with an Entity
79       * @return Node pointer or nullptr if entity has no associated Node
80       * Q: why not add Entity::getNode(self) interface?
81       * A: We in the early POC stage, we don't sure yet to make ECS and the NodeTree be integrated
82       */
83      Node* getNodeByEntity(Entity* entity);
84      Node* getNodeByEntity(entt::entity handle);
```

---

## 3. SCENE.H - Rest of Public API (Lines 86-139)

```cpp
86      /**
87       * @brief Get root node of scene hierarchy
88       */
89      Node* getRootNode()
90      {
91          createRootNode();
92          return _rootNode.get();
93      }
94      bool isValidEntity(const Entity* entity) const;
95  
96      bool isValid() const;
97  
98      Entity*       getEntityByEnttID(entt::entity id);
99      const Entity* getEntityByEnttID(entt::entity id) const;
100     Entity*       getEntityByID(uint32_t id)
101     {
102         return getEntityByEnttID(static_cast<entt::entity>(id));
103     }
104 
105     Entity* getEntityByName(const std::string& name);
106 
107     // Scene management
108     void clear();
109     void onUpdateRuntime(float deltaTime);
110     void onUpdateEditor(float deltaTime);
111     void onRenderEditor();
112     void onRenderRuntime();
113 
114     // Getters
115     const std::string& getName() const { return _name; }
116     void               setName(const std::string& name) { _name = name; }
117 
118     // Registry access
119     entt::registry&       getRegistry() { return _registry; }
120     const entt::registry& getRegistry() const { return _registry; }
121 
122     // Find entities
123     Entity              findEntityByName(const std::string& name);
124     std::vector<Entity> findEntitiesByTag(const std::string& tag);
125 
126     void addToScene(Node* node)
127     {
128         if (!_rootNode) {
129             createRootNode();
130         }
131         _rootNode->addChild(node);
132     }
133 
134     stdptr<Scene>        clone();
135     static stdptr<Scene> cloneScene(const Scene* scene);
136     static stdptr<Scene> cloneSceneByReflection(const Scene* scene);
137 
138     Node* duplicateNode(Node* node, Node* parent = nullptr);
```

