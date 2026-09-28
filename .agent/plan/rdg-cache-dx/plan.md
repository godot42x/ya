# RDG 资源缓存与管线自维护缓存收敛（DX 线）

> 建立日期：2026-09-29
> 状态：P0 已落地；P1–P4 待做。
> 起因：HelloMaterial 稳态每帧 1–3 个 DeferredDeletionQueue 析构，根因为
> SceneFamily（帧内有效）每帧重建 skinning SSBO，RDG 按指针判替换。
> 修复见 commit 533003be，本计划收敛同类双缓存问题并提升开发体验。

## 1. 原则

> **一份 GPU 资源只有一个 owner；RDG 只做 transient 别名池与执行期
> barrier，外部生命周期的资源归 device 侧 scene / view 缓存；import 条目
> 只做执行期 borrow 加保活，不做替换决策。**

失效只讲三种语言：scene 级认内容版本加显式 drop，view 级认 viewId 加
 generation，flight 环只认 fence。不再有第四种（指针比较猜测）。

## 2. 现状地图（2026-09-29）

RDG `RenderGraphResourceRegistry` 拥有：transient 纹理池、transient buffer
槽、owned buffer、imported 纹理条目、imported buffer 条目；失效靠 handle
存活裁剪加 desc / 指针比较。

管线与 device 侧拥有：`ViewTargetStore` 按 viewId 存附件（GBuffer、depth、
 color，带 generation）、`SceneSkinningCache` 按 scene 存 flight 环 buffer、
各管线 DSL 与 `RenderViewBindingTable` 的 per-view binding 槽、submission
池的上传 arena 与描述符 lane、recorder 的顶点与 UBO flight 环。

已知并存点：view 附件在 ViewTargetStore 与 registry import 条目各一份；
arena 切片与 frame UBO 在 arena 与 imported buffer 条目各一份；transient
语义在 graph 槽与管线草稿缓冲两处出现。P0 已把 skinning 收敛为 scene
缓存加 import 直引（commit 533003be），同类问题不再逐个打补丁。

## 3. 边界表

| 层 | 拥有 | 不得拥有 |
| --- | --- | --- |
| RDG registry | transient 别名池与槽、执行期 barrier 计算、import 的 borrow 加保活 | 外部生命周期资源的替换决策、scene / view 语义 |
| device 侧 scene / view 缓存 | 有外部生命周期的 GPU 资源（附件、skinning、IBL 派生）与内容版本判定 | 执行顺序、barrier |
| pass | 读写声明、immutable 快照消费 | 直接创建跨帧 GPU 资源、向当前状态提问 |

## 4. Phase 与验收

P0（已落地）：skinning 跨帧缓存。验收：`ya-render-3d-test` 182 过；
HelloMaterial 200 帧除 warmup 外零 flush；commit 533003be。

P1：graph 可读 dump 加悬空依赖 warning。dump 每帧 pass 顺序、资源
生产消费关系、transient 别名归属；executor 对声明了读写但未形成
 happens-before 边的依赖报 warning。验收：行为零变化（测试全过，flush
 日志与基线一致）；dump 可读，能定位一次历史 churn 到具体 pass。

P2：import 条目瘦身。registry import 条目不再 own 资源，不再做指针与
 desc 比较替换；替换决策只发生在真正的 owner 层。验收：已有测试全过；
HelloMaterial 稳态零 churn；一次模拟 owner 重建只触发单层退役。

P3：统一失效语言。scene 级内容版本加 `dropScenesAbsentFrom`，view 级
 viewId 加 generation，flight 环只认 fence；删除指针比较路径。验收：
逐个缓存迁移，每步测试全过且 flush 基线不变。

P4：pass 侧登记入口收敛。`makeImportedBufferDesc` 加 retained 的手写三
件套包成统一入口。验收：调用点收敛，新 pass 按模板一次写对；测试全过。

## 5. 明确不做

- 不把两套缓存硬合成一个 God object；不把 transient 别名管理搬出 registry。
- 不退回手写执行顺序；barrier 继续由 executor 统一计算。
- 不改 snapshot 抽取与 View 声明语义（归 render-view-family 线）。

## 6. 验证基线命令

```bash
python3 Script/ya.py cfg
xmake b ya-render-3d-test && xmake r ya-render-3d-test
python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=200 --log-level=trace 2>&1 | grep -E "DeferredDeletionQueue::flush"
```

稳态基线（P0 后）：除 warmup 帧（约 frame 2/4/5/10/12）外零 flush，退出时
一次 flushAll。任何 phase 不得让稳态出现逐帧 flush。
