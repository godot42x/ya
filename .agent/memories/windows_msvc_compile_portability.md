# Windows / MSVC 编译可移植性故障清单

## 背景

代码主要在 macOS (Clang) 上开发，切到 Windows (MSVC 2026, VS 18 Insiders) 后全量构建
失败。本文记录 2026-08 一次性修完的故障清单与修复方式；稳定规则已收敛到
`./skills/cross-platform/SKILL.md`，这里只留具体案例与定位手法。

## 故障清单

### 1. POSIX socket / unistd 头在 Windows 不存在

- `Engine/Test/Source/AppAutomationControlJsTest.cpp`：用了
  `<arpa/inet.h> <netinet/in.h> <sys/select.h> <sys/socket.h> <unistd.h>` 裸写 socket
  客户端，`fatal error C1083`。
- 修复：测试客户端改用 asio（与 `AppAutomationControlService.cpp` 服务端一致），
  `pickFreePort` 用 acceptor 绑定端口 0；`tryReadLine` 用 non-blocking socket 轮询保持
  原超时语义。
- 配套：`Engine/Test/xmake.lua` 的 `ya-testing` 增加 `add_packages("asio")`（依赖的包
  不会自动透传）。

### 2. 测试用 getpid()

- `ScriptApiTest.cpp` / `ScriptApiLibraryTest.cpp`：`unistd.h` 的 `getpid()`。
- 修复：Windows 用 `<process.h>` 的 `_getpid()`，包了 `currentProcessId()` 小函数。

### 3. Designated initializer 乱序（C7560）

- `RenderGraphCoreTest.cpp` 8 处 `BufferCreateInfo{.label, .size, .usage}` 与声明顺序
  （label, usage, data, size, memoryUsage）不符。Clang 不报错，MSVC 报
  `error C7560`。
- 修复：按声明顺序重排。全仓已有同类 pre-existing 修复（`ForwardFrameGraphPasses.cpp`
  的 `AttachmentDescription` 等），说明该坑反复出现。

### 4. 跨模块符号未导出（LNK2019）

编辑器/测试跨 DLL 使用以下符号，但类/函数未标 `ENGINE_API`：

- `FrameUploadArena::Allocation`（嵌套结构体不会随外层类导出，需单独标）
- `TextureUploadService`（struct 整体未标）
- `ViewportOverlayStage`（struct 整体未标）
- `DeferredFrameResourceSet`（class 整体未标，测试经 TestAccess 访问私有 static）
- `drawPhysicsCollisionDebug`（自由函数未标，且头文件缺 `Core/Api.h`）

修复：按 `Core/Api.h` 的 `ENGINE_API` 约定逐个补齐。

### 5. class/struct 前向声明不一致（LNK2001 + C4099）

- `Module.h` 里 `class App;`（实际是 `struct ENGINE_API App`）、
  `struct Event;`（实际是 `class ENGINE_API Event`）。MSVC 对 class/struct 生成不同
  修饰名，`HelloMaterial.dll` 的 `onAttach/onDetach` 跨 TU 链接失败：
  obj 里是 `AEAUApp`（struct），引用方是 `AEAVApp`（class）。
- 修复：`Module.h` 对齐为 `struct App;` / `class Event;`。
- 定位手法：`dumpbin /symbols xxx.obj` 对比两侧修饰名；C4099 警告是线索。

### 6. xmake 丢弃 /Zc:preprocessor

- `log.cc` 的 `__VA_OPT__` 宏在 MSVC 需要 `/Zc:preprocessor`，但
  `add_cxflags("/Zc:preprocessor")` 被 xmake auto-ignore 静默丢弃（编译命令行里没有）。
- 修复：`add_cxflags("/Zc:preprocessor", { force = true })`。
- 注意：根 `xmake.lua` 里 `/JMC` 等也有同类 auto-ignore 警告，只是不影响构建。

## 通用定位手法

- 先 `xmake b` 全量构建收集所有报错，再逐类修，避免一次只看一个。
- 链接失败先区分 `LNK2019`（导出缺失/签名不匹配）与 `LNK2001`（声明不一致/
  定义缺失），用 `dumpbin /exports` 与 `/symbols` 核对。
- 怀疑导出缺失时，先确认符号是否真的进了 DLL 导出表，再决定是加导出还是改调用方。

## 关联

- 稳定规则：`./skills/cross-platform/SKILL.md`
- 构建入口：`./skills/ya-build/SKILL.md`
