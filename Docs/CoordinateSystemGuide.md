# 坐标系转换指南

## 引擎坐标系配置

当前引擎使用：**左手系 (Left-Handed)**
- 定义位置：`Engine/Source/Render/Mesh.cpp`
- 常量：`ENGINE_COORDINATE_SYSTEM = CoordinateSystem::LeftHanded`

### 坐标系对比

| 坐标系 | X轴 | Y轴 | Z轴 | 使用者 |
|--------|-----|-----|-----|--------|
| **左手系** | 右 | 上 | **向前(屏幕内)** | DirectX, Unity |
| **右手系** | 右 | 上 | **向后(屏幕外)** | OpenGL, Vulkan, Blender |

## 自动转换机制

### 1. 程序生成几何体（Cube等）

```cpp
// 使用引擎坐标系（左手系）
std::vector<Vertex> vertices;
std::vector<uint32_t> indices;
GeometryUtils::makeCube(-1, 1, -1, 1, -1, 1, vertices, indices);

// 默认使用左手系，无需转换
auto mesh = makeShared<Mesh>(vertices, indices, "MyCube");
```

### 2. 从Blender导出模型

**Blender导出设置（推荐）：**
- 格式：Wavefront (.obj)
- 坐标系会自动标记为 `RightHanded`
- 引擎自动转换绕序

```cpp
// AssetManager自动处理
auto model = AssetManager::get()->loadModel("Models/Monkey.obj");
// MeshData.sourceCoordSystem = CoordinateSystem::RightHanded (自动设置)
// Mesh构造时自动翻转索引绕序
```

### 3. 手动指定坐标系

```cpp
// 如果从Unity导出（左手系）
MeshData meshData;
meshData.sourceCoordSystem = CoordinateSystem::LeftHanded;
meshData.createGPUResources(); // 无需转换

// 如果从Maya/Blender导出（右手系）
MeshData meshData;
meshData.sourceCoordSystem = CoordinateSystem::RightHanded;
meshData.createGPUResources(); // 自动翻转绕序
```

## 修改引擎坐标系

如果需要切换到右手系（例如使用OpenGL后端）：

1. 修改 `Engine/Source/Render/Mesh.cpp`:
```cpp
static constexpr CoordinateSystem ENGINE_COORDINATE_SYSTEM = CoordinateSystem::RightHanded;
```

2. 更新 `Geometry.cpp` 中cube生成代码的注释和法线方向

3. 重新编译

## Blender导出建议

### 方案1：保持默认（右手系）

Blender → Export → Wavefront (.obj)
- ✅ 无需特殊设置
- ✅ 引擎自动转换

### 方案2：导出为左手系

Blender → Export → FBX
- Forward: `-Z Forward`
- Up: `Y Up`
- ⚠️ 仍需在代码中设置 `sourceCoordSystem`

## 调试技巧

### 检查模型是否翻转

如果模型显示错误（只看到背面或内部）：

```cpp
// 在Mesh.cpp中启用日志
YA_CORE_TRACE("Mesh '{}': Converted from {} to {} coordinate system", ...);
```

查看是否进行了坐标系转换。

### 强制禁用转换

```cpp
// 创建Mesh时显式指定源坐标系
auto mesh = makeShared<Mesh>(
    vertices, 
    indices, 
    "MyMesh",
    CoordinateSystem::LeftHanded  // 与ENGINE_COORDINATE_SYSTEM相同，跳过转换
);
```

## 常见问题

**Q: 为什么OpenGL也用右手系？**
A: OpenGL的NDC（标准化设备坐标）是右手系，Z+向屏幕外。

**Q: Vulkan不是左手系吗？**
A: Vulkan的NDC深度范围是0-1（不是-1到1），但坐标系仍是右手系。

**Q: 能否混用左右手系模型？**
A: 可以！每个Mesh独立指定 `sourceCoordSystem`，引擎自动处理。

**Q: 性能影响？**
A: 索引翻转在加载时一次性完成，运行时零开销。
