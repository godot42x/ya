# Shader 热重载架构设计

## 当前限制

### ❌ 不支持的热重载操作
- 修改 descriptor set layout（添加/删除 uniform、texture）
- 修改 push constant 大小或结构
- 修改顶点输入格式（当 `bDeriveFromShader = false` 时）
- 改变 descriptor set 数量

### ✅ 支持的热重载操作
- 修改 shader 函数逻辑（main、自定义函数）
- 修改 shader 内部计算
- 修改已有 uniform 的使用方式（但不能改变类型/大小）

---

## 架构层级

### 当前实现（Simple Reload）

```
Shader 修改
    ↓
ShaderStorage::removeCache()  // 清除缓存
    ↓
Pipeline::reloadShaders()     // 重新编译 + 重建 pipeline
    ↓
✅ 完成（约 50-200ms）
```

**限制**: Descriptor Set Layout、Pipeline Layout 保持不变

---

## 完全动态 Shader 架构（Full Reload）

### 方案 A: 重新初始化材质系统

```cpp
void UnlitMaterialSystem::reloadShaderWithNewLayout()
{
    // 1. 销毁所有资源
    _pipeline.reset();
    _pipelineLayout.reset();
    _materialDSP.reset();
    _frameDSP.reset();
    _materialFrameUboDSL.reset();
    _materialParamDSL.reset();
    _materialResourceDSL.reset();
    
    // 2. 从 shader 反射中重建一切
    auto shaderStorage = App::get()->getShaderStorage();
    shaderStorage->removeCache(_pipelineDesc.shaderDesc.shaderName);
    
    auto stage2Spirv = shaderStorage->load(_pipelineDesc.shaderDesc);
    auto fragReflect = shaderStorage->getProcessor()->reflect(
        EShaderStage::Fragment, 
        stage2Spirv->at(EShaderStage::Fragment)
    );
    
    // 3. 根据反射动态创建 descriptor set layouts
    std::vector<DescriptorSetLayout> dynamicDSLs = 
        buildDescriptorSetLayoutsFromReflection(fragReflect);
    
    // 4. 重新创建所有资源
    createDescriptorSetLayouts(dynamicDSLs);
    createPipelineLayout();
    createDescriptorPools(fragReflect); // 根据反射计算 pool size
    createPipeline();
    
    // 5. 重新分配 descriptor sets
    allocateAllDescriptorSets();
    
    YA_CORE_INFO("Shader fully reloaded with new layout");
}
```

**优点**: 完全灵活，支持任意 shader 修改  
**缺点**: 
- 耗时长（200-500ms）
- 需要重新绑定所有材质数据
- 可能导致一帧渲染中断

---

### 方案 B: 双缓冲架构（推荐）

```cpp
struct MaterialSystemDualBuffer
{
    struct ResourceSet {
        stdptr<IGraphicsPipeline> pipeline;
        stdptr<IPipelineLayout> pipelineLayout;
        stdptr<IDescriptorPool> materialDSP;
        std::vector<IDescriptorSetLayout> dsls;
        // ... 其他资源
    };
    
    ResourceSet _active;   // 当前使用的资源
    ResourceSet _staging;  // 预备的资源
    
    void beginShaderReload()
    {
        // 在后台线程重建 staging 资源
        std::async([this]() {
            buildStagingResources();
        });
    }
    
    void commitShaderReload()
    {
        // 在安全的时机（帧开始）交换
        std::swap(_active, _staging);
        _staging.cleanup(); // 清理旧资源
    }
};
```

**优点**: 无缝切换，不中断渲染  
**缺点**: 内存占用翻倍

---

### 方案 C: Shader 接口契约（当前推荐）

**定义标准接口，shader 必须遵守**

