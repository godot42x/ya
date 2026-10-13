## Shader 生成流程

Slang 是引擎唯一的 shader 语言（GLSL / shaderc 路径已退役，`Engine/Shader/GLSL` 与 `GLSLProcessor` 都已删除）。当前链路是配置 + Slang 两段：

```text
Engine/Config/Engine.jsonc
  -> Shader.xmake.lua Step 0
  -> 生成 Engine/Shader/Slang/Common/Limits.slang

Slang 源文件
  -> slang_gen_header.py
  -> Engine/Shader/Slang/Generated/*.slang.h

C++
  -> include 生成头
  -> 使用 slang_types:: 命名空间中的常量与结构体
```

落地规则：

1. 不要手写与 shader-facing layout 对应的 C++ 结构体，包括 UBO、SSBO、push constant、indirect command，以及标了 `[YaVertexInput]` 的顶点输入结构。
2. C++ 侧必须 include `Engine/Shader/Slang/Generated/*.slang.h`，消费 `slang_types::` 里的生成类型。
3. 若生成头缺少需要的 shader-facing 类型，修 shader 源或生成脚本，不在 C++ 侧补 mirror struct。
4. 顶点输入默认不进生成头（varying 绑定没有 uniform offset）。需要 C++ 记录时，结构体标 `[YaVertexInput]`（`Common/VertexInput.slang`），并作为顶点入口参数。Slang 把字段 `binding.index` 记成相对该参数的下标，SPIR-V location 是参数 `binding.index`（基址）加上这个下标。`Sprite2DWorld` 的网格输入占 0–2，所以实例参数写 `[[vk::location(3)]]`、字段不再写 location，生成表里的 location 才是 3–8。若把 3–8 写在字段上，反射下标仍是 3–8，但 SPIR-V 会再加基址变成 6–11。缺 location 的字段生成失败。布局按 glm 默认对齐，并发出 `VertexInputField`（location、offset、分量数、标量种类）。没标的顶点输入保持不发。不要用一条顶点阶段不读的 push constant 去喂生成器。`VertexInputScalar` 留在各 shader 的生成命名空间里；C++ 用 `vertexAttributeFormat` 模板映射到 `EVertexAttributeFormat`，不另做一份共享枚举。
5. 配置常量优先以 `Engine/Config/Engine.jsonc` 为单一事实源，其余文件只消费不重定义。
6. 若值变更，优先改配置或生成脚本，再运行 `xmake ya-shader`。


