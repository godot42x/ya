# Memory Index

只有在以下情况才读取 memory：

- 当前问题像历史回归或老坑重现
- 需要查之前的调试结论、局部约定、踩坑记录
- 需要决定某个结论该进 skill 还是 memory

## 当前条目

- `./animation_system_debug.md`：骨骼动画、shadow pass、root transform、shader define 相关历史问题
- `./vulkan_submit_lifecycle_debug.md`：Vulkan / MoltenVK submit 期生命周期与 keepalive 排查
- `./ibl_visual_regression_baseline.md`：IBL / environment lighting 视觉回归的固定观察基线
- `./rendergraph_point_shadow_indirect_cull_regression.md`：point shadow indirect cull、RenderGraph compile fail、旧帧冻结与 usage contract 回归
- `./windows_dll_boundary.md`：Windows 下 DLL boundary、单例/注册表重复实例、ImGui/反射状态分裂等问题
- `./windows_msvc_compile_portability.md`：从 macOS 切到 Windows/MSVC 的编译/链接故障清单（C7560、ENGINE_API 导出、class/struct 修饰名、POSIX 头、__VA_OPT__）
- `./module_split_sed_regression.md`：模块拆分时 sed 行号错位误删成员函数 → dylib 未定义符号 → 运行时跳 0x0 崩溃；删除/核对函数清单的方法
- `./render2d_multi_flush_vertex_overwrite.md`：Render2D 单帧多批次 flush 时 host-visible 顶点缓冲被后批次覆盖 → GUI 只渲染最后一个批次；clip 与 flush 顺序约定
- `./gui_lifecycle_teardown_and_first_frame.md`：GUI/editor 渲染生命周期三类坑——首帧管线 prep 时序（display image 在录制期才创建）、负尺寸布局 → scissor 溢出、VMA teardown 顺序（readback buffer / 资产纹理必须在 allocator 销毁前释放）

## 边界

- 可复用的系统设计、长期工作流、稳定约定写入 `../skills/*/SKILL.md`
- 一次性故障、回归根因、项目历史坑写入 `./*.md`
