#pragma once

namespace ya
{

/**
### 一、方案2（代码生成）vs 方案3（SPIR-V反射）：哪个更好？
两者没有绝对的“更好”，**取决于项目规模、协作需求和灵活性要求**，可以根据以下维度选择：

| 维度                | 方案2：代码生成（JSON+脚本）       | 方案3：SPIR-V反射                |
|---------------------|-----------------------------------|----------------------------------|
| **核心优势**        | 静态结构体访问快、编译时校验、易版本控制 | Shader是唯一真相源、无需维护中间格式、灵活性极高 |
| **工具链复杂度**    | 低（仅需简单脚本）                | 中（需集成反射库，可离线缓存优化） |
| **维护成本**        | 中（修改成员需重新生成代码）      | 低（修改Shader无需改C++代码）    |
| **编译时安全**      | 高（`static_assert`直接报错）     | 中（需运行时/离线校验）          |
| **适用场景**        | 中大型游戏项目、多人协作、追求性能 | 引擎开发、Shader频繁迭代、高度灵活 |

#### 最优折中：两者结合！
很多商业引擎（如Unity、Godot）的做法是：
1. **离线用SPIR-V反射**提取Shader的UBO/SSBO布局；
2. **自动生成带对齐的C++静态结构体**；
3. 兼顾「方案2的性能和编译时安全」+「方案3的灵活性」。


### 二、直接用C/C++写Shader：避免跨语言对齐的终极方案
纯C++写Shader并直接编译成SPIR-V的方案较少，但有几个**语法高度接近C++、且能完美解决对齐问题**的替代方案：

---

#### 方案A：HLSL for Vulkan（最推荐，语法90%像C++）
HLSL是微软为DirectX设计的Shader语言，**语法和C++几乎完全一致**，且Vulkan通过`glslangValidator`/`shaderc`可以直接把HLSL编译成SPIR-V，跨语言对齐问题比GLSL少很多。

##### 1. HLSL与C++的完美同步
HLSL支持`std140/std430`布局，且可以用`__declspec(align(16))`/`packoffset`手动控制对齐，和C++结构体一一对应：
```cpp
// C++侧：用__declspec(align(16))对齐
struct __declspec(align(16)) FrameData {
    glm::mat4 viewProj;       // 偏移0
    glm::vec3 lightPos;        // 偏移64
    float lightIntensity;      // 偏移76（和lightPos凑16字节）
    glm::vec4 lightColor;      // 偏移80
};
static_assert(sizeof(FrameData) == 96, "Size mismatch");
```

```hlsl
// HLSL侧：用std140布局，和C++完全对应
cbuffer FrameData : register(b0) {
    matrix viewProj;           // 偏移0
    float3 lightPos;           // 偏移64
    float lightIntensity;      // 偏移76
    float4 lightColor;         // 偏移80
}
```

##### 2. Vulkan中使用HLSL的步骤
1. 用`shaderc`（Vulkan SDK自带）把HLSL编译成SPIR-V：
   ```bash
   glslangValidator -V shader.hlsl -o shader.spv --hlsl-offsets
   ```
2. 在Vulkan中直接加载SPIR-V，和加载GLSL编译的SPIR-V完全一致；
3. C++结构体直接`memcpy`到UBO，无需担心对齐。

---

#### 方案B：Slang（语法100%像C++，跨平台神器）
Slang是NVIDIA主导的开源Shader语言，**语法和C++完全一致**（支持泛型、接口、运算符重载等C++特性），可以直接编译成SPIR-V、DXIL、GLSL，且**内置跨语言反射机制，自动处理对齐问题**。

##### 1. Slang的核心优势
- **同一套代码，C++和Shader共用**：Slang中定义的结构体可以直接在C++中使用，无需手动同步；
- **自动对齐**：Slang编译器自动处理std140/std430布局，C++侧直接用Slang生成的头文件；
- **多平台支持**：一次编写，编译成Vulkan/DirectX/Metal的Shader。

##### 2. 示例：Slang统一代码
```slang
// FrameData.slang：C++和Shader共用的结构体
struct FrameData {
    matrix viewProj;
    float3 lightPos;
    float lightIntensity;
    float4 lightColor;
};

// Shader代码
[shader("vertex")]
void vert(in float3 aPos : POSITION, out float4 vPos : SV_POSITION) {
    vPos = viewProj * float4(aPos, 1.0);
}
```

```cpp
// C++侧：直接包含Slang生成的头文件
#include "FrameData.slang.h" // Slang编译器自动生成

int main() {
    FrameData ubo;
    ubo.viewProj = glm::mat4(1.0f);
    ubo.lightPos = glm::vec3(1,2,3);
    ubo.lightIntensity = 1.0f;
    // 直接memcpy到UBO，Slang自动处理对齐！
    vkMapMemory(device, uboMemory, 0, sizeof(FrameData), 0, &data);
    memcpy(data, &ubo, sizeof(FrameData));
    vkUnmapMemory(device, uboMemory);
    return 0;
}
```

---

#### 方案C：纯C++编译成SPIR-V（限制多，不推荐）
可以用`LLVM的SPIR-V后端`把C++代码编译成SPIR-V，但**限制极多**：
- 不能用STL、虚函数、异常、动态内存分配；
- 只能用非常简单的C++子集；
- 只适合计算Shader，不适合图形Shader。

示例（简单的计算Shader）：
```cpp
// compute.cpp：纯C++计算Shader
__attribute__((reqd_work_group_size(256, 1, 1)))
void main(uint3 globalId : SV_DispatchThreadID) {
    float a = 1.0f;
    float b = 2.0f;
    // ... 简单计算
}
```
编译命令：
```bash
clang++ -target spirv64-unknown-unknown -c compute.cpp -o compute.spv
```

---

### 三、最终推荐
1. **解决跨语言对齐问题**：优先用**HLSL for Vulkan**（语法像C++、工具成熟、Vulkan支持好）；
2. **追求极致的C++体验**：用**Slang**（语法100%像C++、自动对齐、跨平台）；
3. **方案2 vs 3**：中大型项目用「代码生成」，引擎级项目用「SPIR-V反射」，或者两者结合。

通过以上方案，你可以彻底告别跨语言对齐的痛苦！
 */

struct SLang
{
};


} // namespace ya