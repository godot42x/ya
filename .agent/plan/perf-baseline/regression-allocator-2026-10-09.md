# allocator 层回归报告

2026-10-09 · 范围：`Core/Memory` allocator 层、`process_memory` 汇总段、编辑器 Memory 段、`ya_process_allocator()` 构建接线、`bench_run.py`。
方法：全量构建 + 全部门禁 + profile worktree 实跑基准；每个发现都做「改前 / 改后」对照，不靠推断。

## 结论

| 项 | 结果 |
| --- | --- |
| 全量构建（引擎 / 编辑器 / 示例 / 测试，debug） | 通过 |
| 测试门禁 `xmake r -g test` | **3369 / 3369 通过，0 失败** |
| profile 全量构建（独立 worktree） | 通过 |
| profile 基准实跑（PerfLab / Lights_50） | 通过，`process_memory` 出 `heap.*` 全套 |
| 编译错误 | 3 处，已修（见下） |
| 本次回归发现的实现缺陷 | 2 个，已修并补单测（见下） |

## 修掉的编译错误

### 1. `RenderCompanionSpecs.cpp` 缺 include（HEAD 上就存在，非本次引入）

```
RenderCompanionSpecs.cpp:46:34: error: use of undeclared identifier 'renderFeatureMaskOf'
```

`renderFeatureMaskOf` 声明在 `Render/Adapters/Companion/CompanionManager.h`，而该 cpp 只 include 了 `RenderCompanionSpecs.h`。
**修法**：补 `#include "Render/Adapters/Companion/CompanionManager.h"`（一行，按既有 include 分组顺序插在 `Hierarchy/Node.h` 与 `Scene/Core/Scene.h` 之间）。

这条错误此前一直被增量构建掩盖（目标没被重建），直到我为了验证门控做了 `xmake c` + 全量重建才暴露；它同时也是上一轮 profile worktree 拿不到运行期数字的原因。

### 2 & 3. allocator 层自身的两处缺陷（见下节），表现为 `std::bad_alloc`

## 回归抓到的实现缺陷

### 缺陷 A：`aligned_alloc` 拒绝小对齐 → 每次 boxed 分配都抛 `std::bad_alloc`

- **现象**：`ya-testing` 里 `CoreValueTypes.StorageInlineAndBoxed` / `RoundTripCopyAndMove` / `BoxedMat4CopyIsIndependent` 三个用例失败，异常是 `std::bad_alloc`。
- **归属判定**：在本次改动之前的提交（`e56d5d9f`）建 worktree 跑同一组用例 **13/13 通过** → 是本次引入，不是别人在飞的代码。
- **根因**：`MetaValue` 的 boxed 存储走 `::operator new(size, std::align_val_t(alignof(T)))`。`alignof(glm::mat4) == 4`，而 macOS 的 `aligned_alloc` **拒绝小于 `sizeof(void*)` 的对齐并返回 null**（libc++ 自己是回退到普通 new）。我的 `MallocAllocator::allocate` 把它当错误对齐直接丢给 `aligned_alloc`，于是每次 boxed 分配都失败。
- **修法**：只有真正超对齐（`alignment > alignof(std::max_align_t)`）才走 `aligned_alloc`，其余一律 `malloc`（它本来就满足 `max_align_t`）。
- **单测**：`AlignmentsBelowTheDefaultAreServedRatherThanRefused`（对齐 1/2/4/8/max_align_t 全部可分配并正确释放）、`TheGlobalReplacementServesSmallAlignments`（走真实路由路径）。

### 缺陷 B：`liveBytes` 只是累计量，不是存活量

- **现象**：profile 基准里 `heap.liveMiB` = **4429 MiB**，而同时 `rssMiB` 只有 **360 MiB** —— 一个不可能同时成立的数。
- **根因**：绝大多数全局 `operator delete(void*)` **不传 size**，我只把提示值记进 `freeBytes`，于是 `liveBytes` 只加不减。同一份 summary 里 `freeMiB` 是 0.001 MiB/帧就是征兆。
- **修法**：每个 block 前置一个 size 头部（`{size, baseOffset}`），释放时从头部读回真实大小。过对齐请求在 block 内偏移用户指针，头部记录偏移量，两条路径共用一条还原规则 `base = pointer - kHeaderSize - baseOffset`。
- **修后**（同场景 profile 实跑）：

  | 指标 | 修前 | 修后 |
  | --- | --- | --- |
  | `heap.liveMiB` p50 | 4429.6 | **99.85** |
  | `heap.freeMiB` p50 | 0.001 | **1.58** |
  | `rssMiB` p50 | 360.6 | 301.4 |
  | `heap.liveAllocations` | 298194 | 297672 |
  | `heap.totalAllocMiB`（累计） | 4928 | 1519 |

  修后 live（99.9 MiB）与 RSS（301 MiB）量级吻合：差额是代码段、栈、GPU 映射页与碎片。
- **单测**：`UnsizedDeleteStillReleasesTheRecordedSize`、`OverAlignedBlocksCarryTheirRealSizeAndComeBackAligned`、`ZeroSizedRequestsAreDistinctAndFreeCleanly`。

## 另外修掉的两处工具缺陷

