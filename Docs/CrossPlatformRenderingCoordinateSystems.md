# 跨平台渲染坐标系处理方案

## 问题背景

不同图形 API 的坐标系存在差异，主要体现在：

| 图形API | NDC Y轴方向 | NDC Z范围 | Viewport原点 | 投影矩阵 |
|---------|-------------|-----------|--------------|----------|
| OpenGL  | 上正下负    | [-1, 1]   | 左下角       | RH/LH    |
| Vulkan  | 上负下正    | [0, 1]    | 左上角       | RH_ZO    |
| DirectX | 上负下正    | [0, 1]    | 左上角       | LH_ZO    |

这些差异如果不处理，会导致：
- 光照方向错误（前后颠倒）
- 深度测试失败
- 法线方向反转
- 纹理上下翻转

---

## 解决方案对比

### 方案1：统一到一个标准坐标系（✅ 推荐）

**核心思想**：选择一个主要目标平台（如 Vulkan）作为标准，其他平台在渲染后端层适配。

#### 实现方式

**Camera 层（逻辑层）**：
```cpp
// Camera.h
void setPerspective(float fov, float aspectRatio, float nearClip, float farClip) {
    // 统一使用 Vulkan 风格投影矩阵（右手坐标系，Z: [0,1]）
    projectionMatrix = glm::perspectiveRH_ZO(glm::radians(fov), aspectRatio, nearClip, farClip);
    recalculateViewProjectionMatrix();
}
```

**VulkanRenderTarget（渲染后端）**：
```cpp
void VulkanRenderTarget::getViewAndProjMatrix(glm::mat4& view, glm::mat4& proj) const {
    proj = camera.getProjectionMatrix();
    
    // Vulkan viewport 翻转适配（Y轴向下为正）
    proj[1][1] *= -1;
    
    view = camera.getViewMatrix();
}
```

**OpenGLRenderTarget（渲染后端）**：
```cpp
void OpenGLRenderTarget::getViewAndProjMatrix(glm::mat4& view, glm::mat4& proj) const {
    proj = camera.getProjectionMatrix();
    
    // 转换 Z 范围：[0,1] → [-1,1]
    // 公式：NDC_OpenGL = NDC_Vulkan * 2 - 1
    proj[2][2] = proj[2][2] * 2.0f - proj[3][2];
    proj[2][3] = proj[2][3] * 2.0f;
    
    // OpenGL 不需要 Y 轴翻转（传统 viewport 左下角为原点）
    
    view = camera.getViewMatrix();
}
```

**Shader 层**：
```glsl
// Lit.glsl - 统一按 Vulkan 坐标系编写
void main() {
    // 世界空间计算（后端无关）
    vec3 worldPos = (modelMat * vec4(aPos, 1.0)).xyz;
    vec3 lightDir = normalize(light.position - worldPos);
    
    // 光照计算（后端无关）
    float diff = max(dot(normal, lightDir), 0.0);
    
    // 投影变换（由投影矩阵处理坐标系差异）
    gl_Position = projMat * viewMat * vec4(worldPos, 1.0);
}
```

**优点**：
- ✅ Shader 代码简洁，无需宏定义
- ✅ 后端差异封装在各自的 RenderTarget 中
- ✅ 易于维护和调试
- ✅ 运行时零开销
- ✅ 符合分层架构设计

**缺点**：
- 需要为每个后端实现坐标系转换逻辑

---

### 方案2：Shader 预处理宏（灵活但复杂）

**核心思想**：在编译 Shader 时注入平台宏，Shader 根据宏选择不同的代码路径。

#### 实现方式

**Shader 编译时注入宏**：
```cpp
// VulkanRender::compileShader()
std::string shaderCode = "#define BACKEND_VULKAN\n" + loadShaderSource(path);
compile(shaderCode);

// OpenGLRender::compileShader()
std::string shaderCode = "#define BACKEND_OPENGL\n" + loadShaderSource(path);
compile(shaderCode);
```

