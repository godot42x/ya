# Windows DLL Boundary

这是 render/runtime 边界下的专项补充，聚焦 Windows 动态库边界。

## 核心规则

1. 有全局状态的 runtime 库不能被多个 DLL 各自静态持有。
2. 注册表、单例、延迟初始化队列、UI context、全局缓存都应有唯一 owner。
3. 模板/宏注册可以多模块执行，但注册目标必须是同一份共享状态，且注册 API 要幂等。
4. 能力上若允许，优先由 engine 封装第三方 ABI，避免 editor / project module 直接跨 DLL 调第三方库。

## YA 当前明确高风险的对象

1. ImGui / ImGuizmo
2. reflects-core / ClassRegistry / EnumRegistry
3. ECSRegistry / deferred reflection init queue

## 推荐阅读

- 历史问题与处理细节见 `../memories/windows_dll_boundary.md`