- **`bench_run.py --filter` 精确名匹配不到**：我把匹配从 `c.stem` 改成 fnmatch，而文件名是 `Lights_50.bench.json`，stem 是 `Lights_50.bench`，于是 `--filter Lights_50` 选不出任何东西（只有 `Lights*` 这种通配还能用）。改为对**去掉 `.bench.json` 后缀的场景名**匹配。
- **`ya_process_allocator()` 的构建模式门控不可靠**：见下节。

## 构建模式门控：两处判断都是错的，已改成不问模式

`ya_process_allocator()` 原来用 `is_mode("profile")` 决定是否把全局替换编进模块。实测：

```
[probe] is_mode(profile)=false is_mode(debug)=false get_config(mode)=nil
```

xmake 在**求值项目描述时**还没加载 mode 选项，两个查询都拿不到答案，`get_config("mode")` 甚至是 nil（不是 "debug"）。表现就是门控时灵时不灵：同一份 debug 配置下，增量构建出来的 core **是**接管了 `operator new` 的（`nm` 有 `T __Znwm`），而我按脚本判断应该没有。

**改法**：

1. `ya_process_allocator()` **不再问构建模式**，无条件把替换编进每个模块 —— 一层只在部分构建里存在的 allocator 不是一层；而且这样 debug 的全部门禁和整套测试都会走这条路径，等于持续验证它。
2. 「路由到哪个 backend」搬到 C++，由 `YA_BUILD_MODE_PROFILE` 决定（`Core/Profiling/Profiling.h` 已在用同一组宏，那里的 build mode 是真实可见的）。于是 debug/release 走 `MallocAllocator`，profile 走 `CountingAllocator<MallocAllocator>`。

顺带记录：xmake 在改了源文件列表后**不会**自动重生成 unity 文件，需要清 `build/.gens` 或 `xmake c`；只删生成目录而不清构建状态会让 `xmake b` 报 `no such file or directory: unity_N.cpp`（本次撞到，用 `xmake c` + 全量重建修好）。

## 各项验证明细

### 全量构建（debug，engine profile）

- `xmake b`：通过（含引擎聚合、编辑器、所有示例、测试目标）。
- 编辑器两个相关 TU 单独语法检查：`RuntimeMemorySection.cpp`、`EditorRuntimeToolsTab.cpp` 均 RC 0。

### 测试门禁（`xmake r -g test`）

| 门禁 | 结果 |
| --- | --- |
| ya-testing | **1784 / 1784** |
| ya-gui-closure-test | 641 |
| ya-resource-core-test | 477 |
| ya-ecs-core-test | 209 |
| ya-render-3d-test | 47 |
| ya-render-2d-test | 46 |
| ya-gui-widgets-test | 75 |
| ya-resource-runtime-closure-test | 34 |
| ya-gui-declarative-contract-test | 23 |
| ya-rhi-vulkan-smoke | 18 |
| ya-gui-headless-host-test | 4 |
| ya-module-fixture 等 | 2 |
| **合计** | **3369 通过 / 0 失败** |

新增用例 17 个（`Engine/Test/Source/Core/AllocatorTest.cpp`）：concept 静态断言、零尺寸 / 过对齐 / 小对齐 / 空指针释放、进程路由、未计数构建报不支持、无 size 提示释放、超对齐块的对齐与真实大小、嵌套 owner scope 还原、live 回落、超额释放截断、失败分配不计数、`deltaSince` 语义。

### profile worktree 实跑

- `git worktree add` + `ya.py cfg --mode profile`：零下载（重型 submodule 与 Vulkan SDK 都是链接）。
- 全量构建通过（`ya-runtime` + PerfLab 模块）。
- `bench_run.py --filter Lights_50`：1 次运行成功，`process_memory` 出全套 `heap.*`（见缺陷 B 的修后数据）。

**帧时间对比**：本次 8.758 ms，已入库基线（`15277c00`）8.384 ms。两者不在同一提交、且当时机器有并发构建（本项目已实测并发构建能让同一场景 frame p50 差 87%），因此**不作为性能回归判断**，只作为「能跑通、数量级合理」的证据。要比必须在同一提交、机器空闲时重采。

## 未覆盖 / 后续

- **子系统分桶全是 0**：`resource` / `render` / `script` 三个桶在真实运行里都是 0 MiB，因为还没有在任何子系统入口打 `YA_MEMORY_OWNER`。机制（thread-local + 嵌套还原 + Untagged 兜底）已测，但**没有真实归属数据**。下一步是在资源加载 / 渲染 / 脚本的边界各打一个 scope。
- **Windows 未编译**：这一层在 Windows 上同样接管全局分配（每个 DLL 一份），但没在 MSVC 下编译验证过；`Core/xmake.lua` 里也没有为它加任何链接项（设计上不需要）。
- **性能开销未量化**：profile 下每次分配多一次跨 dylib 调用 + 4 个 relaxed 原子。没有 before/after 数字，需要时用同一条基准命令量。
- **跨模块 new/delete 配对**已由设计保证（全部转发到同一 `processAllocate`），但没有专门的测试覆盖「A 模块 new、B 模块 delete」；现有覆盖是同一模块内。