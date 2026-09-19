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
- `./rendergraph_import_reuse_wrapper_identity_regression.md`：RenderGraph imported 纹理跨帧复用失效——身份比较误用每帧重建的 `ImageResource` 包装指针（应比较底层 image/view）；症状是每帧 `replacing texture` + 每帧析构
- `./legacy_test_target_break_after_module_move.md`：模块迁移/类型收敛/API 折回后，遗留测试 target 编译失败的常见形态与修复方式（include 失效、API 删除、类型替换、字段可见性、生成头 include、枚举收紧）
- `./derived_visual_on_host_component_slot.md`：把相机机身这类派生视觉塞进宿主组件槽的回归——场景文件被污染、资产路径被钉死、排除逻辑散落五处、宿主丢槽位；正确做法是生成子实体 + 声明式边界
- `./app_teardown_order_and_instance_lock.md`：启动失败路径（控制端口被占用）崩溃 139/133——App 只有 `quit()` 一条有序 teardown 却没人走、模块 `onStop` 在 App 析构后执行、`unloadAll()` 提前于 App 的裸模块指针；含实例锁/墙钟上限运行策略的边界
- `./control_instance_lifecycle.md`：agent 驱动引擎时实例堆积的四个根因——没有发现渠道、帧预算挡不住闲置进程、端口被占静默降级、harness `kill()` 杀的是启动器而不是引擎；对应 control 入口 + 实例记录 + 默认墙钟上限
- `./uninitialized_rect_and_view_rect_contract.md`：未初始化的 `Rect2D`（非规格化小数骗过 `> 0`）被当成 View 尺寸 → 截断成 0×0 → `createTexture` 断言 → 编辑器 exit 255；含从 .ips 指令地址用 `atos` 反查源码行、以及为什么 lldb 会掩盖这类 bug
- `./unity_build_duplicate_private_symbol.md`：批量搬迁重排 unity 批次后，平铺布局掩盖的重复私有符号（结构体/自由函数）突然变成 duplicate symbol；不能用加命名空间糊过去，应抽私有头或改名
- `./pointer_session_lost_release.md`：GUI pointer session 的 press 缓存只有 release 才清，而 release 会丢（focus 丢失、指针离开窗口、注入 press）→ `WidgetTree::beginPointerDispatch` 断言在拖 splitter/dock 时频发 abort；现在由框架 `cancelPointerSession` / `reconcilePointerButtons` 回收并计数
- `./dead_snapshot_channel_survives_empty_input.md`：没有生产者的 `FramePacket::overlay` 通道让 Forward/Deferred 的 overlay pass 每帧空跑（"空输入是合法输入"掩盖了死通道）；含"兜底链恒非空分支即死代码"（`resolveViewportExtent`）
- `./reflected_transform_write_bypasses_child_dirty.md`：反射/undo/反序列化直写 `TransformComponent` 字段绕过 setter，子节点 world matrix 不标脏 → 生成物（相机机身）停在旧位置；含"父脏必然子脏"应落在 `updateNodeTree` 的理由
- `./component_created_without_owner.md`：scene.json 载入的组件 `_owner` 为空（反序列化走的是不知道 `Entity*` 的按名字 funnel）→ 相机 `getFreeView()` 落到 orbit 默认分支，预览与视锥线框一起停在世界原点而 mesh 在 authored 位姿；含"枚举全部 emplace 路径"的排查法

## 边界

- 可复用的系统设计、长期工作流、稳定约定写入 `../skills/*/SKILL.md`
- 一次性故障、回归根因、项目历史坑写入 `./*.md`
