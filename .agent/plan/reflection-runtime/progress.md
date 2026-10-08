# Progress

## 2026-10-08 — 立项

- 来源：S5.1 审查与用户复核暴露反射层与脚本层的结构问题（两套动态值、五处类型 if 链、`Function::payload` 盲盒、脚本层自算继承展开、public 可变注册表）。
- 方案问答：范围五处消费方全收；值模型开放 MetaValue 放 `reflects-core`（零依赖）；动态注册走版本号 + 通知；S5 暂停并入 RR8；P1，先于 C3 / G3.2。
- 用户纠正：动态注册是正当需求（Godot extension 加载后注册），不应以「冻结」规避；问题在没有版本与通知，不在动态本身。
- 下一步：RR0。

## 2026-10-08 — 计划 review 修订

- 消费方清单补全：编辑器 `EditorAutoPropertySection`（10）、`PropertyEditorMetadata`（4）、序列化器 / 拷贝器头文件模板分支、`LuaScriptLive` / `ScriptPropertyPreview`（ScriptValue）；RR0 改为全仓搜索出矩阵。
- 验证补 `reflects-core-test`、`reflects-generator-test`（使用 `ArgumentList`，不在 `ya-testing` 内）；Python 生成器去留在 RR5a 开工前定。
- RR5 拆为 RR5a（调用 ABI）/ RR5b（ScriptValue 替换）；关键路径定为 RR0→1→2→5a→5b→6→8，RR3 / RR4 / RR7 在关键路径外，避免拖住 C3 → S4 → G2.3。
- MetaValue（用户拍板）：内建种类含借用引用；三种存储形态（内联值 / 持有的盒子 / 借用引用）；reflects 补齐非 const 引用参数、输出参数、指针成员的动态访问（现状 `ArgumentList::get` 对 const any 做 any_cast，`T&` 编译不过、`const T&` 为拷贝）；安全句柄由引擎注册；裸借用引用不泄漏进脚本。
- `type_index`：实现跨 DLL、跨运行稳定，无需改；跨编译器值不同（Clang 实测 `glm::vec<3, float>` / `std::string`），边界加「永不持久化」，RR0 核查现状。
- 新增：facet 缺失策略、注册时哈希碰撞断言、按模块注销与线程模型（主线程帧边界注册、后台读快照）、`Example/` 场景重存零 diff、微基准不进 gtest 断言、三个 Windows 验证点、RR9 文档与 skill 吸收。
- 用户拍板：`ComponentJson::serializeCustom` 收编为 JSON facet；GUI 线 10F-6 / 10F-7 与 TypeOps 是同一件事，并入 RR4（避免编辑器另造一张平行注册表），10F-1~5 留 GUI 线先做，10F-8 排在 RR4 之后。
