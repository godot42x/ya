## Profiling

- profiling、automation trace 与 speedscope 规则已独立到 `profiling` skill。
- 若任务重点是 profile 模式、CPU trace、RenderDoc、automation 配置，转到 `../../profiling/SKILL.md`。
- 最小入口仍是：

```bash
python3 Script/ya.py cfg --mode profile
python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=300 --log-level=warn --log-detail-level=error
```



## 当前仓库锚点

- `Script/ya.py`：引擎项目流程入口，封装 `cfg / build / run / run-editor / package / test`
- `makefile`：两种模式的薄包装（模式 1：`make b|r TARGET=...`；模式 2：
  `make run|run-editor|build|package t=...`）
- `xmake.lua`：全局规则、`compile_commands.autoupdate`
- `Engine/Shader/Shader.xmake.lua`：shader 生成入口
- `Engine/Test/Test.xmake.lua`：测试 target 定义 + `xmake test` action；`Engine/Test/Source/<Gate>/` 按门禁分目录收源码
- `test/Test.xmake.lua`：作者 scratch 原型（`bus` / `type_size` / `LazyStatic`），**不在** `test` group 里（不是 gtest 用例，`bus.cpp` 还会卡在 `std::cin`）
- `Example/`：可运行示例目标



## 并行 agent 的独占构建锁

同一仓库、同一构建目录同一时间只能有一份 xmake 构建 / `ya.py test` / `ya.py run` 在跑。多个 agent
并行时用 `Script/agent/ya_lock.sh` 排队，并且能看到是谁占着：

```bash
export YA_AGENT_NAME=<你的名字>                       # 让别人看得到你是谁
Script/agent/ya_lock.sh run -- python3 Script/ya.py test --target ya
Script/agent/ya_lock.sh run -- python3 Script/ya.py run --project <p> -- --exit-after-frame=120
Script/agent/ya_lock.sh status                    # free，或「held by 'x' (pid) for Ns: <命令>」
```

- `run` 拿不到锁会等待，每 30 秒打印一次占用者；默认 3600 秒超时，退出码 75（`YA_BUILD_LOCK_TIMEOUT` 可改）。
- 锁是目录 `/tmp/ya-build.lock.d`（`YA_BUILD_LOCK_DIR` 可改）+ pid/owner/cmd/since；占用者进程死掉会被自动回收，不用手工删。
- 锁内嵌套 `run` 直接执行（`YA_BUILD_LOCK_HELD=1`），不会死锁。
- 只包「自己会结束」的命令（构建、测试、带 `--exit-after-frame` / `--max-lifetime-seconds` 的运行）。**不要把长驻进程放在锁里**（编辑器常驻、`control start` 的实例）。
- 并行时只有一个 agent 的构建在跑，其余在等；纯读代码、写代码、跑 Python 脚本不需要锁。派发并行 worker 时，prompt 必须要求它们用这个锁，并说明各自的文件范围互不重叠（见 `subagent-delegation`）。



## 共享缓存（多 worktree / 多 agent 并行）

大体积产物（Vulkan SDK、重型 submodule）以**真实文件**存放在主项目自己的
`Engine/ThirdParty` 里（SDK 在 `Engine/ThirdParty/VulkanSDK/`），其他
worktree 通过目录链接（macOS/Linux symlink，Windows junction）指回主项目
的同一份，避免各自下载/存储。系统目录（~/Library/Caches 等）从不写入：
删除主项目目录即整体清理，不留垃圾。

### 开一个独立 worktree（换构建模式又不影响别人时用）

共享工作区只有一个 xmake 配置（`xmake f -m profile` 会改掉所有人正在用的
配置），所以 profile / gui profile 这类模式要去独立 worktree 里跑：

```bash
git worktree add /tmp/ya-profile HEAD        # 从主项目里执行，别用 --detach 以外的姿势
cd /tmp/ya-profile && python3 Script/ya.py cfg --mode profile
```

