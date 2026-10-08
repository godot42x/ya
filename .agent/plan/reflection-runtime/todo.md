# TODO

关键路径：RR0 → RR1 → RR2 → RR5a → RR5b → RR6 → RR8（C3 / G3.2 只依赖这条）

- [x] RR0 行为基线：全仓类型 × 系统矩阵 + characterization 测试 + 场景重存基线 + 微基准 + type_index 持久化核查
- [x] RR1 reflects-core：TypeOps + facet + MetaValue（三种存储形态）+ 碰撞断言（done：macOS 与 Windows 均通过，`ya-testing` 1737/1737、`reflects-core-test` 61/61）
- [x] RR2 引擎层注册值类型（glm、ScriptRef、句柄、ScriptRect）
- [ ] RR3 序列化器 / PropertyAccessor / Copier 改查 TypeOps；ComponentJson 收编为 JSON facet；场景零 diff（关键路径外）
- [ ] RR4 编辑器 facet，并入 GUI 10F-6 / 10F-7（关键路径外；等 GUI 10F-1~5）
- [x] RR5a invoker 收发 MetaValue；引用 / 输出参数；ArgumentList 退出外部 ABI；payload 删除
- [x] RR5b ScriptValue → MetaValue；Lua 投影器；借用引用不泄漏（macOS 通过；Windows 待验证）
- [x] RR6 最终成员视图下沉 reflects
- [ ] RR8 脚本 native 迁移（原 S5.2 / S5.3），删除 addNativeMethod（Windows 验证点）
- [ ] RR7 动态注册：按模块注销 + 版本号 + 通知 + 只读查询（关键路径外）
- [ ] RR9 README 重写 + skill 吸收