**Shader 代码**：
```glsl
// Lit.glsl
#ifdef BACKEND_VULKAN
    #define FLIP_Y_AXIS 1
    #define NDC_Z_ZERO_TO_ONE 1
#elif defined(BACKEND_OPENGL)
    #define FLIP_Y_AXIS 0
    #define NDC_Z_ZERO_TO_ONE 0
#endif

void main() {
    vec3 worldPos = (modelMat * vec4(aPos, 1.0)).xyz;
    
    // 法线处理
    mat3 normalMatrix = transpose(inverse(mat3(modelMat)));
    vec3 worldNormal = normalMatrix * aNormal;
    
    #if FLIP_Y_AXIS
        // Vulkan: Y轴翻转
        vNormal = vec3(worldNormal.x, -worldNormal.y, worldNormal.z);
    #else
        // OpenGL: 保持原样
        vNormal = worldNormal;
    #endif
    
    gl_Position = projMat * viewMat * vec4(worldPos, 1.0);
}
```

**优点**：
- ✅ 可以精确控制每个平台的差异
- ✅ 适合需要大量平台特定优化的场景

**缺点**：
- ❌ Shader 代码复杂，充满宏定义
- ❌ 维护困难，容易出错
- ❌ 调试困难（不同平台执行不同代码路径）
- ❌ 编译时需要为每个平台生成不同版本

---

### 方案3：SPIRV-Cross 自动转换（自动化但依赖工具链）

**核心思想**：使用 SPIRV-Cross 在运行时将 SPIR-V 转换为目标平台的 Shader 代码。

#### 实现方式

```cpp
// VulkanRender - 直接使用 SPIR-V
loadShader("Lit.spv");

// OpenGLRender - SPIR-V → GLSL 转换
#include <spirv_cross/spirv_glsl.hpp>

std::vector<uint32_t> spirv = loadSPIRV("Lit.spv");
spirv_cross::CompilerGLSL compiler(spirv);

// 配置转换选项
spirv_cross::CompilerGLSL::Options options;
options.version = 450;
options.es = false;
options.vertex.flip_vert_y = false;  // OpenGL 不翻转 Y
compiler.set_common_options(options);

// 生成 GLSL 代码
std::string glsl = compiler.compile();
compileGLSL(glsl);
```

**优点**：
- ✅ 完全自动化，无需手动处理差异
- ✅ 官方支持，稳定可靠
- ✅ Shader 代码只需编写一次（SPIR-V）

**缺点**：
- ❌ 依赖 SPIRV-Cross 工具链
- ❌ 增加运行时开销（首次加载时转换）
- ❌ 生成的代码可读性差（调试困难）

---

### 方案4：混合方案（实践推荐 ⭐）

**核心思想**：结合方案1和方案2的优点，投影矩阵转换 + 关键差异使用宏。

#### 实现方式

**架构分层**：
```
┌─────────────────────────────────────┐
│  Camera 层（逻辑层）                  │
│  - 生成后端无关的标准矩阵              │
│  - 统一使用 RH_ZO (Vulkan 风格)       │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│  RenderTarget 层（后端适配层）         │
│  - VulkanRenderTarget: Y 轴翻转      │
│  - OpenGLRenderTarget: Z 范围转换     │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│  Shader 层（渲染逻辑）                 │
│  - 统一按 Vulkan 坐标系编写            │
│  - 仅关键差异使用宏（可选）             │
└─────────────────────────────────────┘
```

**代码示例**：
```glsl
// 主要逻辑按 Vulkan 标准编写（90%的代码）
vec3 lightDir = normalize(light.position - vPos);
float diff = max(dot(norm, lightDir), 0.0);

// 仅在必要时使用宏处理关键差异（10%的代码）
#ifdef BACKEND_SPECIFIC_OPTIMIZATION
    // 平台特定优化代码
#endif
```

**优点**：
- ✅ 兼顾代码简洁性和灵活性
- ✅ 易于维护
- ✅ 性能优秀

---

## 本项目采用的方案

### 当前实现（方案1）

**1. Camera 层（`Engine/Source/Core/Camera.h`）**：
```cpp
void setPerspective(float fov, float aspectRatio, float nearClip, float farClip) {
    // 使用 Vulkan 兼容的投影矩阵（右手坐标系，Z: [0,1]）
    // Y 轴翻转由渲染后端处理
    projectionMatrix = glm::perspectiveRH_ZO(glm::radians(fov), aspectRatio, nearClip, farClip);
    recalculateViewProjectionMatrix();
}
```