`cfg` 会做三件事：把重型 submodule 占位目录换成指回主项目的链接、把
Vulkan SDK 链回来、配置 xmake。**在它之前不要跑裸 `git submodule update`**，
否则重型仓库会先被完整拉下来，脚本才把干净的 checkout 转回链接。
跑完 `git status` 应当是干净的（重型路径带 skip-worktree 标记）。

实测（2026-10-09，macOS）：`git worktree add` + `cfg --mode profile` 全程没有
任何下载，重型 submodule 与 SDK 都是链接，配置直接可用。

### 三方库复用

- **xmake 包**（glm / entt / sol2 / lua / nlohmann_json / libsdl3 /
  vulkan-headers / vulkan-memory-allocator / gtest …）装在全局
  `~/.xmake/packages`（约 600 MB），**所有 worktree 直接复用**，不需要每个
  worktree 重新拉。worktree 里没有独立的包目录，只有 `.xmake/macosx`
  （xmake 自己的配置缓存）。
- **Vulkan SDK**（约 1.3 GB，gitignore）见下：主项目真实目录，worktree
  symlink。
- 所以真正随 worktree 数量线性增长的只有 **`build/`**（见下）。

- 主项目解析：`$YA_CACHE_ROOT` → `git config ya.cacheRoot`（独立 clone 想
  共享时设成主项目路径）→ 自动推导（git worktree 的 `--git-common-dir`
  指向主项目，所有 worktree 自动共享，零配置）。
- 主项目本身保持标准 git 语义：submodule 是真实 checkout，
  `git submodule status/update` 正常可用；只有链接的 worktree 需要
  skip-worktree。
- 旧版布局（系统缓存 `~/Library/Caches/ya-engine`、`<主>/.ya-cache`、以及
  非主 checkout 里的真实目录）会在首次运行时自动迁移回主项目原位，不会
  重新下载。
- macOS Vulkan SDK：`Script/setup_vulkan_sdk_macos.py` 装到
  主项目的 `Engine/ThirdParty/VulkanSDK/`（`YA_VULKAN_SDK_ROOT` 可单独
  覆盖），主项目内是真实目录，worktree 内是指向它的 symlink；安装原子化
  （pid 唯一临时目录 + rename），并发执行不会损坏。
- 重型 submodule（`Vulkan-Samples-Assets` ~1GB、`LearnOpenGL` ~250MB）：
  `Script/setup_submodules.py` 在主项目里保持真实 checkout（标准 git），
  worktree 内路径是指回主项目的链接，通过 `git update-index --skip-worktree`
  保持 `git status` 干净。worktree 看到的 submodule 内容跟随主项目。

注意：
- 在**链接的 worktree** 里，重型 submodule 路径上不要跑裸
  `git submodule status/update`（会报 "expected submodule path ... not to be
  a symbolic link"）；主项目里完全正常。小 submodule（ImGui 等）到处都保持
  标准 git 语义。
- 新 clone 先跑 `ya.py cfg` 再手动 `git submodule update`，否则重型
  submodule 会先被完整拉下来；脚本会把干净的 checkout 自动转回链接。
- 不要在主项目里跑 `git clean -fdx`（会清掉 gitignore 的
  `Engine/ThirdParty/VulkanSDK/` 触发重新下载）；误清后重跑
  `python3 Script/ya.py vulkan-sdk-macos` 即可恢复。
- 若链接目标异常，先跑 `python3 Script/ya.py cfg` 再看构建。
- **每个 worktree 有自己的 `build/`**：一份 debug 构建约 11 GB，profile 同量级。
  把 worktree 放 `/tmp` 并不省空间（macOS 上 `/tmp` 和数据盘是同一卷），
  开第二个构建前先 `df -h` 确认有 10 GB 以上可用；构建中途写满会报
  "cannot copy file … No space left on device"，且会连带拖垮其他 agent 的构建。
- **删 worktree 前要先 `chmod -R u+w <path>`**：`setup_3rd_party.py` 把三方目录
  设成只读（`0o555`），否则 `git worktree remove` / `rm -rf` 会一路
  `Permission denied` 留下半个目录，还要手动 `git worktree prune`。
  删完记得 `df -h` 确认空间回来了。