```glsl
// 标准契约：所有 Unlit shader 必须有这些 binding
layout(set = 0, binding = 0) uniform FrameUBO { ... };
layout(set = 1, binding = 0) uniform MaterialUBO { ... };
layout(set = 2, binding = 0) uniform sampler2D tex0;
layout(set = 2, binding = 1) uniform sampler2D tex1;

// ✅ 可以修改：函数逻辑
vec4 customLighting(vec3 color) { ... }

void main() {
    // ✅ 可以修改：main 函数内容
    vec4 color = texture(tex0, uv);
    color = customLighting(color.rgb);
    outColor = color;
}

// ❌ 不能修改：添加新的 uniform
// layout(set = 3, binding = 0) uniform NewUBO { ... }; // 违反契约！
```

**优点**: 
- 简单、高效
- 热重载快（50ms）
- 不需要复杂的资源管理

**缺点**: 
- 灵活性受限
- 需要预先规划接口

---

## Descriptor Pool Size 动态计算

如果要支持动态 shader，pool size 计算方式：

```cpp
DescriptorPoolCreateInfo calculatePoolSizeFromReflection(
    const ShaderReflection::ShaderResources& reflect,
    uint32_t materialCount)
{
    std::unordered_map<EPipelineDescriptorType, uint32_t> typeCounts;
    
    // 遍历反射信息，统计每种类型的 descriptor 数量
    for (const auto& ubo : reflect.uniformBuffers) {
        typeCounts[EPipelineDescriptorType::UniformBuffer]++;
    }
    for (const auto& sampler : reflect.sampledImages) {
        typeCounts[EPipelineDescriptorType::CombinedImageSampler]++;
    }
    
    // 为每个材质实例预留
    std::vector<DescriptorPoolSize> poolSizes;
    for (const auto& [type, count] : typeCounts) {
        poolSizes.push_back({
            .type = type,
            .descriptorCount = count * materialCount,
        });
    }
    
    return DescriptorPoolCreateInfo{
        .maxSets = materialCount * reflect.sets.size(),
        .poolSizes = poolSizes,
    };
}
```

---

## 推荐方案

### 开发阶段（Editor）
**方案 C（Shader 接口契约）** + 快速热重载
- 定义标准接口
- 只允许修改函数逻辑
- 热重载速度快，迭代效率高

### 未来扩展（Runtime Shader Variants）
**方案 B（双缓冲）** + 异步加载
- 支持运行时切换不同 shader
- 不中断渲染
- 用于材质编辑器、效果切换等

---

## 实现建议

### 1. 短期：增强当前架构

```cpp
// 在 UnlitMaterialSystem.h 中添加
void enableShaderHotReload() {
    FileWatcher::get()->watchDirectory(
        "Engine/Shader/GLSL", 
        ".glsl",
        [this](const FileWatcher::FileEvent& e) {
            if (e.path.find(_pipelineDesc.shaderDesc.shaderName) != string::npos) {
                _pipeline->reloadShaders();
                YA_CORE_INFO("Shader hot reloaded: {}", e.path);
            }
        }
    );
}
```

### 2. 中期：Shader 验证工具

```cpp
// 编译时验证 shader 是否符合契约
bool validateShaderContract(const ShaderReflection& reflect) {
    // 检查必需的 descriptor sets
    if (!reflect.hasUniformBuffer("FrameUBO", 0, 0)) return false;
    if (!reflect.hasUniformBuffer("MaterialUBO", 1, 0)) return false;
    if (!reflect.hasSampler("tex0", 2, 0)) return false;
    return true;
}
```

### 3. 长期：材质系统 2.0

- 支持 shader 变体（不同接口的 shader）
- 运行时材质编辑器
- 自动从 shader 生成材质参数 UI

---

## 总结

| 方案 | 灵活性 | 性能 | 复杂度 | 适用场景 |
|------|--------|------|--------|----------|
| 当前（固定接口） | ★☆☆☆☆ | ★★★★★ | ★☆☆☆☆ | 日常开发 |
| 接口契约 | ★★☆☆☆ | ★★★★☆ | ★★☆☆☆ | **推荐** |
| 完全重建 | ★★★★★ | ★★☆☆☆ | ★★★★☆ | 材质编辑器 |
| 双缓冲 | ★★★★★ | ★★★★☆ | ★★★★★ | 高级功能 |

**当前阶段建议**: 采用**方案 C（接口契约）**，兼顾开发效率和灵活性。