**2. VulkanRenderTarget（`Engine/Source/Platform/Render/Vulkan/VulkanRenderTarget.cpp`）**：
```cpp
void VulkanRenderTarget::getViewAndProjMatrix(glm::mat4& view, glm::mat4& proj) const {
    // ... 获取投影矩阵 ...
    
    // Vulkan Y-flip (viewport inverted)
    proj[1][1] *= -1;
    
    view = /* 获取视图矩阵 */;
}
```

**3. Shader 层（`Engine/Shader/GLSL/Test/Lit.glsl`）**：
```glsl
void main() {
    // 统一按 Vulkan 坐标系编写
    vec3 worldPos = (modelMat * vec4(aPos, 1.0)).xyz;
    mat3 normalMatrix = transpose(inverse(mat3(modelMat)));
    vec3 worldNormal = normalize(normalMatrix * aNormal);
    
    // 光照计算（后端无关）
    vec3 lightDir = normalize(light.position - worldPos);
    float diff = max(dot(worldNormal, lightDir), 0.0);
    
    gl_Position = projMat * viewMat * vec4(worldPos, 1.0);
}
```

### 未来扩展（添加 OpenGL 后端）

当需要添加 OpenGL 渲染后端时，只需实现：

```cpp
// Engine/Source/Platform/Render/OpenGL/OpenGLRenderTarget.cpp
void OpenGLRenderTarget::getViewAndProjMatrix(glm::mat4& view, glm::mat4& proj) const {
    proj = camera.getProjectionMatrix();
    
    // 转换 Z 范围：[0,1] → [-1,1]
    proj[2][2] = proj[2][2] * 2.0f - proj[3][2];
    proj[2][3] = proj[2][3] * 2.0f;
    
    // OpenGL 不需要 Y 轴翻转
    
    view = camera.getViewMatrix();
}
```

**Shader 代码无需修改！**

---

## 投影矩阵转换公式参考

### Z 范围转换：[0,1] → [-1,1]

```
NDC_GL.z = NDC_VK.z * 2 - 1

投影矩阵变换：
proj[2][2] = proj[2][2] * 2.0 - proj[3][2]
proj[2][3] = proj[2][3] * 2.0
```

### Y 轴翻转

```
proj[1][1] *= -1
```

---

## 最佳实践建议

1. **统一标准**：选择一个主要平台（Vulkan/DirectX）作为标准坐标系
2. **分层封装**：坐标系转换逻辑封装在 RenderTarget 层
3. **Shader 简洁**：避免在 Shader 中使用大量宏定义
4. **文档清晰**：明确标注各层使用的坐标系约定
5. **测试覆盖**：为每个后端编写光照和深度测试用例

---

## 常见问题排查

### 问题1：光照方向前后颠倒

**原因**：Z 轴方向不一致  
**解决**：检查投影矩阵 Z 范围转换是否正确

### 问题2：物体上下颠倒

**原因**：Y 轴翻转处理不当  
**解决**：检查 `proj[1][1] *= -1` 是否在正确的后端应用

### 问题3：深度测试失败

**原因**：NDC Z 范围不匹配  
**解决**：确保投影矩阵和深度缓冲格式一致

### 问题4：法线方向错误

**原因**：法线矩阵变换未考虑坐标系  
**解决**：使用 `transpose(inverse(mat3(modelMat)))` 变换法线

---

## 参考资源

- [Vulkan Specification - Coordinate Systems](https://registry.khronos.org/vulkan/specs/1.3/html/chap24.html#vertexpostproc-coord-transform)
- [OpenGL Specification - Coordinate Transformations](https://www.khronos.org/opengl/wiki/Coordinate_Transformations)
- [GLM Projection Functions](https://github.com/g-truc/glm/blob/master/manual.md#section5)
- [SPIRV-Cross Documentation](https://github.com/KhronosGroup/SPIRV-Cross)

---

*最后更新：2025-12-19*
