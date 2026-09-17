# 启动失败路径的 teardown 顺序：App / Module 生命周期倒挂

> 2026-09-18，做「agent 开发时多实例长驻」时定位。让 `App::init` 在自动化控制端口被占用时
> 抛异常之后，进程以 SIGSEGV / SIGTRAP（139 / 133）退出并留下 crash report，而预期只是
> 一个非零退出码。App teardown 与模块 teardown 的顺序已在本切片修好。

## 现象

端口被占用启动 `ya-runtime --editor`：日志里已有
`Automation control port <p> is unavailable; refusing to start without it`，但退出码是
139/133，`~/Library/Logs/DiagnosticReports/ya-runtime-*.ips` 多一份崩溃报告。
崩溃点随修法漂移：先崩在 `App::~App → NativeWindowManager::shutdown → SDL_DestroyWindow
→ AppKit`，再崩在 `App::detachModules → getRuntimeModule`。

## 根因（三处，同一个「谁按什么顺序 teardown」）

1. **App 只有一条有序 teardown 路径，而失败路径没人走它。** `quit()` 的顺序是
   `_deleter.clear()`（软件栈）→ 资产纹理 → render coordinator/device → native window
   manager → factories；`App` 析构本身不是这个顺序：`_deleter` 声明在 `_renderState` 之前，
   析构顺序反而让 `_deleter` 在 `_renderState` 释放之后才跑，SDL window 又在 device 之后才拆。
   `init` 抛异常时没人调 `quit()`，于是隐式析构顺序生效 → 崩在平台窗口 teardown。
2. **模块的 `onStop()` 与 App 生命周期倒挂。** `ModuleManager` 是 `main` 里声明在 `App` 之前的
   局部对象，析构在 App 之后；editor 的 `onStop → persistLayout` 读 `_app->getRenderServices()`，
   宿主已经析构 → 悬垂 `App*`。
3. **修第 2 处时的反向错误：`unloadAll()` 不能早于 App 析构。** `App::_modules` 存的是裸
   `IModule*`，App 析构时 `detachModules()` 仍要用；把 unload 一起提到 App 之前，直接崩在
   `getRuntimeModule()` 解空指针。

## 不变量

- App teardown 只有一条有序路径 `quit()`；析构必须兜底调用它（判据 `App::_instance == this`
  = init 跑过且还没 quit，单元测试里那种「只构造不 init」的 App 因此不受影响）。
- 模块相对 App 的顺序固定为：`stopAll()`（onStop 需要 App 作为宿主）→ App 析构（detach 用裸
  指针）→ `unloadAll()`（销毁模块实例）。三者的相对次序不能合并、不能换位。
- `App::_instance` 必须在 teardown 时清空：信号处理、mouse picking、`App::get()` 那些路径
  否则会读到已释放的 App。
- 任何「启动中途失败」的路径都要走完整 teardown，而不是靠栈展开把对象丢给隐式析构。

## 排查入口

`~/Library/Logs/DiagnosticReports/<proc>-*.ips`：跳过首行 JSON，解析 `threads` 里
`triggered == true` 那一组的符号链（或找 `faultingThread` 索引）。崩溃落在 `~App`、模块钩子、
`ModuleManager` 里时，先问「谁负责按什么顺序 teardown」，而不是先盯崩溃点本身。

## 已知未修（另行处理）

`ModuleManager::startAll` 中途失败时 `_started` 仍是 false → 内部 `stopAll()` 空转，已经
`onStart` 成功的模块拿不到 `onStop`。属于同一类生命周期缺口，与本切片目标无关，未一并改。
