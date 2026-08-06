---
name: ya-cross-platform
description: Windows (MSVC) 与 macOS/Linux (Clang) 之间的跨平台编译规则、DLL 导出与平台差异清单。
---

## 适用场景

- 在 Windows 与 macOS 之间切换开发设备，遇到另一台设备编译/链接报错
- 提交涉及公共头文件、结构体、类导出或平台 API 的改动前做跨平台自检
- 新增测试、示例或模块时确保两边都能编译链接

## 核心规则

### 1. Designated initializer 必须按成员声明顺序

MSVC 要求 `.field = value` 严格按 struct/class 成员**声明顺序**书写，乱序直接报
`error C7560: designators must appear in member declaration order`；Clang 只是警告，所以在
macOS 上"一切正常"，切到 Windows 才炸。

- 新增或调整结构体成员顺序时，同步检查所有使用点。
- 写聚合初始化时，字段顺序与声明保持一致，即使某个字段有默认值。
- 全仓检查：`rg -n -U '<Type>\{\s*\.label' -g '*.cpp' -g '*.h'`（按实际类型名替换）。
- 典型已踩坑：`BufferCreateInfo` 声明顺序为 `label, usage, data, size, memoryUsage`，
  测试里写成 `.label, .size, .usage` 直接 C7560。

### 2. 跨模块使用必须显式导出 API

Windows 下 DLL 边界是硬边界：类、自由函数、嵌套结构体若被其他模块（ya-engine /
ya-editor / 项目模块 / ya-testing）使用，必须加 `ENGINE_API`（见 `Core/Api.h`），否则
链接期报 `LNK2019 unresolved external symbol`，但源码里定义明明存在。

- 类 / 结构体：`struct ENGINE_API Foo` / `class ENGINE_API Foo`，并 `#include "Core/Api.h"`。
- 自由函数：`ENGINE_API void foo(...)`。
- **嵌套结构体不会随外层类自动导出**：外层类标了 `ENGINE_API`，嵌套的
  `Allocation` 等仍需要自己标（已踩坑：`FrameUploadArena::Allocation`）。
- 测试通过 `TestAccess` 访问私有/静态成员时，被访问的类必须导出（如
  `DeferredFrameResourceSet`）。
- 症状识别：`dumpbin /exports build\windows\x64\debug\ya-engine.dll` 查不到符号，
  但 `.cpp` 里确实有定义 → 99% 是导出缺失。

### 3. class/struct 前向声明必须与定义一致

MSVC 对 `class` 与 `struct` 生成**不同的修饰名**。前向声明关键字与定义不一致时，同一
函数在不同 TU 里被 mangle 成不同符号，链接报 LNK2001（伴随 warning C4099）。

- 前向声明与最终定义保持相同关键字：定义是 `struct App` 就写 `struct App;`，
  定义是 `class Event` 就写 `class Event;`。
- 看到 `warning C4099: type name first seen using 'struct' now seen using 'class'`
  立即修正，不要当噪音忽略。
- 已踩坑：`Module.h` 里 `class App;`（实际是 `struct ENGINE_API App`）、
  `struct Event;`（实际是 `class ENGINE_API Event`），导致 `HelloMaterial.dll`
  `onAttach/onDetach` 链接失败。

### 4. 不使用 POSIX-only 头

Windows 没有 `<unistd.h>`、`<arpa/inet.h>`、`<netinet/in.h>`、`<sys/socket.h>`、
`<sys/select.h>`、`<sys/time.h>`；`ssize_t`、`timeval`、`FD_SET` 等也不存在。

- 网络代码：优先用引擎已依赖的 asio（`AppAutomationControlService` 就是 asio 实现），
  不要在测试/模块里裸写 POSIX socket。
- 进程 ID：Windows 用 `<process.h>` 的 `_getpid()`，其余平台 `getpid()`，包一层小函数。
- 全仓检查：`rg -n "unistd|arpa/inet|sys/socket|sys/select|netinet|ssize_t" Engine Test Example`

### 5. MSVC 预处理器与 __VA_OPT__

`__VA_OPT__` 需要 MSVC 开启 `/Zc:preprocessor`；xmake 的
`add_cxflags("/Zc:preprocessor")` 会被 auto-ignore 策略静默丢弃，**必须写
`{ force = true }`**，否则编译报 `C3861: '__VA_OPT__': identifier not found`。

```lua
add_cxflags("/Zc:preprocessor", { force = true })
```

### 6. 其他 MSVC 差异

- 无变长数组（VLA）。
- `std::format` 的 `basic_format_string` 在 MSVC 是立即函数，宏展开出来的非常量格式串
  会报 C7595；宏里用 `__VA_OPT__` 时格式串必须是字面量。
- `/utf-8` 编译选项已在根 `xmake.lua` 全局开启，新目标不需要重复加。

## 切换设备后的处理流程

1. `python3 Script/ya.py cfg` 刷新本机配置（MSVC 工具链、包缓存不同）。
2. `xmake b` 全量构建收集全部报错（先跑一遍再逐类修，不要看到一个修一个）。
3. 按错误分类对照本 skill：
   - `C7560` → 规则 1
   - `LNK2019/LNK2001` → 规则 2 / 规则 3（用 dumpbin 区分）
   - `C1083 cannot open include file` → 规则 4
   - `__VA_OPT__` / `C3861` / `C7595` → 规则 5
   - 其余 → 规则 6 或查 `ya-build`
4. 收尾验证：`xmake r ya-testing`（全量测试）、`xmake b` 确认 100% 通过。
5. 若修复涉及公共头/导出/xmake 配置，把新坑补回本 skill 或 memory。

## 相关 skills

- `ya-build`：构建、目标、shader 生成、测试入口
- `cpp-style`：代码风格与类布局（跨平台规则之外的部分）
- `debug-review`：提交前自检
- `windows_dll_boundary` memory：DLL 边界上的单例/注册表/状态分裂问题

## 变更约束

1. 跨平台修复保持最小改动，不混入无关重构。
2. 生成文件不直接改；问题来自生成链时回到生成规则修。
3. 优先选择引擎已有抽象（asio、ENGINE_API、stdptr），不平行造新接口。

## 退出条件

- Windows 与 macOS 都能全量构建、链接、跑测试
- 公共头改动遵循导出与声明顺序规则
- 新踩的平台坑已回写本 skill 或 memory
